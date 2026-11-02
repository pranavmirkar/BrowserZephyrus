// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_urlskip_filter.h"

#include <algorithm>
#include <utility>

#include "base/base64.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"

namespace zephyrus_adblock {

BASE_FEATURE(kZephyrusAdblockUrlSkip,
             "ZephyrusAdblockUrlSkip",
             base::FEATURE_ENABLED_BY_DEFAULT);

namespace {

// A rule has a handful of steps; a list that has more is not one we know.
constexpr size_t kMaxSteps = 8;
// The longest "current string" a step may produce or be given. A link that
// carries kilobytes of nested redirect is not a link to rewrite.
constexpr size_t kMaxStringBytes = 16 * 1024;

// The options a rule can carry besides `urlskip=`. Used to tell where the value
// of `urlskip=` ends: `urlskip=?url,doc,1p` has two options after it, while the
// comma in `urlskip=/x{0,2}y/` belongs to the regex.
bool IsOptionToken(std::string_view token) {
  if (!token.empty() && token.front() == '~') {
    token.remove_prefix(1);
  }
  const std::string_view name = token.substr(0, token.find('='));
  static constexpr std::string_view kNames[] = {
      "doc",        "document",    "1p",           "3p",
      "first-party", "third-party", "strict1p",     "strict3p",
      "all",        "image",       "script",       "stylesheet",
      "css",        "xhr",         "xmlhttprequest", "frame",
      "subdocument", "font",       "media",        "object",
      "ping",       "websocket",   "other",        "popup",
      "important",  "match-case",  "to",           "from",
      "domain",     "denyallow",   "badfilter",    "header",
      "method",     "redirect",    "redirect-rule", "csp",
      "removeparam", "ipaddress",  "reason",
  };
  return std::ranges::find(kNames, name) != std::end(kNames);
}

// `text` split into steps. A `/regex/` step may hold spaces, so it runs to its
// closing slash (an escaped slash, or one inside [...], does not close it).
// Empty when the text has an unclosed regex.
std::vector<std::string_view> Tokenize(std::string_view text) {
  std::vector<std::string_view> tokens;
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == ' ') {
      ++i;
      continue;
    }
    const size_t start = i;
    if (text[i] == '/') {
      bool in_class = false;
      size_t j = i + 1;
      for (; j < text.size(); ++j) {
        if (text[j] == '\\') {
          ++j;
        } else if (in_class) {
          in_class = text[j] != ']';
        } else if (text[j] == '[') {
          in_class = true;
        } else if (text[j] == '/') {
          break;
        }
      }
      if (j >= text.size()) {
        return {};
      }
      i = j + 1;
    }
    while (i < text.size() && text[i] != ' ') {
      ++i;
    }
    tokens.push_back(text.substr(start, i - start));
  }
  return tokens;
}

// "https://x", "ftp://x": a scheme followed by `://`.
bool HasLeadingProtocol(std::string_view s) {
  if (s.empty() || !base::IsAsciiAlpha(s.front())) {
    return false;
  }
  for (size_t i = 1; i < s.size(); ++i) {
    const char c = s[i];
    if (c == ':') {
      return s.substr(i).starts_with("://");
    }
    if (!base::IsAsciiAlphaNumeric(c) && c != '+' && c != '-' && c != '.') {
      return false;
    }
  }
  return false;
}

bool HasControlCharacter(std::string_view s) {
  return std::ranges::any_of(
      s, [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7f; });
}

// decodeURIComponent: every `%` must start a hex pair, and the result must be
// UTF-8; otherwise there is no result (JavaScript throws).
std::optional<std::string> DecodeUriComponent(std::string_view s) {
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && (i + 2 >= s.size() || !base::IsHexDigit(s[i + 1]) ||
                        !base::IsHexDigit(s[i + 2]))) {
      return std::nullopt;
    }
  }
  std::string decoded = base::UnescapeBinaryURLComponent(s);
  if (!base::IsStringUTF8(decoded)) {
    return std::nullopt;
  }
  return decoded;
}

}  // namespace

AdblockUrlSkipFilter::AdblockUrlSkipFilter() = default;
AdblockUrlSkipFilter::~AdblockUrlSkipFilter() = default;

// static
bool AdblockUrlSkipFilter::ParseSteps(std::string_view text, Spec* spec) {
  const std::vector<std::string_view> tokens = Tokenize(text);
  if (tokens.empty() || tokens.size() > kMaxSteps) {
    return false;
  }
  for (const std::string_view token : tokens) {
    Step step;
    if (token == "-blocked") {
      spec->also_when_blocked = true;
      continue;
    } else if (token == "+https") {
      step.kind = Step::kPlusHttps;
    } else if (token == "-base64") {
      step.kind = Step::kBase64;
    } else if (token == "-safebase64") {
      step.kind = Step::kSafeBase64;
    } else if (token == "-uricomponent") {
      step.kind = Step::kUriComponent;
    } else if (token == "#") {
      step.kind = Step::kFragment;
    } else if (token.size() > 1 && token.front() == '?') {
      step.kind = Step::kQuery;
      step.name = std::string(token.substr(1));
    } else if (token.size() > 1 && token.front() == '&') {
      step.kind = Step::kParamName;
      size_t position = 0;
      if (!base::StringToSizeT(token.substr(1), &position) || position == 0) {
        return false;
      }
      step.position = position;
    } else if (token.size() > 2 && token.front() == '/' &&
               token.back() == '/') {
      RE2::Options options;
      options.set_log_errors(false);
      options.set_max_mem(1 << 20);
      auto regex = std::make_shared<const RE2>(
          token.substr(1, token.size() - 2), options);
      if (!regex->ok() || regex->NumberOfCapturingGroups() < 1) {
        return false;
      }
      step.kind = Step::kRegex;
      step.regex = std::move(regex);
    } else {
      return false;  // a step this code does not know: the rule is not ours
    }
    spec->steps.push_back(std::move(step));
  }
  return !spec->steps.empty();
}

// static
std::optional<GURL> AdblockUrlSkipFilter::ApplySteps(std::string_view steps,
                                                     const GURL& url) {
  Spec spec;
  if (!ParseSteps(steps, &spec)) {
    return std::nullopt;
  }
  return Run(spec, url);
}

// static
std::optional<GURL> AdblockUrlSkipFilter::Run(const Spec& spec,
                                              const GURL& url) {
  if (!url.is_valid()) {
    return std::nullopt;
  }
  std::string current = url.spec();
  for (const Step& step : spec.steps) {
    switch (step.kind) {
      case Step::kQuery: {
        const GURL as_url(current);
        if (!as_url.is_valid() || !as_url.has_query()) {
          return std::nullopt;
        }
        std::optional<std::string> value;
        for (net::QueryIterator it(as_url); !it.IsAtEnd(); it.Advance()) {
          if (it.GetKey() == step.name) {
            value = it.GetUnescapedValue();
            break;
          }
        }
        if (!value) {
          return std::nullopt;
        }
        current = *std::move(value);
        break;
      }
      case Step::kParamName: {
        const GURL as_url(current);
        if (!as_url.is_valid() || !as_url.has_query()) {
          return std::nullopt;
        }
        size_t seen = 0;
        std::optional<std::string> name;
        for (net::QueryIterator it(as_url); !it.IsAtEnd(); it.Advance()) {
          if (++seen == step.position) {
            name = std::string(it.GetKey());
            break;
          }
        }
        if (!name) {
          return std::nullopt;
        }
        current = *std::move(name);
        break;
      }
      case Step::kFragment: {
        const size_t hash = current.find('#');
        if (hash == std::string::npos) {
          return std::nullopt;
        }
        current = current.substr(hash + 1);
        break;
      }
      case Step::kRegex: {
        std::string capture;
        if (!RE2::PartialMatch(current, *step.regex, &capture)) {
          return std::nullopt;
        }
        current = std::move(capture);
        break;
      }
      case Step::kPlusHttps: {
        if (base::StartsWith(current, "http://",
                             base::CompareCase::INSENSITIVE_ASCII)) {
          current = "https://" + current.substr(7);
        } else if (!HasLeadingProtocol(current)) {
          current = "https://" + current;
        }
        break;
      }
      case Step::kBase64:
      case Step::kSafeBase64: {
        std::string input = current;
        if (step.kind == Step::kSafeBase64) {
          std::ranges::replace(input, '-', '+');
          std::ranges::replace(input, '_', '/');
        }
        std::string decoded;
        if (!base::Base64Decode(input, &decoded,
                                base::Base64DecodePolicy::kForgiving)) {
          return std::nullopt;
        }
        current = std::move(decoded);
        break;
      }
      case Step::kUriComponent: {
        std::optional<std::string> decoded = DecodeUriComponent(current);
        if (!decoded) {
          return std::nullopt;
        }
        current = *std::move(decoded);
        break;
      }
    }
    if (current.empty() || current.size() > kMaxStringBytes) {
      return std::nullopt;
    }
  }

  // The result has to be a web address, other than the one asked for, that
  // carries no credentials: a redirect must not turn a link into one that
  // reads `https://trusted.example@elsewhere.example`.
  if (HasControlCharacter(current)) {
    return std::nullopt;
  }
  GURL target(current);
  if (!target.is_valid() || !target.SchemeIsHTTPOrHTTPS() ||
      target.host().empty() || target.has_username() || target.has_password() ||
      target.GetWithoutRef() == url.GetWithoutRef()) {
    return std::nullopt;
  }
  return target;
}

size_t AdblockUrlSkipFilter::AddRules(std::string_view list_text,
                                      bool trust_unsectioned) {
  bool trusted = trust_unsectioned;
  size_t added = 0;
  for (const std::string_view raw : base::SplitStringPiece(
           list_text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (raw.front() == '!') {
      if (raw.starts_with("! =====")) {
        trusted = IsTrustedScriptletSectionMarker(raw);
      }
      continue;
    }
    if (raw.find("urlskip=") == std::string_view::npos) {
      continue;
    }
    if (!trusted) {
      ++untrusted_count_;
      continue;
    }
    if (AddOne(raw)) {
      ++added;
    } else {
      ++skipped_count_;
    }
  }
  rule_count_ += added;
  return added;
}

bool AdblockUrlSkipFilter::AddOne(std::string_view line) {
  if (line.starts_with("@@")) {
    return false;  // what an exception to a skip would mean is not specified
  }
  // The options start at the `$` before `urlskip=`: a regex step may end in one.
  const size_t at = line.find("urlskip=");
  if (at == std::string_view::npos || at == 0) {
    return false;
  }
  const size_t dollar = line.rfind('$', at - 1);
  if (dollar == std::string_view::npos || dollar == 0) {
    return false;
  }
  const std::string pattern(line.substr(0, dollar));

  std::string steps;
  std::vector<std::string> rest;
  bool seen = false;
  bool in_value = false;
  for (const std::string_view token : base::SplitStringPiece(
           line.substr(dollar + 1), ",", base::KEEP_WHITESPACE,
           base::SPLIT_WANT_ALL)) {
    if (!seen && token.starts_with("urlskip=")) {
      seen = true;
      in_value = true;
      steps = std::string(token.substr(8));
    } else if (in_value && !IsOptionToken(token)) {
      steps += ',';
      steps += token;
    } else {
      in_value = false;
      rest.emplace_back(token);
    }
  }
  if (!seen) {
    return false;
  }

  Spec spec;
  if (!ParseSteps(steps, &spec)) {
    return false;
  }
  std::string engine_line = pattern;
  if (!rest.empty()) {
    engine_line += '$' + base::JoinString(rest, ",");
  }

  // Which kind of pattern it is, and whether the engine can read the options.
  AdblockFilterEngine probe;
  if (probe.AddRules(engine_line) != 1) {
    return false;
  }
  specs_.push_back(std::move(spec));
  if (!probe.regex_rules().empty()) {
    FilterRule options = probe.regex_rules().front();
    RE2::Options re2_options;
    re2_options.set_log_errors(false);
    re2_options.set_max_mem(1 << 20);
    re2_options.set_case_sensitive(options.match_case);
    auto regex = std::make_shared<const RE2>(options.pattern, re2_options);
    if (!regex->ok()) {
      specs_.pop_back();  // lookahead and the like
      return false;
    }
    options.pattern.clear();
    regex_rules_.push_back({std::move(options), std::move(regex),
                            specs_.size() - 1});
    return true;
  }
  if (!engine_.AddTaggedRule(engine_line, static_cast<uint32_t>(specs_.size()))) {
    specs_.pop_back();
    return false;
  }
  return true;
}

std::optional<GURL> AdblockUrlSkipFilter::Skip(const GURL& url,
                                               const GURL& initiator,
                                               ResourceType type,
                                               bool blocked) const {
  if (specs_.empty() || !url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  std::vector<size_t> hits;
  engine_.ForEachMatchingRule(
      url, initiator, type, [&](const FilterRule& rule) {
        if (!rule.is_exception && rule.tag != 0 && rule.tag <= specs_.size()) {
          hits.push_back(rule.tag - 1);
        }
      });
  if (!regex_rules_.empty()) {
    std::string_view initiator_host;
    bool third_party = false;
    if (initiator.is_valid() && !initiator.host().empty()) {
      initiator_host = initiator.host();
      third_party = !net::registry_controlled_domains::SameDomainOrHost(
          url, initiator,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
    }
    for (const RegexRule& rule : regex_rules_) {
      if (RuleOptionsMatch(rule.options, type, third_party, initiator_host,
                           url.host()) &&
          RE2::PartialMatch(url.spec(), *rule.pattern)) {
        hits.push_back(rule.spec);
      }
    }
  }
  if (hits.empty()) {
    return std::nullopt;  // nearly every navigation
  }
  // The earliest rule in the list that can skip wins.
  std::sort(hits.begin(), hits.end());
  hits.erase(std::unique(hits.begin(), hits.end()), hits.end());
  for (const size_t index : hits) {
    const Spec& spec = specs_[index];
    if (blocked && !spec.also_when_blocked) {
      continue;
    }
    if (std::optional<GURL> target = Run(spec, url)) {
      return target;
    }
  }
  return std::nullopt;
}

}  // namespace zephyrus_adblock
