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

// --- $to= -----------------------------------------------------------------

TEST(AdblockFilterEngineTest, ToScopesARuleToTheDestinationHost) {
  // The rule has no host of its own: $to= is what limits it to .example hosts.
  const std::string rule = "/c/*?s1=$doc,to=landing.example\n";
  EXPECT_TRUE(Blocks(rule, "https://landing.example/c/x?s1=1", kTypeDocument));
  EXPECT_TRUE(
      Blocks(rule, "https://sub.landing.example/c/x?s1=1", kTypeDocument))
      << "a subdomain of a listed host is the listed host";
  EXPECT_FALSE(Blocks(rule, "https://other.example/c/x?s1=1", kTypeDocument));
  EXPECT_FALSE(
      Blocks(rule, "https://notlanding.example/c/x?s1=1", kTypeDocument))
      << "only a whole label counts";
}

TEST(AdblockFilterEngineTest, ToCanLeaveHostsOut) {
  const std::string rule = "/c/*?s1=$doc,to=example|~safe.example\n";
  EXPECT_TRUE(Blocks(rule, "https://a.example/c/x?s1=1", kTypeDocument));
  EXPECT_FALSE(Blocks(rule, "https://safe.example/c/x?s1=1", kTypeDocument));
  EXPECT_FALSE(Blocks(rule, "https://www.safe.example/c/x?s1=1", kTypeDocument));
  EXPECT_FALSE(Blocks(rule, "https://a.test/c/x?s1=1", kTypeDocument));
  // Only exclusions: every other host is in.
  const std::string only_out = "/c/*?s1=$doc,to=~safe.example\n";
  EXPECT_TRUE(Blocks(only_out, "https://a.test/c/x?s1=1", kTypeDocument));
  EXPECT_FALSE(Blocks(only_out, "https://safe.example/c/x?s1=1", kTypeDocument));
}

TEST(AdblockFilterEngineTest, ToAndDomainAreBothRequired) {
  // $domain= is the page that asks, $to= is where it asks.
  const std::string rule = "/ad.js$script,domain=news.example,to=cdn.example\n";
  EXPECT_TRUE(Blocks(rule, "https://cdn.example/ad.js"));  // page is news.example
  EXPECT_FALSE(Blocks(rule, "https://other.example/ad.js"));
}

TEST(AdblockFilterEngineTest, ToOnAnExceptionCancelsOnlyThere) {
  const std::string rules =
      "||ads.example^\n@@||ads.example^$to=ok.ads.example\n";
  EXPECT_TRUE(Blocks(rules, "https://ads.example/a.js"));
  EXPECT_FALSE(Blocks(rules, "https://ok.ads.example/a.js"));
}

// --- Long domain lists ----------------------------------------------------
//
// Some rules name a hundred sites. The parser sorts such a list, and a list
// past a handful is searched by host suffix; a short one is searched end to
// end. Both must answer the same.

std::string DomainList(int count, const std::string& extra = std::string()) {
  std::string list;
  for (int i = 0; i < count; ++i) {
    list += "site" + std::to_string(i * 7 % count) + ".example|";
  }
  return list + extra;
}

TEST(AdblockFilterEngineTest, ALongDomainListMatchesPlainAndSubdomains) {
  const std::string rule = "||ads.example^$domain=" + DomainList(40, "last.test");
  const std::string ads = "https://ads.example/x.js";
  AdblockFilterEngine engine;
  engine.AddRules(rule);
  const auto on = [&](const std::string& page) {
    return engine.ShouldBlock(GURL(ads), GURL(page), kTypeScript);
  };
  EXPECT_TRUE(on("https://site0.example/"));
  EXPECT_TRUE(on("https://site39.example/"));
  EXPECT_TRUE(on("https://www.site17.example/a")) << "a subdomain of a listed site";
  EXPECT_TRUE(on("https://deep.er.last.test/"));
  EXPECT_FALSE(on("https://site40.example/")) << "not in the list";
  EXPECT_FALSE(on("https://notsite3.example/")) << "only a whole label counts";
  EXPECT_FALSE(on("https://example/"));
  EXPECT_FALSE(on("https://other.test/"));
}

TEST(AdblockFilterEngineTest, ALongDomainListCanLeaveSitesOut) {
  // The page is on a listed site but also on an excluded one.
  const std::string rule =
      "||ads.example^$domain=" + DomainList(30) + "~skip.site3.example";
  AdblockFilterEngine engine;
  engine.AddRules(rule);
  const GURL ads("https://ads.example/x.js");
  EXPECT_TRUE(engine.ShouldBlock(ads, GURL("https://site3.example/"), kTypeScript));
  EXPECT_FALSE(engine.ShouldBlock(ads, GURL("https://skip.site3.example/"),
                                  kTypeScript));
  EXPECT_FALSE(engine.ShouldBlock(ads, GURL("https://a.skip.site3.example/"),
                                  kTypeScript));
}

TEST(AdblockFilterEngineTest, ALongDomainListKeepsItsEntityWildcards) {
  // `google.*` is the same name under any public suffix; it has to survive the
  // sort and still be found when the list is searched by suffix.
  const std::string rule =
      "||ads.example^$domain=" + DomainList(30, "mapsite.*");
  AdblockFilterEngine engine;
  engine.AddRules(rule);
  const GURL ads("https://ads.example/x.js");
  EXPECT_TRUE(engine.ShouldBlock(ads, GURL("https://mapsite.com/"), kTypeScript));
  EXPECT_TRUE(
      engine.ShouldBlock(ads, GURL("https://www.mapsite.co.uk/"), kTypeScript));
  EXPECT_TRUE(engine.ShouldBlock(ads, GURL("https://site9.example/"), kTypeScript));
  EXPECT_FALSE(engine.ShouldBlock(ads, GURL("https://mapsites.com/"), kTypeScript));
}

TEST(AdblockFilterEngineTest, ALongToListScopesTheDestination) {
  const std::string rule = "/c/*?s1=$doc,to=" + DomainList(25, "landing.test");
  AdblockFilterEngine engine;
  engine.AddRules(rule);
  const GURL page(kPage);
  EXPECT_TRUE(engine.ShouldBlock(GURL("https://site4.example/c/x?s1=1"), page,
                                 kTypeDocument));
  EXPECT_TRUE(engine.ShouldBlock(GURL("https://a.landing.test/c/x?s1=1"), page,
                                 kTypeDocument));
  EXPECT_FALSE(engine.ShouldBlock(GURL("https://elsewhere.test/c/x?s1=1"), page,
                                  kTypeDocument));
}

// --- Lines that are not network rules ------------------------------------

TEST(AdblockFilterEngineTest, EveryCosmeticSpellingIsKeptOutOfTheNetworkIndex) {
  // All ten separators. Six of them used to be admitted as network rules.
  for (const char* line : {"site.example##.ad", "site.example#@#.ad",
                           "site.example#?#div:has-text(Ad)",
                           "site.example#@?#div:has-text(Ad)",
                           "site.example#$#body { overflow: auto }",
                           "site.example#@$#body { overflow: auto }",
                           "site.example#$?#div:has(a) { display: none }",
                           "site.example#@$?#div:has(a) { display: none }",
                           "site.example#%#//scriptlet('abort-on-property-read', 'x')",
                           "site.example#@%#//scriptlet('abort-on-property-read', 'x')"}) {
    AdblockFilterEngine engine;
    EXPECT_EQ(engine.AddRules(line), 0u) << line;
    EXPECT_EQ(engine.block_rule_count() + engine.exception_rule_count(), 0u)
        << line;
  }
}

TEST(AdblockFilterEngineTest, AHashInAnOrdinaryNetworkRuleIsStillANetworkRule) {
  // A `#` that does not form a separator belongs to the pattern.
  AdblockFilterEngine engine;
  EXPECT_EQ(engine.AddRules("||tracker.example/#pixel\n||x.example/a#b"), 2u);
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

TEST(AdblockFilterEngineTest, RegexRulesAreKeptApartFromThePatternRules) {
  // A regex used to be stored as a literal pattern when options followed it: it
  // never matched, and it still cost every request it was tried against. Now
  // it is parsed as a regex and none of the keyword buckets ever holds it.
  AdblockFilterEngine engine;
  EXPECT_EQ(1u, engine.AddRules("/ad[0-9]+\\.js/$script"));
  EXPECT_EQ(1u, engine.AddRules("/ad[0-9]+\\.js/"));
  EXPECT_EQ(0u, engine.block_rule_count());
  EXPECT_EQ(2u, engine.regex_rules().size());
  EXPECT_FALSE(engine.ShouldBlock(GURL("https://cdn.example/ad7.js"),
                                  GURL(kPage), kTypeScript))
      << "the engine alone does not run regexes; AdblockRegexMatcher does";
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
