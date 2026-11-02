// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_param_filter.h"

#include <algorithm>
#include <utility>

#include "base/strings/escape.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "third_party/re2/src/re2/re2.h"

namespace zephyrus_adblock {

BASE_FEATURE(kZephyrusAdblockStripTrackingParams,
             "ZephyrusAdblockStripTrackingParams",
             base::FEATURE_ENABLED_BY_DEFAULT);

namespace {

// Parameters that exist to say who sent you, and do nothing else. Only names no
// site needs to work: nothing like `id`, `ref`, `source` or `code`, which carry
// real state. The lists add the rest.
constexpr char kBuiltInRules[] = R"RULES(
*$removeparam=/^utm_(source|medium|campaign|term|content|id|name|cid|reader|brand|social|social-type|referrer|place|creative|adgroup|keyword)(=|$)/
*$removeparam=fbclid
*$removeparam=gclid
*$removeparam=gbraid
*$removeparam=wbraid
*$removeparam=dclid
*$removeparam=gclsrc
*$removeparam=msclkid
*$removeparam=twclid
*$removeparam=ttclid
*$removeparam=li_fat_id
*$removeparam=yclid
*$removeparam=igshid
*$removeparam=mc_cid
*$removeparam=mc_eid
*$removeparam=_hsenc
*$removeparam=_hsmi
*$removeparam=mkt_tok
*$removeparam=vero_id
*$removeparam=oly_anon_id
*$removeparam=oly_enc_id
*$removeparam=rb_clickid
*$removeparam=s_cid
*$removeparam=wickedid
*$removeparam=ef_id
*$removeparam=__s
)RULES";

// The literal text every match of the regex `source` must start with, or empty
// when there is none to rely on. `^utm_(source|medium)` gives "utm_".
std::string LiteralPrefix(std::string_view source) {
  if (source.empty() || source.front() != '^') {
    return std::string();
  }
  source.remove_prefix(1);
  // A top-level alternative (`^a|^b`) means the first prefix is not the only one.
  const size_t bar = source.find('|');
  const size_t paren = source.find('(');
  if (bar != std::string_view::npos &&
      (paren == std::string_view::npos || bar < paren)) {
    return std::string();
  }
  std::string prefix;
  for (size_t i = 0; i < source.size(); ++i) {
    const char ch = source[i];
    if (base::IsAsciiAlphaNumeric(ch) || ch == '_' || ch == '-') {
      prefix.push_back(ch);
      continue;
    }
    // `ad*` makes the `d` optional: it is not part of what must be there.
    if ((ch == '*' || ch == '?' || ch == '{') && !prefix.empty()) {
      prefix.pop_back();
    }
    break;
  }
  return prefix;
}

// Splits `options` on commas that are not escaped (uBlock writes a comma inside
// a regex as `\,`).
std::vector<std::string> SplitOptions(std::string_view options) {
  std::vector<std::string> out(1);
  for (size_t i = 0; i < options.size(); ++i) {
    if (options[i] == ',' && (i == 0 || options[i - 1] != '\\')) {
      out.emplace_back();
    } else {
      out.back().push_back(options[i]);
    }
  }
  return out;
}

}  // namespace

AdblockParamFilter::AdblockParamFilter() = default;
AdblockParamFilter::~AdblockParamFilter() = default;

size_t AdblockParamFilter::AddRules(std::string_view list_text) {
  // The shipped lists are 9 MB and about 270 lines mention the word, so jump
  // from one occurrence to the next and look only at the lines around them.
  size_t added = 0;
  size_t pos = 0;
  while ((pos = list_text.find("removeparam", pos)) != std::string_view::npos) {
    const size_t line_start = list_text.rfind('\n', pos);
    const size_t begin = line_start == std::string_view::npos ? 0 : line_start + 1;
    size_t end = list_text.find('\n', pos);
    if (end == std::string_view::npos) {
      end = list_text.size();
    }
    pos = end;
    std::string_view raw = list_text.substr(begin, end - begin);
    while (!raw.empty() && (raw.back() == '\r' || raw.back() == ' ')) {
      raw.remove_suffix(1);
    }
    while (!raw.empty() && raw.front() == ' ') {
      raw.remove_prefix(1);
    }
    if (raw.empty() || raw.front() == '!' || raw.front() == '[') {
      continue;
    }
    if (AddOne(raw)) {
      ++added;
    } else {
      ++skipped_count_;
    }
  }
  rule_count_ += added;
  Finalize();
  return added;
}

void AdblockParamFilter::AddBuiltInRules() {
  AddRules(kBuiltInRules);
}

void AdblockParamFilter::Finalize() {
  if (!dirty_) {
    return;
  }
  dirty_ = false;
  if (regex_sources_.empty()) {
    regex_set_.reset();
    return;
  }
  RE2::Options options;
  options.set_log_errors(false);
  options.set_max_mem(8 << 20);
  auto set = std::make_shared<RE2::Set>(options, RE2::UNANCHORED);
  for (const std::string& source : regex_sources_) {
    std::string error;
    if (set->Add(source, &error) < 0) {
      // Each was compiled on its own already; if the set refuses one, the set is
      // not trusted and every query falls through to the full test.
      regex_set_.reset();
      regex_sources_.clear();
      return;
    }
  }
  if (!set->Compile()) {
    regex_set_.reset();
    return;
  }
  regex_set_ = std::move(set);
}

bool AdblockParamFilter::QueryHasCandidate(std::string_view query) const {
  if (!regex_sources_.empty() && !regex_set_) {
    return true;  // the set could not be built: do the full test
  }
  size_t pos = 0;
  while (pos < query.size()) {
    size_t amp = query.find('&', pos);
    if (amp == std::string_view::npos) {
      amp = query.size();
    }
    const std::string_view piece = query.substr(pos, amp - pos);
    pos = amp + 1;
    if (piece.empty()) {
      continue;
    }
    std::string decoded;
    std::string_view text = piece;
    if (piece.find('%') != std::string_view::npos) {
      decoded = base::UnescapeURLComponent(piece, base::UnescapeRule::NORMAL);
      text = decoded;
    }
    if (names_.contains(text.substr(0, text.find('=')))) {
      return true;
    }
    if (regex_set_ && RegexMightMatch(text) && regex_set_->Match(text, nullptr)) {
      return true;
    }
  }
  return false;
}

bool AdblockParamFilter::RegexMightMatch(std::string_view text) const {
  if (has_unprefixed_regex_) {
    return true;
  }
  for (const std::string& prefix : regex_prefixes_) {
    if (text.size() >= prefix.size() &&
        base::EqualsCaseInsensitiveASCII(text.substr(0, prefix.size()),
                                         prefix)) {
      return true;
    }
  }
  return false;
}

bool AdblockParamFilter::AddOne(std::string_view line) {
  // The options start at the `$` before `removeparam`, not the last `$` of the
  // line: a regex may end in one (`removeparam=/^(a|b)$/`).
  const size_t spec_at = line.find("removeparam");
  if (spec_at == std::string_view::npos || spec_at == 0) {
    return false;
  }
  const size_t dollar = line.rfind('$', spec_at - 1);
  if (dollar == std::string_view::npos) {
    return false;
  }
  std::string pattern(line.substr(0, dollar));
  const bool exception = pattern.starts_with("@@");
  // A rule with no pattern (`$removeparam=gclid`) is for every URL.
  if (pattern.empty()) {
    pattern = "*";
  } else if (pattern == "@@") {
    pattern = "@@*";
  }

  Spec spec;
  spec.exception = exception;
  bool has_spec = false;
  std::string rest;
  for (const std::string& opt : SplitOptions(line.substr(dollar + 1))) {
    if (opt == "removeparam" || opt.starts_with("removeparam=")) {
      if (has_spec) {
        return false;  // two in one line: not a shape we know
      }
      has_spec = true;
      spec.raw = opt == "removeparam" ? std::string() : opt.substr(12);
      continue;
    }
    // `@@...$doc` is a page-level exception in the engine, not a type.
    if (exception && (opt == "doc" || opt == "document")) {
      return false;
    }
    if (!rest.empty()) {
      rest += ',';
    }
    rest += opt;
  }
  if (!has_spec) {
    return false;
  }

  if (spec.raw.empty()) {
    if (!exception) {
      return false;  // would strip every query from the matching pages
    }
    spec.kind = Spec::kAll;
  } else if (spec.raw.front() == '/') {
    const size_t close = spec.raw.rfind('/');
    if (close == 0) {
      return false;
    }
    const std::string_view flags = std::string_view(spec.raw).substr(close + 1);
    if (!flags.empty() && flags != "i") {
      return false;
    }
    std::string source = spec.raw.substr(1, close - 1);
    base::ReplaceSubstringsAfterOffset(&source, 0, "\\,", ",");
    RE2::Options options;
    options.set_log_errors(false);
    options.set_max_mem(1 << 20);
    options.set_case_sensitive(flags.empty());
    auto regex = std::make_shared<const RE2>(source, options);
    if (!regex->ok()) {
      return false;
    }
    spec.kind = Spec::kRegex;
    spec.regex = std::move(regex);
  } else if (spec.raw.front() == '~' ||
             spec.raw.find('=') != std::string::npos) {
    return false;  // `all except` and `name=value` are not supported
  } else {
    spec.kind = Spec::kName;
    spec.name = spec.raw;
  }

  std::string engine_line = pattern;
  if (!rest.empty()) {
    engine_line += '$' + rest;
  }
  // A rule that applies to every URL is decided by its name alone.
  spec.generic = !exception && pattern == "*" && rest.empty();
  specs_.push_back(std::move(spec));
  if (specs_.back().generic) {
    // no engine rule
  } else if (!engine_.AddTaggedRule(engine_line,
                                    static_cast<uint32_t>(specs_.size()))) {
    specs_.pop_back();  // an option the engine does not understand
    return false;
  } else {
    engine_has_rules_ = true;
  }
  const Spec& added = specs_.back();
  const uint32_t index = static_cast<uint32_t>(specs_.size() - 1);
  if (added.kind == Spec::kName && !added.exception) {
    names_.insert(added.name);
    specs_by_name_.emplace(added.name, index);
  } else if (added.kind == Spec::kRegex && !added.exception) {
    // `(?i)` carries the /i flag into the one shared automaton.
    regex_sources_.push_back((added.raw.ends_with("/i") ? "(?i)" : "") +
                             added.regex->pattern());
    regex_spec_index_.push_back(index);
    const std::string prefix = LiteralPrefix(added.regex->pattern());
    if (prefix.empty()) {
      has_unprefixed_regex_ = true;
    } else {
      regex_prefixes_.push_back(base::ToLowerASCII(prefix));
    }
  }
  dirty_ = true;
  return true;
}

bool AdblockParamFilter::Removes(const Spec& spec,
                                 std::string_view decoded_pair) const {
  switch (spec.kind) {
    case Spec::kName: {
      const size_t eq = decoded_pair.find('=');
      return decoded_pair.substr(0, eq) == spec.name;
    }
    case Spec::kRegex:
      return RE2::PartialMatch(decoded_pair, *spec.regex);
    case Spec::kAll:
      return false;
  }
  return false;
}

std::optional<GURL> AdblockParamFilter::Strip(const GURL& url,
                                              const GURL& initiator,
                                              ResourceType type) const {
  if (specs_.empty() || !url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
      !url.has_query() || url.query().empty()) {
    return std::nullopt;
  }
  // Almost no request has a parameter any rule removes: say so in one pass over
  // the query, before any rule is matched.
  if (!QueryHasCandidate(url.query())) {
    return std::nullopt;
  }

  // The rules that need matching (scoped to a host, a type, a page) and the
  // exceptions that can cancel any removal.
  std::vector<const Spec*> scoped;
  std::vector<const Spec*> exceptions;
  if (engine_has_rules_) {
    engine_.ForEachMatchingRule(
        url, initiator, type, [&](const FilterRule& rule) {
          if (rule.tag == 0 || rule.tag > specs_.size()) {
            return;
          }
          const Spec& spec = specs_[rule.tag - 1];
          (spec.exception ? exceptions : scoped).push_back(&spec);
        });
  }
  for (const Spec* exception : exceptions) {
    if (exception->kind == Spec::kAll) {
      return std::nullopt;  // the site is exempt from stripping altogether
    }
  }
  auto excepted = [&](const Spec& spec) {
    return std::ranges::any_of(exceptions, [&](const Spec* exception) {
      return exception->raw == spec.raw;
    });
  };

  // Keep every other piece of the query exactly as it was written.
  std::vector<std::string_view> kept;
  bool removed_any = false;
  for (const std::string_view piece :
       base::SplitStringPiece(url.query(), "&", base::KEEP_WHITESPACE,
                              base::SPLIT_WANT_ALL)) {
    const std::string decoded =
        base::UnescapeURLComponent(piece, base::UnescapeRule::NORMAL);
    // Only the rules this parameter could be removed by.
    std::vector<uint32_t> candidates;
    const auto named = specs_by_name_.equal_range(
        std::string_view(decoded).substr(0, decoded.find('=')));
    for (auto it = named.first; it != named.second; ++it) {
      candidates.push_back(it->second);
    }
    if (regex_set_ && RegexMightMatch(decoded)) {
      std::vector<int> matched;
      regex_set_->Match(decoded, &matched);
      for (const int m : matched) {
        candidates.push_back(regex_spec_index_[m]);
      }
    }
    bool drop = false;
    for (const uint32_t index : candidates) {
      const Spec& spec = specs_[index];
      if (!spec.generic && !std::ranges::contains(scoped, &spec)) {
        continue;  // a scoped rule that does not apply to this request
      }
      if (!excepted(spec)) {
        drop = true;
        break;
      }
    }
    if (drop) {
      removed_any = true;
    } else {
      kept.push_back(piece);
    }
  }
  if (!removed_any) {
    return std::nullopt;
  }
  GURL::Replacements replacements;
  std::string query;
  if (kept.empty()) {
    replacements.ClearQuery();
  } else {
    for (size_t i = 0; i < kept.size(); ++i) {
      if (i) {
        query += '&';
      }
      query.append(kept[i]);
    }
    replacements.SetQueryStr(query);
  }
  return url.ReplaceComponents(replacements);
}

}  // namespace zephyrus_adblock
