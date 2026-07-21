// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_COSMETIC_ENGINE_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_COSMETIC_ENGINE_H_

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "url/gurl.h"

namespace zephyrus_adblock {

// Parses EasyList/uBO "cosmetic" element-hiding filters (##selector) and, for a
// given page URL, returns the CSS selectors that should be hidden. Handles
// generic filters (##.ad), domain-scoped filters (site.com##.banner), and
// unhide exceptions (site.com#@#.banner). Procedural selectors (:has, :xpath,
// :style, etc.) are skipped — only plain CSS selectors are emitted.
class AdblockCosmeticEngine {
 public:
  AdblockCosmeticEngine();
  AdblockCosmeticEngine(const AdblockCosmeticEngine&) = delete;
  AdblockCosmeticEngine& operator=(const AdblockCosmeticEngine&) = delete;
  ~AdblockCosmeticEngine();

  // Parses filter-list text; returns the number of cosmetic rules kept.
  // Generic (all-site) selectors are stored but NOT injected by default (mass
  // lists carry ~15k of them — too costly per page). Pass
  // `inject_generic_selectors` = true only for small curated lists (e.g. the
  // starter cookie-banner set) whose generic selectors should apply to every
  // page.
  size_t AddRules(std::string_view filter_list_text,
                  bool inject_generic_selectors = false);

  // Selectors to hide on `url`: generic (minus generic exceptions) plus any
  // scoped to the URL's host or a parent domain (minus their exceptions).
  std::vector<std::string> GetSelectorsForUrl(const GURL& url) const;

  size_t rule_count() const { return rule_count_; }
  size_t generic_count() const { return generic_selectors_.size(); }

 private:
  // Generic (all-site) hide selectors and the exceptions that cancel them.
  std::vector<std::string> generic_selectors_;
  // Curated generic selectors that ARE injected on every page (small; e.g.
  // the top consent-management-platform containers).
  std::vector<std::string> injected_generic_selectors_;
  std::unordered_set<std::string> generic_exceptions_;
  // Per-domain hide selectors, and per-domain unhide exceptions.
  std::unordered_map<std::string, std::vector<std::string>> domain_selectors_;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      domain_exceptions_;

  size_t rule_count_ = 0;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_COSMETIC_ENGINE_H_
