// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_SCRIPTLET_ENGINE_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_SCRIPTLET_ENGINE_H_

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "url/gurl.h"

namespace zephyrus_adblock {

// Parses "scriptlet injection" filters (domain##+js(name, args...)) and builds,
// for a page URL, a self-contained JavaScript payload that runs the applicable
// scriptlets. Scriptlets are small snippets (a ported subset of uBlock Origin's
// library) that patch the page's own JS to neutralize ads/anti-adblock — this
// is what makes YouTube and hard sites blockable. The payload is injected into
// the page's MAIN world at document-start by the renderer.
class AdblockScriptletEngine {
 public:
  AdblockScriptletEngine();
  AdblockScriptletEngine(const AdblockScriptletEngine&) = delete;
  AdblockScriptletEngine& operator=(const AdblockScriptletEngine&) = delete;
  ~AdblockScriptletEngine();

  // Parses filter-list text; returns the number of scriptlet rules kept.
  //
  // TRUSTED scriptlets (trusted-*) rewrite responses, set cookies and storage
  // and click elements, so -- as in uBO -- they are honoured only from uBO's
  // own lists. `filter_list_text` may be a combined list: each
  // "! ===== <label> =====" marker line sets the trust of the lines after it
  // (IsTrustedScriptletSectionMarker). Lines before the first marker take
  // `trust_unsectioned`: true for the browser's built-in rules, false for a
  // downloaded or bundled combined list, whose unmarked lines are third-party.
  size_t AddRules(std::string_view filter_list_text,
                  bool trust_unsectioned = true);

  // trusted-* rules refused because they came from a list not allowed them.
  size_t untrusted_scriptlets_dropped() const {
    return untrusted_scriptlets_dropped_;
  }

  // Returns the complete JS to inject on `url` (library + applicable scriptlet
  // invocations), or empty if none apply.
  std::string BuildInjectionScriptForUrl(const GURL& url) const;

  size_t rule_count() const { return rule_count_; }

 private:
  // One "+js(name, arg1, arg2)" invocation, stored as its raw argument list.
  struct Invocation {
    std::string name;
    std::vector<std::string> args;
    // "~sub.site.com" carve-outs from the rule's domain list.
    std::vector<std::string> excluded;
  };

  // Per-domain scriptlet invocations and per-domain exceptions (#@#+js).
  std::unordered_map<std::string, std::vector<Invocation>> domain_scriptlets_;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      domain_exceptions_;  // by scriptlet name
  // Sites with a bare "#@#+js()": no scriptlets at all.
  std::unordered_set<std::string> domain_disable_all_;

  size_t rule_count_ = 0;
  size_t untrusted_scriptlets_dropped_ = 0;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_SCRIPTLET_ENGINE_H_
