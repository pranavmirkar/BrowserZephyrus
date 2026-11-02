// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_PARAM_FILTER_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_PARAM_FILTER_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/feature_list.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "third_party/re2/src/re2/re2.h"
#include "third_party/re2/src/re2/set.h"
#include "url/gurl.h"


namespace zephyrus_adblock {

// Whether links lose their tracking parameters (utm_*, fbclid, gclid...) before
// the page is requested. On by default;
// `--disable-features=ZephyrusAdblockStripTrackingParams` is the kill switch.
BASE_DECLARE_FEATURE(kZephyrusAdblockStripTrackingParams);

// Tracking-parameter removal: `$removeparam` rules from the filter lists plus a
// built-in set of well-known tracking parameters.
//
// A link such as https://shop.example/item?id=7&utm_source=mail&fbclid=AbC is a
// request that tells the site, and everyone it shares the URL with, who sent you
// and which campaign. This removes the parameters that carry only that, and
// leaves the rest alone (`id=7`).
//
// The `$removeparam` rules are read here and not by AdblockFilterEngine, which
// keeps dropping them: if it admitted one it would BLOCK the request. Matching
// reuses the engine (so patterns, `$domain`, `$third-party` and resource types
// mean what they mean everywhere else), in a separate instance that only ever
// holds these rules.
//
// Supported, as in uBlock Origin: `$removeparam=name`, `$removeparam=/regex/`
// (tested against `name=value`), and the exception `@@...$removeparam` (all) or
// `@@...$removeparam=name` (that one). Dropped, and counted: `~name`, `name=value`,
// bare `$removeparam` blocks (they would strip every query), and rules with
// options the engine does not understand.
//
// Thread-compatible: build on any thread, then use const from one at a time.
class AdblockParamFilter {
 public:
  // Looks up a std::string_view without making a std::string of it.
  struct NameHash {
    using is_transparent = void;
    size_t operator()(std::string_view name) const {
      return std::hash<std::string_view>{}(name);
    }
  };

  AdblockParamFilter();
  AdblockParamFilter(const AdblockParamFilter&) = delete;
  AdblockParamFilter& operator=(const AdblockParamFilter&) = delete;
  ~AdblockParamFilter();

  // Adds every `$removeparam` line of `list_text`. Returns how many were usable.
  size_t AddRules(std::string_view list_text);
  // Adds the built-in tracking parameters.
  void AddBuiltInRules();

  // `url` without the parameters the rules remove, or nullopt when nothing is
  // removed (the usual case). `initiator` is the page that made the request
  // (null for a typed URL or a link from outside the browser), `type` the kind
  // of request. Only GET-like loads should be passed.
  std::optional<GURL> Strip(const GURL& url,
                            const GURL& initiator,
                            ResourceType type) const;

  size_t rule_count() const { return rule_count_; }
  size_t skipped_count() const { return skipped_count_; }

 private:
  struct Spec {
    enum Kind { kName, kRegex, kAll };
    Kind kind = kName;
    bool exception = false;
    // A rule for every URL, with no option: `$removeparam=gclid`. These are
    // most of the rules and need no matching, so they never enter the engine.
    bool generic = false;
    std::string raw;   // the text after `removeparam=`, to pair exceptions
    std::string name;  // for kName
    std::shared_ptr<const re2::RE2> regex;  // for kRegex
  };

  bool AddOne(std::string_view line);
  // Builds the lookup structures the cheap first test uses.
  void Finalize();
  // Whether any parameter of `query` is one some rule removes. A request that
  // fails this needs no rule matching at all, which is nearly all of them.
  bool QueryHasCandidate(std::string_view query) const;
  // Whether a regex rule could match `text` (its literal prefix is there).
  bool RegexMightMatch(std::string_view text) const;
  bool Removes(const Spec& spec, std::string_view decoded_pair) const;

  AdblockFilterEngine engine_;
  std::vector<Spec> specs_;  // a rule's tag is its index + 1
  size_t rule_count_ = 0;
  size_t skipped_count_ = 0;

  // The cheap first test. Names some rule removes, and one automaton over every
  // regex rule, so a query is judged in one pass rather than one try per rule.
  std::unordered_set<std::string, NameHash, std::equal_to<>> names_;
  // Which rules a parameter name can mean, and which rule each regex in the set
  // belongs to, so a parameter is only ever tried against the few that could
  // remove it.
  std::unordered_multimap<std::string, uint32_t, NameHash, std::equal_to<>>
      specs_by_name_;
  std::vector<uint32_t> regex_spec_index_;
  // What a regex rule's match must start with, so the automaton is only run on a
  // parameter that does. Regexes with no such prefix are always run.
  std::vector<std::string> regex_prefixes_;
  bool has_unprefixed_regex_ = false;
  // Whether the engine holds any rule: scoped removals or exceptions. When it
  // holds none, a candidate parameter is removed without any matching at all.
  bool engine_has_rules_ = false;
  std::vector<std::string> regex_sources_;  // as added to the set, in order
  std::shared_ptr<const re2::RE2::Set> regex_set_;
  bool dirty_ = false;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_PARAM_FILTER_H_
