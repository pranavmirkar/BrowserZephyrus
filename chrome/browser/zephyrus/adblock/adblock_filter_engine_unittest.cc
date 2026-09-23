// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"

#include <string>
#include <vector>

#include "chrome/browser/zephyrus/adblock/adblock_cosmetic_engine.h"

#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_adblock {
namespace {

constexpr char kPage[] = "https://news.example/article";

bool Blocks(const std::string& rules,
            const std::string& url,
            ResourceType type = kTypeScript) {
  AdblockFilterEngine engine;
  engine.AddRules(rules);
  return engine.ShouldBlock(GURL(url), GURL(kPage), type);
}

// --- Keyword choice -------------------------------------------------------
//
// A rule is filed under one keyword and looked at only for a URL containing
// that keyword as a WHOLE token (a run of letters and digits). A keyword taken
// from a run the pattern does not bound -- one that touches a `*`, or the
// pattern's own unanchored end -- can be the START of a longer token in a URL
// the rule matches, and then the rule is never even tried. MEASURED: 412 rules
// in the shipped list were filed that way.

TEST(AdblockFilterEngineTest, KeywordTouchingAWildcardDoesNotHideTheRule) {
  // Filed under "funcript", but the URL's token is "funcript123".
  EXPECT_TRUE(Blocks("/funcript*.php?pub=",
                     "https://cdn.example/funcript123.php?pub=7"));
}

TEST(AdblockFilterEngineTest, KeywordAtAnUnanchoredEndDoesNotHideTheRule) {
  // Filed under "banner", but the URL's token is "banners".
  EXPECT_TRUE(Blocks("/banner", "https://cdn.example/banners/top.png",
                     kTypeImage));
}

TEST(AdblockFilterEngineTest, BoundedKeywordsStillMatch) {
  EXPECT_TRUE(Blocks("||ads.example.com^", "https://ads.example.com/x.js"));
  EXPECT_TRUE(Blocks("||tracker.example^", "https://tracker.example/p"));
  // Not "/adserver/": a pattern that starts and ends with a slash is a
  // regex in ABP syntax, and those are skipped.
  EXPECT_TRUE(Blocks("/adserver/*", "https://cdn.example/adserver/a.js"));
}

TEST(AdblockFilterEngineTest, HostnameAnchorStillRespectsLabels) {
  EXPECT_FALSE(Blocks("||ads.example.com^",
                      "https://notads.example.com.evil/x.js"));
  EXPECT_TRUE(Blocks("||ads.example.com^", "https://cdn.ads.example.com/x.js"));
}

TEST(AdblockFilterEngineTest, ExceptionsStillUnblock) {
  EXPECT_FALSE(Blocks("||ads.example.com^\n@@||ads.example.com/ok.js",
                      "https://ads.example.com/ok.js"));
  EXPECT_TRUE(Blocks("||ads.example.com^\n@@||ads.example.com/ok.js",
                     "https://ads.example.com/other.js"));
}

TEST(AdblockFilterEngineTest, ScopingStillApplies) {
  EXPECT_FALSE(Blocks("||ads.example.com^$image", "https://ads.example.com/a.js",
                      kTypeScript));
  EXPECT_FALSE(
      Blocks("||news.example^$third-party", "https://news.example/own.js"));
}

// --- Regex rules ----------------------------------------------------------

TEST(AdblockFilterEngineTest, RegexRulesWithOptionsAreSkippedNotMisread) {
  // Unsupported, and used to be caught only when the line ENDED in '/'. With
  // options after it the regex source was stored as a literal pattern: it never
  // matched, and it still cost every request it was tried against.
  AdblockFilterEngine engine;
  EXPECT_EQ(0u, engine.AddRules("/ad[0-9]+\\.js/$script"));
  EXPECT_EQ(0u, engine.AddRules("/ad[0-9]+\\.js/"));
  EXPECT_EQ(0u, engine.block_rule_count());
}

// --- Bounded work ---------------------------------------------------------

TEST(AdblockFilterEngineTest, PathologicalPatternIsBoundedPerRule) {
  // Five wildcards (the most the parser admits), no usable keyword, against a
  // long URL of the repeated letter: the backtracking worst case. The answer
  // does not matter; how long it takes does. MEASURED before the budget became
  // one per rule rather than one per URL position: 721 ms for this one
  // request, on the UI thread. The bound here is loose on purpose -- it
  // separates "bounded" from "one budget per character", not fast from slow.
  const std::string url = "https://x.example/" + std::string(1500, 'a');
  const base::TimeTicks start = base::TimeTicks::Now();
  EXPECT_FALSE(Blocks("a*a*a*a*a*b", url));
  EXPECT_LT(base::TimeTicks::Now() - start, base::Milliseconds(100));
}

// --- Cosmetic filters are CSS written by the network ---------------------
//
// The hiding sheet is injected at USER origin, which the page cannot override,
// so a filter that escapes its own rule writes CSS nothing on the page can
// undo. These are the escapes; see IsSafeFilterCss.

std::vector<std::string> Selectors(const std::string& rules) {
  AdblockCosmeticEngine engine;
  engine.AddRules(rules);
  return engine.GetSelectorsForUrl(GURL("https://site.example/"));
}

std::vector<std::string> StyleRules(const std::string& rules) {
  AdblockCosmeticEngine engine;
  engine.AddRules(rules);
  return engine.GetStyleRulesForUrl(GURL("https://site.example/"));
}

TEST(AdblockCosmeticEngineTest, OrdinarySelectorsAreKept) {
  EXPECT_EQ(Selectors("site.example##.ad-slot"),
            std::vector<std::string>{".ad-slot"});
  EXPECT_EQ(StyleRules("site.example##body:style(overflow: auto !important)"),
            std::vector<std::string>{"body{overflow: auto !important}"});
}

TEST(AdblockCosmeticEngineTest, SelectorThatClosesItsRuleIsDropped) {
  EXPECT_TRUE(
      Selectors("site.example##x{} *{background:url(https://t.example/)}")
          .empty());
  EXPECT_TRUE(Selectors("site.example##x /* rest of sheet").empty());
}

TEST(AdblockCosmeticEngineTest, StyleThatLoadsAnythingIsDropped) {
  EXPECT_TRUE(
      StyleRules("site.example##div:style(background: url(https://t.example/))")
          .empty());
  // A CSS escape spells url( without the letters.
  EXPECT_TRUE(
      StyleRules("site.example##div:style(background: u\\72l(https://t/))")
          .empty());
  EXPECT_TRUE(StyleRules("site.example##div:style(color: red} *{x: y)").empty());
  // And an unsafe :style() is not demoted into a HIDE rule instead.
  EXPECT_TRUE(
      Selectors("site.example##div:style(background: url(https://t/))")
          .empty());
}

}  // namespace
}  // namespace zephyrus_adblock
