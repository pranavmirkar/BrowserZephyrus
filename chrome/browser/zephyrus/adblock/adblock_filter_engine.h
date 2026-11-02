// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_FILTER_ENGINE_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_FILTER_ENGINE_H_

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/functional/function_ref.h"
#include "url/gurl.h"

namespace zephyrus_adblock {

// Resource types a network rule can be scoped to (via $script, $image, ...).
// Bit flags; kAll (0) means "no type restriction".
enum ResourceType : uint32_t {
  kTypeOther = 1 << 0,
  kTypeScript = 1 << 1,
  kTypeImage = 1 << 2,
  kTypeStylesheet = 1 << 3,
  kTypeObject = 1 << 4,
  kTypeXhr = 1 << 5,
  kTypeSubdocument = 1 << 6,  // iframes
  kTypeFont = 1 << 7,
  kTypeMedia = 1 << 8,
  kTypePing = 1 << 9,
  kTypeWebsocket = 1 << 10,
  kTypePopup = 1 << 11,  // new-window/tab (pop-ups, pop-unders) via $popup
  kTypeDocument = 1 << 12,  // a top-level page load ($document)
  kTypeAll = 0,
};

// Every type bit, which is what `$all` means: unlike a rule with no type
// option, it also reaches top-level documents and pop-ups.
inline constexpr uint32_t kTypeEverything = (1u << 13) - 1;

// Page-level exceptions an `@@` rule can grant the document it matches.
enum DocumentException : uint32_t {
  kExceptDocument = 1 << 0,      // $document: no filtering on the page at all
  kExceptGenericHide = 1 << 1,   // $generichide: no generic cosmetic filters
  kExceptElemHide = 1 << 2,      // $elemhide: no cosmetic filters at all
  kExceptSpecificHide = 1 << 3,  // $specifichide: no site-specific cosmetics
};

// A single parsed network filter rule (EasyList/ABP "static" network syntax).
struct FilterRule {
  enum Anchor {
    kAnchorNone = 0,     // substring match anywhere
    kAnchorHostname,     // "||" — match at a domain boundary
    kAnchorStart,        // leading "|" — match at start of URL
  };

  std::string pattern;           // normalized match pattern (may contain * ^)
  Anchor anchor = kAnchorNone;
  bool anchor_end = false;       // trailing "|"
  bool is_exception = false;     // "@@" allowlist rule
  bool important = false;        // "$important" overrides exceptions
  // A pure hostname rule ("||ads.example^"): the only untyped kind allowed to
  // block a whole page or a pop-up, as in uBO. An untyped path rule such as
  // `/ads/*` would otherwise refuse to load any article whose URL has "/ads/"
  // in it.
  bool pure_host = false;
  uint32_t document_exceptions = 0;  // DocumentException bits, @@ rules only
  uint32_t type_mask = kTypeAll;      // allowed resource types (0 = all)
  uint32_t type_mask_not = 0;         // disallowed types (~script etc.)
  int party = 0;                 // 0 any, 1 first-party only, 2 third-party only
  std::vector<std::string> domains_included;  // $domain=a.com
  std::vector<std::string> domains_excluded;  // $domain=~b.com
  // $denyallow=a.com|b.com: the rule does not apply to requests TO these.
  std::vector<std::string> denyallow;
  // $to=a.com|~b.a.com: the rule applies only to requests whose host is (a
  // subdomain of) one of the first, and not one of the second.
  std::vector<std::string> to_included;
  std::vector<std::string> to_excluded;
  // The parser sorts each of the five domain lists above (plain domains first,
  // then `name.*` entities) and sets this, so that a long list -- some rules
  // name a hundred sites -- is searched by host suffix instead of end to end.
  // A rule built any other way leaves it false and is searched linearly.
  bool domain_lists_sorted = false;
  // An opaque number the caller attached with AddTaggedRule (0 = none). Used by
  // AdblockParamFilter to find which `$removeparam` spec a matching rule stands
  // for. Never read by the matcher, and not part of the compiled index.
  uint32_t tag = 0;
  // A `/regex/` rule: `pattern` is the regex source (original case), not a
  // pattern for MatchAt. These never enter the keyword buckets or the compiled
  // index; AdblockRegexMatcher matches them.
  bool is_regex = false;
  // `$match-case`: the regex is case-sensitive. Only a regex rule may have it;
  // on any other rule the line is dropped, because the engine lowercases both
  // the URL and the pattern.
  bool match_case = false;
};

// The answer a set of regex rules gives for one request. The regex rules are
// matched apart from the keyword buckets / compiled index (RE2, not MatchAt),
// and the three flags are ORed into the verdict of whichever of those is in
// use, in the same order: important, then block, then exception.
struct RegexHits {
  bool important = false;
  bool block = false;
  bool exception = false;
};

// Whether a rule's OPTIONS admit this request: resource type, first/third
// party, $domain=, $denyallow=, $to=. Everything but the pattern. Shared by the
// engine and the regex matcher so the two cannot read an option differently.
// `request_host` and `initiator_host` are lowercase.
bool RuleOptionsMatch(const FilterRule& rule,
                      ResourceType type,
                      bool third_party,
                      std::string_view initiator_host,
                      std::string_view request_host);

// A compiled set of network filter rules with fast request matching.
//
// Not thread-safe for concurrent mutation; build once (AddRules) then Matches()
// concurrently from the network path. In practice both happen on the UI thread.
// Heterogeneous lookup, so a request's tokens -- views into its URL -- find
// their bucket without being copied into strings first.
struct TransparentStringHash {
  using is_transparent = void;
  size_t operator()(std::string_view value) const {
    return std::hash<std::string_view>{}(value);
  }
};
using RuleBuckets = std::unordered_map<std::string,
                                       std::vector<FilterRule>,
                                       TransparentStringHash,
                                       std::equal_to<>>;

class AdblockFilterEngine {
 public:
  AdblockFilterEngine();
  AdblockFilterEngine(const AdblockFilterEngine&) = delete;
  AdblockFilterEngine& operator=(const AdblockFilterEngine&) = delete;
  ~AdblockFilterEngine();

  // Parses filter-list text (one rule per line; comments start with ! or [).
  // Cosmetic (##) and unsupported rules are skipped. Returns rules parsed.
  size_t AddRules(std::string_view filter_list_text);

  // Returns true if a request to `url` should be BLOCKED. `initiator` is the
  // document origin making the request (for first/third-party classification),
  // `type` is the resource type. Exception (@@) rules override block rules.
  bool ShouldBlock(const GURL& url,
                   const GURL& initiator,
                   ResourceType type,
                   const RegexHits* regex = nullptr) const;

  // The `/regex/` rules the lists hold, in list order. The engine only parses
  // them; AdblockRegexMatcher::Build() compiles them.
  const std::vector<FilterRule>& regex_rules() const { return regex_rules_; }

  // The DocumentException bits the lists grant a page at `document_url`
  // (`@@||site^$generichide` and friends). 0 for nearly every page.
  uint32_t GetDocumentExceptions(const GURL& document_url) const;

  size_t block_rule_count() const { return block_rule_count_; }
  size_t exception_rule_count() const { return exception_rule_count_; }

  // Parses ONE line like AddRules, and stamps the rule it produces with `tag`.
  // A `*`-untyped rule added this way also reaches page loads (it is for rules
  // that are not blocks, such as `$removeparam`, where a navigation is the
  // case that matters). Returns whether a rule was added.
  bool AddTaggedRule(std::string_view line, uint32_t tag);

  // Calls `visit` for EVERY block or exception rule that matches this request,
  // not only the first. For callers that need to know which rules apply (the
  // `$removeparam` rules and their exceptions), not just whether one does.
  void ForEachMatchingRule(const GURL& url,
                           const GURL& initiator,
                           ResourceType type,
                           base::FunctionRef<void(const FilterRule&)> visit)
      const;

  // Which of the engine's four rule sets a rule lives in.
  enum class RuleGroup { kBlock, kException, kImportant, kDocument };

  // Calls `visit` once for every rule the engine holds. `keyword` is the bucket
  // key the engine filed it under, empty for rules it could not key. This is
  // how a compiled index (AdblockZIndex) is built from the real parser's
  // output rather than from a second parser, so the two cannot disagree about
  // what a line of a list means.
  void ForEachRule(base::FunctionRef<void(RuleGroup,
                                          const FilterRule&,
                                          std::string_view keyword)> visit)
      const;

 private:
  // Rules keyed by a representative token from their pattern (for fast lookup).
  // Rules without a usable token go in `untokenized_*`.
  RuleBuckets block_buckets_;
  RuleBuckets exception_buckets_;
  std::vector<FilterRule> untokenized_block_;
  std::vector<FilterRule> untokenized_exception_;
  // $important block rules: tried first, and no exception undoes them.
  RuleBuckets important_buckets_;
  std::vector<FilterRule> untokenized_important_;
  // Rules granting page-level exceptions, matched against the document URL.
  RuleBuckets document_buckets_;
  std::vector<FilterRule> untokenized_document_;

  // `/regex/` rules (block, exception and $important alike; each says which).
  std::vector<FilterRule> regex_rules_;

  // Rule texts cancelled by a `$badfilter` twin, normalised without it.
  std::unordered_set<std::string> badfilters_;

  void AddParsedRule(FilterRule rule);
  // Runs the applicable candidate rules for `url`; returns true if any
  // matches. With `collect_exceptions`, visits EVERY match and ORs their
  // document_exceptions into it instead of stopping at the first.
  bool AnyRuleMatches(const RuleBuckets& buckets,
                      const std::vector<FilterRule>& untokenized,
                      const std::string& url_lower,
                      std::string_view initiator_host,
                      bool third_party,
                      ResourceType type,
                      uint32_t* collect_exceptions = nullptr,
                      const base::FunctionRef<void(const FilterRule&)>*
                          on_each_match = nullptr) const;
  // Stamps the next rule AddParsedRule files; see AddTaggedRule.
  uint32_t pending_tag_ = 0;

  size_t block_rule_count_ = 0;
  size_t exception_rule_count_ = 0;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_FILTER_ENGINE_H_
