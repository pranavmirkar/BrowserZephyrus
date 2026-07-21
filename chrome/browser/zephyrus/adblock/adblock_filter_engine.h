// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_FILTER_ENGINE_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_FILTER_ENGINE_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
  kTypeAll = 0,
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
  uint32_t type_mask = kTypeAll;      // allowed resource types (0 = all)
  uint32_t type_mask_not = 0;         // disallowed types (~script etc.)
  int party = 0;                 // 0 any, 1 first-party only, 2 third-party only
  std::vector<std::string> domains_included;  // $domain=a.com
  std::vector<std::string> domains_excluded;  // $domain=~b.com
};

// A compiled set of network filter rules with fast request matching.
//
// Not thread-safe for concurrent mutation; build once (AddRules) then Matches()
// concurrently from the network path. In practice both happen on the UI thread.
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
                   ResourceType type) const;

  size_t block_rule_count() const { return block_rule_count_; }
  size_t exception_rule_count() const { return exception_rule_count_; }

 private:
  // Rules keyed by a representative token from their pattern (for fast lookup).
  // Rules without a usable token go in `untokenized_*`.
  std::unordered_map<std::string, std::vector<FilterRule>> block_buckets_;
  std::unordered_map<std::string, std::vector<FilterRule>> exception_buckets_;
  std::vector<FilterRule> untokenized_block_;
  std::vector<FilterRule> untokenized_exception_;

  void AddParsedRule(FilterRule rule);
  // Runs the applicable candidate rules for `url`; returns true if any matches.
  bool AnyRuleMatches(
      const std::unordered_map<std::string, std::vector<FilterRule>>& buckets,
      const std::vector<FilterRule>& untokenized,
      const std::string& url_lower,
      std::string_view initiator_host,
      bool third_party,
      ResourceType type) const;

  size_t block_rule_count_ = 0;
  size_t exception_rule_count_ = 0;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_FILTER_ENGINE_H_
