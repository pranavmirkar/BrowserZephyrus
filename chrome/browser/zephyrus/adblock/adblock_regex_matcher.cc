// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_regex_matcher.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <utility>

#include "base/check_op.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/i18n/unicode/regex.h"
#include "third_party/re2/src/re2/filtered_re2.h"
#include "third_party/re2/src/re2/re2.h"

namespace zephyrus_adblock {
namespace {

// Literals shorter than this are not worth gating on: they occur in almost
// every URL, so a regex that offers nothing longer is cheaper to just try.
constexpr int kMinAtomLength = 3;

// How many regex rules are compiled, and how much memory RE2 may use for each
// (its program and its lazily built DFA together). The shipped lists hold about
// 400 regexes and the largest needs well under a megabyte; the caps are for a
// list that is not the shipped one.
constexpr size_t kMaxRegexRules = 4096;
constexpr int64_t kRegexMemoryBytes = 2 << 20;

// Gate literals are looked up by their first four bytes.
constexpr size_t kPrefixBytes = 4;
constexpr size_t kPrefixBitWords = 1024;  // 65,536 bits

// Per-thread working memory, so a request allocates nothing it did not have to.
struct Scratch {
  std::string lower;
  std::vector<uint32_t> candidates;
};

Scratch& ThreadScratch() {
  thread_local base::NoDestructor<Scratch> scratch;
  return *scratch;
}

uint32_t PrefixOf(std::string_view s, size_t at) {
  return static_cast<uint32_t>(static_cast<uint8_t>(s[at])) |
         static_cast<uint32_t>(static_cast<uint8_t>(s[at + 1])) << 8 |
         static_cast<uint32_t>(static_cast<uint8_t>(s[at + 2])) << 16 |
         static_cast<uint32_t>(static_cast<uint8_t>(s[at + 3])) << 24;
}

// 16 bits of a multiplicative hash of the prefix.
uint32_t HashPrefix(uint32_t prefix) {
  return (prefix * 2654435761u) >> 16;
}

// The lines of `list_text` that can be a regex rule: `/.../` or `@@/.../`,
// with or without options. The rest of the list is not looked at again.
std::string RegexLines(std::string_view list_text) {
  std::string out;
  size_t pos = 0;
  while (pos < list_text.size()) {
    size_t end = list_text.find('\n', pos);
    if (end == std::string_view::npos) {
      end = list_text.size();
    }
    std::string_view line = list_text.substr(pos, end - pos);
    pos = end + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.remove_suffix(1);
    }
    std::string_view body = line;
    if (body.starts_with("@@")) {
      body.remove_prefix(2);
    }
    if (body.size() >= 2 && body[0] == '/' &&
        body.find('/', 1) != std::string_view::npos) {
      out.append(line);
      out.push_back('\n');
    }
  }
  return out;
}

// How likely a URL is to contain this literal, 0..1. Only a guess, used to
// prefer the rarer of two correct gates and to decide that a gate is not worth
// scanning for; a wrong guess costs time, not accuracy.
double Commonness(std::string_view atom) {
  // Everything that is a piece of "https://www." is in nearly every URL.
  constexpr std::string_view kEveryUrl = "https://www.";
  // The shapes of ordinary URLs: top-level domains, file types, path stems.
  constexpr std::string_view kCommon[] = {
      ".com/", "com/",     ".net/",   "net/",     ".org/",    "org/",
      ".html", ".php",     ".json",   ".js",      ".css",     ".png",
      ".jpg",  ".gif",     ".svg",    ".woff",    "/js/",     "/css/",
      "/img/", "/images/", "/static/", "/assets/", "/api/",    "cdn.",
      "://cdn.", "/v1/",   "/v2/",    "index",    "https://st"};
  if (kEveryUrl.find(atom) != std::string_view::npos) {
    return 1.0;
  }
  for (std::string_view common : kCommon) {
    if (atom == common) {
      return 1.0;
    }
  }
  // Each further character is about four times rarer.
  const size_t extra = atom.size() > 2 ? atom.size() - 2 : 0;
  return 1.0 / static_cast<double>(uint64_t{1} << std::min<size_t>(2 * extra, 40));
}

// A gate at least this likely to be in a URL wakes its rules for so many
// requests that scanning for it costs more than just trying them.
constexpr double kNotWorthGating = 1.0 / 16;

// The literals of which a URL must contain at least one for `regex` to match,
// chosen to be rare; empty when there is no such set (the regex needs none, or
// FilteredRE2 found none), and then the regex is tried for every request.
std::vector<std::string> GatesOf(re2::FilteredRE2& regex) {
  std::vector<std::string> atoms;
  regex.Compile(&atoms);
  std::vector<int> potential;
  regex.AllPotentials({}, &potential);
  if (atoms.empty() || !potential.empty()) {
    return {};  // matches the empty set of literals: nothing to gate on
  }
  // A set S of atoms is sufficient when the regex cannot match a URL that has
  // every atom EXCEPT those in S. Start with all of them and drop the most
  // common ones first for as long as that stays true; what is left is a
  // sufficient set none of whose members can be spared.
  std::vector<size_t> order(atoms.size());
  std::iota(order.begin(), order.end(), size_t{0});
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    const double ca = Commonness(atoms[a]);
    const double cb = Commonness(atoms[b]);
    if (ca != cb) {
      return ca > cb;
    }
    return atoms[a].size() != atoms[b].size() ? atoms[a].size() < atoms[b].size()
                                              : a < b;
  });
  std::vector<bool> in_set(atoms.size(), true);
  std::vector<int> present;
  for (size_t a : order) {
    in_set[a] = false;
    present.clear();
    for (size_t i = 0; i < atoms.size(); ++i) {
      if (!in_set[i]) {
        present.push_back(static_cast<int>(i));
      }
    }
    regex.AllPotentials(present, &potential);
    if (!potential.empty()) {
      in_set[a] = true;  // the regex could match without any of the set: keep it
    }
  }
  std::vector<std::string> gates;
  for (size_t i = 0; i < atoms.size(); ++i) {
    if (!in_set[i]) {
      continue;
    }
    if (atoms[i].size() < kPrefixBytes ||
        atoms[i].find(static_cast<char>(0)) != std::string::npos ||
        Commonness(atoms[i]) >= kNotWorthGating) {
      return {};  // cannot be looked up, or not worth looking up
    }
    gates.push_back(std::move(atoms[i]));
  }
  return gates;
}

// Whether `p[at]` begins a lookahead or lookbehind group: (?= (?! (?<= (?<!
bool StartsLookaround(std::string_view p, size_t at) {
  if (at + 2 >= p.size() || p[at] != '(' || p[at + 1] != '?') {
    return false;
  }
  if (p[at + 2] == '=' || p[at + 2] == '!') {
    return true;
  }
  return p[at + 2] == '<' && at + 3 < p.size() &&
         (p[at + 3] == '=' || p[at + 3] == '!');
}

// The index just past the `)` that closes the group opened at `p[open]`, or
// npos. Escapes and character classes are skipped, so a parenthesis inside
// either does not count.
size_t EndOfGroup(std::string_view p, size_t open) {
  int depth = 0;
  bool in_class = false;
  for (size_t i = open; i < p.size(); ++i) {
    const char c = p[i];
    if (c == '\\') {
      ++i;
    } else if (in_class) {
      in_class = c != ']';
    } else if (c == '[') {
      in_class = true;
    } else if (c == '(') {
      ++depth;
    } else if (c == ')' && --depth == 0) {
      return i + 1;
    }
  }
  return std::string_view::npos;
}

// A regex RE2 can run that matches EVERYTHING `pattern` matches, and possibly
// more: each lookahead / lookbehind group is removed (an extra condition can
// only narrow a match) and each back-reference becomes `.*` (it matches some
// substring, and `.*` matches any). Used to gate a rule and as the first, cheap
// test; ICU then checks the real pattern. Null when `pattern` has neither (RE2
// refused it for some other reason), or when the removal would leave a
// quantifier with nothing to repeat, or for a named back-reference.
std::optional<std::string> WidenForRe2(std::string_view pattern) {
  std::string out;
  bool changed = false;
  bool in_class = false;
  size_t i = 0;
  while (i < pattern.size()) {
    const char c = pattern[i];
    if (c == '\\') {
      if (i + 1 >= pattern.size()) {
        return std::nullopt;
      }
      const char next = pattern[i + 1];
      if (!in_class && next == 'k') {
        return std::nullopt;
      }
      if (!in_class && next >= '1' && next <= '9') {
        out += ".*";
        changed = true;
      } else {
        out.append(pattern.substr(i, 2));
      }
      i += 2;
    } else if (in_class) {
      in_class = c != ']';
      out += c;
      ++i;
    } else if (c == '[') {
      in_class = true;
      out += c;
      ++i;
    } else if (StartsLookaround(pattern, i)) {
      const size_t end = EndOfGroup(pattern, i);
      if (end == std::string_view::npos) {
        return std::nullopt;
      }
      i = end;
      changed = true;
      if (i < pattern.size() && (pattern[i] == '*' || pattern[i] == '+' ||
                                 pattern[i] == '?' || pattern[i] == '{')) {
        return std::nullopt;
      }
    } else {
      out += c;
      ++i;
    }
  }
  if (!changed) {
    return std::nullopt;
  }
  return out;
}

struct SharedCache {
  base::Lock lock;
  uint64_t key = 0;
  scoped_refptr<const AdblockRegexMatcher> matcher;
};

SharedCache& Cache() {
  static base::NoDestructor<SharedCache> cache;
  return *cache;
}

// ICU's regex engine is a backtracking one, so a hostile pattern could run for
// a very long time on a chosen URL. It is only reached by a URL RE2 has already
// found plausible, and is held to these limits; exceeding either is "no match".
// The time limit counts blocks of 10,000 match-engine steps.
constexpr int32_t kIcuTimeLimit = 20;
// How many rules ICU may be asked about. The shipped lists have about 40.
constexpr size_t kMaxVerifiedRules = 512;
constexpr int32_t kIcuStackBytes = 256 * 1024;

}  // namespace

// A pattern in ICU's engine, which reads lookahead and back-references.
struct AdblockRegexMatcher::Verifier {
  std::unique_ptr<icu::RegexPattern> pattern;

  static std::unique_ptr<Verifier> Create(std::string_view regex,
                                          bool match_case) {
    UErrorCode status = U_ZERO_ERROR;
    UParseError parse_error;
    auto verifier = std::make_unique<Verifier>();
    verifier->pattern.reset(icu::RegexPattern::compile(
        icu::UnicodeString::fromUTF8(
            icu::StringPiece(regex.data(), static_cast<int32_t>(regex.size()))),
        match_case ? 0u : static_cast<uint32_t>(UREGEX_CASE_INSENSITIVE),
        parse_error, status));
    if (U_FAILURE(status) || !verifier->pattern) {
      return nullptr;
    }
    return verifier;
  }

  bool Matches(const std::string& url) const {
    UErrorCode status = U_ZERO_ERROR;
    const icu::UnicodeString text = icu::UnicodeString::fromUTF8(
        icu::StringPiece(url.data(), static_cast<int32_t>(url.size())));
    std::unique_ptr<icu::RegexMatcher> matcher(
        pattern->matcher(text, status));
    if (U_FAILURE(status) || !matcher) {
      return false;
    }
    matcher->setTimeLimit(kIcuTimeLimit, status);
    matcher->setStackLimit(kIcuStackBytes, status);
    if (U_FAILURE(status)) {
      return false;
    }
    const UBool found = matcher->find(status);
    return found && U_SUCCESS(status);
  }
};

AdblockRegexMatcher::AdblockRegexMatcher() = default;
AdblockRegexMatcher::~AdblockRegexMatcher() = default;

// static
scoped_refptr<const AdblockRegexMatcher> AdblockRegexMatcher::Build(
    const AdblockFilterEngine& engine) {
  auto matcher = base::WrapRefCounted(new AdblockRegexMatcher());
  matcher->Compile(engine.regex_rules());
  return matcher;
}

// static
scoped_refptr<const AdblockRegexMatcher> AdblockRegexMatcher::BuildFromList(
    std::string_view list_text) {
  AdblockFilterEngine parser;
  parser.AddRules(RegexLines(list_text));
  return Build(parser);
}

// static
scoped_refptr<const AdblockRegexMatcher> AdblockRegexMatcher::GetOrBuild(
    uint64_t list_key,
    std::string_view list_text) {
  if (list_key == 0) {
    return BuildFromList(list_text);
  }
  SharedCache& cache = Cache();
  base::AutoLock hold(cache.lock);
  if (cache.matcher && cache.key == list_key) {
    return cache.matcher;
  }
  cache.matcher = BuildFromList(list_text);
  cache.key = list_key;
  return cache.matcher;
}

// static
void AdblockRegexMatcher::ResetForTesting() {
  SharedCache& cache = Cache();
  base::AutoLock hold(cache.lock);
  cache.matcher.reset();
  cache.key = 0;
}

void AdblockRegexMatcher::Compile(const std::vector<FilterRule>& rules) {
  std::vector<std::vector<std::string>> gates_of_entry;
  for (const FilterRule& rule : rules) {
    if (entries_.size() >= kMaxRegexRules) {
      ++skipped_;
      continue;
    }
    RE2::Options options;
    options.set_case_sensitive(rule.match_case);
    options.set_log_errors(false);
    options.set_max_mem(kRegexMemoryBytes);
    auto regex = std::make_unique<re2::FilteredRE2>(kMinAtomLength);
    int id = -1;
    std::unique_ptr<Verifier> verifier;
    if (regex->Add(rule.pattern, options, &id) != RE2::NoError) {
      // RE2 refuses lookahead and back-references. Run a widened copy in RE2
      // and let ICU, which reads them, check the real pattern.
      const std::optional<std::string> widened = WidenForRe2(rule.pattern);
      // The bounded step first (RE2's memory cap), ICU's compile after it.
      if (!widened || verified_ >= kMaxVerifiedRules ||
          regex->Add(*widened, options, &id) != RE2::NoError) {
        ++skipped_;
        continue;
      }
      verifier = Verifier::Create(rule.pattern, rule.match_case);
      if (!verifier) {
        ++skipped_;
        continue;
      }
      ++verified_;
    }
    gates_of_entry.push_back(GatesOf(*regex));
    verifiers_.push_back(std::move(verifier));
    regexes_.push_back(std::move(regex));
    entries_.push_back(rule);
    entries_.back().pattern.clear();
    const FilterRule& kept = entries_.back();
    quick_.push_back({kept.type_mask, kept.type_mask_not,
                      kept.type_mask == kTypeAll && !kept.is_exception});
  }
  BuildGates(gates_of_entry);
}

void AdblockRegexMatcher::BuildGates(
    const std::vector<std::vector<std::string>>& gates_of_entry) {
  std::map<std::string, std::vector<uint32_t>> by_text;
  for (size_t entry = 0; entry < entries_.size(); ++entry) {
    if (gates_of_entry[entry].empty()) {
      always_.push_back(static_cast<uint32_t>(entry));
      continue;
    }
    for (const std::string& gate : gates_of_entry[entry]) {
      by_text[gate].push_back(static_cast<uint32_t>(entry));
    }
  }

  // std::map is ordered by text, so gates with the same first four bytes are
  // adjacent.
  prefix_bits_.assign(kPrefixBitWords, 0);
  std::map<uint32_t, std::pair<uint32_t, uint32_t>> runs;  // prefix -> gates
  for (const auto& [text, entries] : by_text) {
    const uint32_t prefix = PrefixOf(text, 0);
    auto it = runs.try_emplace(prefix, static_cast<uint32_t>(gates_.size()), 0u)
                  .first;
    ++it->second.second;
    gates_.push_back({text, static_cast<uint32_t>(gate_entries_.size()),
                      static_cast<uint32_t>(entries.size())});
    gate_entries_.insert(gate_entries_.end(), entries.begin(), entries.end());
  }
  size_t slot_count = 16;
  while (slot_count < runs.size() * 4) {
    slot_count *= 2;
  }
  slots_.assign(slot_count, Slot());
  for (const auto& [prefix, run] : runs) {
    const uint32_t hash = HashPrefix(prefix);
    prefix_bits_[hash >> 6] |= uint64_t{1} << (hash & 63);
    size_t at = hash & (slot_count - 1);
    while (slots_[at].prefix != 0) {
      at = (at + 1) & (slot_count - 1);
    }
    slots_[at] = {prefix, run.first, run.second};
  }
}

RegexHits AdblockRegexMatcher::Match(const GURL& url,
                                     const GURL& initiator,
                                     ResourceType type) const {
  RegexHits hits;
  if (entries_.empty() || !url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return hits;
  }
  const std::string& spec = url.spec();
  if (spec.size() > kMaxMatchedUrlBytes) {
    return hits;
  }

  std::string_view initiator_host;
  if (initiator.is_valid() && !initiator.host().empty()) {
    initiator_host = initiator.host();
  }
  const std::string_view request_host = url.host();
  // Worked out the first time a candidate has a party option, not before.
  enum class Party { kUnknown, kFirst, kThird };
  Party party = Party::kUnknown;

  // Runs one rule against the request; true when nothing can change the
  // verdict any more (an $important rule matched).
  auto try_rule = [&](uint32_t id) {
    // The cheap half of the type test, from 12 bytes.
    const Quick& quick = quick_[id];
    if ((quick.type_mask != kTypeAll && !(quick.type_mask & type)) ||
        (quick.type_not & type) ||
        (quick.untyped_block && (type & (kTypeDocument | kTypePopup)))) {
      return false;
    }
    const FilterRule& rule = entries_[id];
    // A verdict already reached cannot change; do not run its regex again.
    bool& decided = rule.is_exception ? hits.exception
                    : rule.important  ? hits.important
                                      : hits.block;
    if (decided) {
      return false;
    }
    // Everything else the rule asks of the request.
    if (rule.party != 0 && party == Party::kUnknown) {
      party = initiator_host.empty() ||
                      net::registry_controlled_domains::SameDomainOrHost(
                          url, initiator,
                          net::registry_controlled_domains::
                              INCLUDE_PRIVATE_REGISTRIES)
                  ? Party::kFirst
                  : Party::kThird;
    }
    if (!RuleOptionsMatch(rule, type, party == Party::kThird, initiator_host,
                          request_host)) {
      return false;
    }
    // RE2 first: for a lookahead rule it is the widened regex, which says
    // "possibly", and only then is ICU asked.
    if (RE2::PartialMatch(spec, regexes_[id]->GetRE2(0)) &&
        (!verifiers_[id] || verifiers_[id]->Matches(spec))) {
      decided = true;
    }
    return hits.important;  // nothing overrides it
  };

  // Rules with no gate are tried for every request.
  for (uint32_t id : always_) {
    if (try_rule(id)) {
      return hits;
    }
  }
  if (gates_.empty() || spec.size() < kPrefixBytes) {
    return hits;
  }

  // Which gated rules does this URL wake? The gates are lowercase; the regexes
  // run on the URL as it is, so a $match-case rule sees the case it was
  // written for.
  Scratch& scratch = ThreadScratch();
  scratch.lower.assign(spec);
  for (char& c : scratch.lower) {
    c = base::ToLowerASCII(c);
  }
  const std::string_view text(scratch.lower);
  std::vector<uint32_t>& woken = scratch.candidates;
  woken.clear();
  const size_t mask = slots_.size() - 1;
  for (size_t i = 0; i + kPrefixBytes <= text.size(); ++i) {
    const uint32_t prefix = PrefixOf(text, i);
    const uint32_t hash = HashPrefix(prefix);
    if (!((prefix_bits_[hash >> 6] >> (hash & 63)) & 1)) {
      continue;
    }
    for (size_t at = hash & mask; slots_[at].prefix != 0;
         at = (at + 1) & mask) {
      const Slot& slot = slots_[at];
      if (slot.prefix != prefix) {
        continue;
      }
      const std::string_view rest = text.substr(i);
      for (uint32_t g = slot.first_gate; g < slot.first_gate + slot.gate_count;
           ++g) {
        const Gate& gate = gates_[g];
        if (rest.starts_with(gate.text)) {
          woken.insert(woken.end(), gate_entries_.begin() + gate.first_entry,
                       gate_entries_.begin() + gate.first_entry +
                           gate.entry_count);
        }
      }
      break;
    }
  }
  if (woken.empty()) {
    return hits;
  }
  // A rule with several gates can be woken by more than one.
  if (woken.size() > 1) {
    std::sort(woken.begin(), woken.end());
    woken.erase(std::unique(woken.begin(), woken.end()), woken.end());
  }
  for (uint32_t id : woken) {
    if (try_rule(id)) {
      break;
    }
  }
  return hits;
}

}  // namespace zephyrus_adblock
