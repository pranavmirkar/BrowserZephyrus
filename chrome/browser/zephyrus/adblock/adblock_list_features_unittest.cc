// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The filter-syntax features that were parsed as "unsupported" (or silently
// never matched) and let ads through: $document, $important, $badfilter,
// $denyallow, $redirect, $generichide, entity domains, ~domain carve-outs,
// `!#if` blocks, `!#include`, and the scriptlet names the lists use most.

#include <string>
#include <vector>

#include "base/strings/strcat.h"

#include "chrome/browser/zephyrus/adblock/adblock_cosmetic_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_scriptlet_engine.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_adblock {
namespace {

bool Contains(const std::vector<std::string>& v, const std::string& s) {
  for (const std::string& x : v) {
    if (x == s) {
      return true;
    }
  }
  return false;
}

// --- Network options ------------------------------------------------------

TEST(AdblockListFeaturesTest, DocumentRuleBlocksThePageItself) {
  AdblockFilterEngine engine;
  engine.AddRules("/zclkredirect?$document");
  EXPECT_TRUE(engine.ShouldBlock(GURL("https://x.example/zclkredirect?u=1"),
                                 GURL(), kTypeDocument));
  // ...and only the page: the same URL as a script is not its business.
  EXPECT_FALSE(engine.ShouldBlock(GURL("https://x.example/zclkredirect?u=1"),
                                  GURL(), kTypeScript));
}

TEST(AdblockListFeaturesTest, UntypedPathRuleNeverBlocksAPage) {
  AdblockFilterEngine engine;
  engine.AddRules("/ads/*\n||adhost.example^");
  // An article whose URL happens to contain "/ads/" still opens...
  EXPECT_FALSE(engine.ShouldBlock(GURL("https://news.example/ads/policy"),
                                  GURL(), kTypeDocument));
  EXPECT_FALSE(engine.ShouldBlock(GURL("https://news.example/ads/policy"),
                                  GURL(), kTypePopup));
  // ...while the rule still blocks subresources, and a pure host rule still
  // blocks the page and pop-ups.
  EXPECT_TRUE(engine.ShouldBlock(GURL("https://news.example/ads/a.js"),
                                 GURL("https://news.example/"), kTypeScript));
  EXPECT_TRUE(engine.ShouldBlock(GURL("https://adhost.example/land"), GURL(),
                                 kTypeDocument));
  EXPECT_TRUE(engine.ShouldBlock(GURL("https://adhost.example/land"), GURL(),
                                 kTypePopup));
}

TEST(AdblockListFeaturesTest, DocumentExceptionAllowlistsThePage) {
  AdblockFilterEngine engine;
  engine.AddRules("||tracker.example^\n@@||optout.example^$document");
  EXPECT_FALSE(engine.ShouldBlock(GURL("https://tracker.example/t.js"),
                                  GURL("https://optout.example/"),
                                  kTypeScript));
  EXPECT_TRUE(engine.ShouldBlock(GURL("https://tracker.example/t.js"),
                                 GURL("https://news.example/"), kTypeScript));
  EXPECT_EQ(engine.GetDocumentExceptions(GURL("https://optout.example/a")),
            static_cast<uint32_t>(kExceptDocument));
}

TEST(AdblockListFeaturesTest, GenericHideIsReportedForThePage) {
  AdblockFilterEngine engine;
  engine.AddRules("@@||tvtoday.example^$ghide\n@@||x.example^$specifichide");
  EXPECT_EQ(engine.GetDocumentExceptions(GURL("https://www.tvtoday.example/")),
            static_cast<uint32_t>(kExceptGenericHide));
  EXPECT_EQ(engine.GetDocumentExceptions(GURL("https://x.example/")),
            static_cast<uint32_t>(kExceptSpecificHide));
  EXPECT_EQ(engine.GetDocumentExceptions(GURL("https://other.example/")), 0u);
}

TEST(AdblockListFeaturesTest, ImportantBeatsAnException) {
  AdblockFilterEngine engine;
  engine.AddRules(
      "||ads.example^$important\n@@||ads.example^\n"
      "||plain.example^\n@@||plain.example^");
  EXPECT_TRUE(engine.ShouldBlock(GURL("https://ads.example/a.js"),
                                 GURL("https://news.example/"), kTypeScript));
  EXPECT_FALSE(engine.ShouldBlock(GURL("https://plain.example/a.js"),
                                  GURL("https://news.example/"), kTypeScript));
}

TEST(AdblockListFeaturesTest, BadfilterCancelsItsTwinEvenAboveIt) {
  AdblockFilterEngine engine;
  engine.AddRules(
      "||cdn.example^$script,domain=news.example\n"
      "||cdn.example^$script,domain=news.example,badfilter");
  EXPECT_FALSE(engine.ShouldBlock(GURL("https://cdn.example/lib.js"),
                                  GURL("https://news.example/"), kTypeScript));
}

TEST(AdblockListFeaturesTest, DenyallowSparesTheListedHosts) {
  AdblockFilterEngine engine;
  engine.AddRules("*$script,3p,denyallow=googleapis.com,domain=warez.example");
  EXPECT_TRUE(engine.ShouldBlock(GURL("https://popunder.example/p.js"),
                                 GURL("https://warez.example/"), kTypeScript));
  EXPECT_FALSE(engine.ShouldBlock(GURL("https://ajax.googleapis.com/jq.js"),
                                  GURL("https://warez.example/"), kTypeScript));
}

TEST(AdblockListFeaturesTest, EmptyRedirectBlocksSurrogateRedirectDoesNot) {
  AdblockFilterEngine engine;
  engine.AddRules(
      "||stats.example/a.js$script,redirect=noopjs:10\n"
      "||imasdk.example/ima3.js$script,redirect=google-ima.js");
  EXPECT_TRUE(engine.ShouldBlock(GURL("https://stats.example/a.js"),
                                 GURL("https://news.example/"), kTypeScript));
  // Blocking the IMA SDK without its surrogate would break the player.
  EXPECT_FALSE(engine.ShouldBlock(GURL("https://imasdk.example/ima3.js"),
                                  GURL("https://news.example/"), kTypeScript));
}

TEST(AdblockListFeaturesTest, EntityDomainOptionMatchesAnySuffix) {
  AdblockFilterEngine engine;
  engine.AddRules("||cdn.example^$script,domain=google.*");
  EXPECT_TRUE(engine.ShouldBlock(GURL("https://cdn.example/x.js"),
                                 GURL("https://www.google.co.in/"),
                                 kTypeScript));
  EXPECT_FALSE(engine.ShouldBlock(GURL("https://cdn.example/x.js"),
                                  GURL("https://google.example.com/"),
                                  kTypeScript));
}

TEST(AdblockListFeaturesTest, PopunderAndAllOptions) {
  AdblockFilterEngine engine;
  engine.AddRules("*$popunder,domain=warez.example\n||scam.example^$all");
  EXPECT_TRUE(engine.ShouldBlock(GURL("https://any.example/x"),
                                 GURL("https://warez.example/"), kTypePopup));
  EXPECT_TRUE(engine.ShouldBlock(GURL("https://scam.example/win"), GURL(),
                                 kTypeDocument));
}

// --- Domains ---------------------------------------------------------------

TEST(AdblockListFeaturesTest, DomainLookupKeysIncludeEntityForms) {
  const std::vector<std::string> keys = DomainLookupKeys("m.news.google.co.in");
  EXPECT_TRUE(Contains(keys, "m.news.google.co.in"));
  EXPECT_TRUE(Contains(keys, "google.co.in"));
  EXPECT_TRUE(Contains(keys, "google.*"));
  EXPECT_TRUE(Contains(keys, "news.google.*"));
  EXPECT_FALSE(Contains(keys, "co.*"));
  EXPECT_TRUE(HostMatchesFilterDomain("www.google.de", "google.*"));
  EXPECT_FALSE(HostMatchesFilterDomain("notgoogle.de", "google.*"));
  EXPECT_FALSE(HostMatchesFilterDomain("192.168.0.1", "168.*"));
}

TEST(AdblockListFeaturesTest, CosmeticEntityAndCarveOuts) {
  AdblockCosmeticEngine engine;
  engine.AddRules(
      "yts.*##.ad-slot\n"
      "shop.example,~cart.shop.example##.promo\n"
      "~quiet.example##.nag\n"
      "##div[data-ad-unit]");
  EXPECT_TRUE(Contains(engine.GetSelectorsForUrl(GURL("https://yts.mx/")),
                       ".ad-slot"));
  EXPECT_TRUE(Contains(
      engine.GetSelectorsForUrl(GURL("https://www.shop.example/")), ".promo"));
  EXPECT_FALSE(Contains(
      engine.GetSelectorsForUrl(GURL("https://cart.shop.example/")), ".promo"));
  // "~quiet.example##.nag" is generic everywhere else.
  EXPECT_TRUE(Contains(
      engine.GetGenericSelectorsForTokens(GURL("https://a.example/"), {".nag"}),
      ".nag"));
  EXPECT_FALSE(Contains(engine.GetGenericSelectorsForTokens(
                            GURL("https://quiet.example/"), {".nag"}),
                        ".nag"));
  // A keyless generic selector goes out with the document-start sheet...
  EXPECT_TRUE(Contains(engine.GetSelectorsForUrl(GURL("https://a.example/")),
                       "div[data-ad-unit]"));
  // ...unless the page has $generichide.
  EXPECT_FALSE(Contains(engine.GetSelectorsForUrl(GURL("https://a.example/"),
                                                  /*include_generic=*/false),
                        "div[data-ad-unit]"));
}

TEST(AdblockListFeaturesTest, UnbalancedSelectorsAreRejected) {
  AdblockCosmeticEngine engine;
  engine.AddRules(
      "site.example##div:not(.a\n"
      "site.example##a[href=\"x\n"
      "site.example##.ok:not(.a)\n");
  const std::vector<std::string> selectors =
      engine.GetSelectorsForUrl(GURL("https://site.example/"));
  EXPECT_EQ(selectors, std::vector<std::string>{".ok:not(.a)"});
}

// --- Preprocessor and includes --------------------------------------------

TEST(AdblockListFeaturesTest, ConditionsFollowUboSemantics) {
  EXPECT_TRUE(EvaluateListCondition("env_chromium"));
  EXPECT_FALSE(EvaluateListCondition("env_firefox"));
  EXPECT_TRUE(EvaluateListCondition("!cap_html_filtering"));
  EXPECT_TRUE(EvaluateListCondition("env_chromium && !env_mobile"));
  EXPECT_TRUE(EvaluateListCondition("env_safari || (ext_ublock && !ext_ubol)"));
  EXPECT_FALSE(EvaluateListCondition("unknown_token"));
  EXPECT_FALSE(EvaluateListCondition("(env_chromium"));
  EXPECT_FALSE(EvaluateListCondition(std::string(200, '(')));
}

TEST(AdblockListFeaturesTest, PreprocessorKeepsOnlyTheLiveBranch) {
  const std::string out = PreprocessFilterList(
      "a\n"
      "!#if env_firefox\nfirefox-only\n!#else\nchromium-else\n!#endif\n"
      "!#if env_chromium\n!#if env_mobile\nmobile\n!#endif\nchromium\n!#endif\n"
      "b\n");
  EXPECT_EQ(out, "a\nchromium-else\nchromium\nb\n");
}

TEST(AdblockListFeaturesTest, IncludesAreBareSameDirectoryNames) {
  const std::vector<std::string> names = FindListIncludes(
      "!#include filters-2025.txt\n"
      "!#include ../secret.txt\n"
      "!#include https://evil.example/x.txt\n"
      "!#include sub/dir.txt\n"
      "!#include notes.md\n"
      "!#include ubo-link-shorteners.txt\n");
  ASSERT_EQ(names.size(), 2u);
  EXPECT_EQ(names[0], "filters-2025.txt");
  EXPECT_EQ(names[1], "ubo-link-shorteners.txt");
}

// --- The combined file the updater writes ---------------------------------

TEST(AdblockListFeaturesTest, CombinedHeaderRoundTrips) {
  const CombinedListHeader header =
      ParseCombinedListHeader(CombinedListHeaderText(1790000000));
  EXPECT_EQ(header.format, kCombinedListFormat);
  EXPECT_EQ(header.full_update_seconds, 1790000000);
}

TEST(AdblockListFeaturesTest, FileFromAnOlderUpdaterHasNoFormat) {
  // What every build before format 2 wrote: the service must treat it as due.
  const CombinedListHeader header = ParseCombinedListHeader(
      "! Zephyrus combined filter lists (auto-updated). Do not edit.\n"
      "\n! ===== https://easylist.to/easylist/easylist.txt =====\n"
      "! Zephyrus-Format: 2\n");
  EXPECT_EQ(header.format, 0) << "a header line inside a list must not count";
  EXPECT_EQ(header.full_update_seconds, 0);
}

TEST(AdblockListFeaturesTest, SectionsAreFoundByUrl) {
  const std::string combined =
      CombinedListHeaderText(1) + "\n" + ListSectionMarker("https://a/1.txt") +
      "\n||one^\n||uno^\n\n" + ListSectionMarker("https://a/2.txt") +
      "\n||two^\n\n" + ListSectionMarker("https://a/3.txt") + "\n\n";
  EXPECT_EQ(FindListSection(combined, "https://a/1.txt"),
            std::optional<std::string_view>("||one^\n||uno^\n"));
  EXPECT_EQ(FindListSection(combined, "https://a/2.txt"),
            std::optional<std::string_view>("||two^\n"));
  // An empty section is no copy at all: carrying it forward would keep
  // nothing while claiming to keep the list.
  EXPECT_EQ(FindListSection(combined, "https://a/3.txt"), std::nullopt);
  EXPECT_EQ(FindListSection(combined, "https://a/4.txt"), std::nullopt);
  // A URL that is a prefix of another must not match it.
  EXPECT_EQ(FindListSection(combined, "https://a/1.tx"), std::nullopt);
}

// --- Scriptlets -------------------------------------------------------------

TEST(AdblockListFeaturesTest, ScriptletsResolveEntityAndCarveOuts) {
  AdblockScriptletEngine engine;
  engine.AddRules(
      "yts.*##+js(nowoif)\n"
      "news.example,~live.news.example##+js(acs, document.write, adblock)\n");
  EXPECT_NE(engine.BuildInjectionScriptForUrl(GURL("https://yts.mx/"))
                .find("\"nowoif\""),
            std::string::npos);
  EXPECT_NE(engine.BuildInjectionScriptForUrl(GURL("https://news.example/"))
                .find("\"acs\""),
            std::string::npos);
  EXPECT_TRUE(
      engine.BuildInjectionScriptForUrl(GURL("https://live.news.example/"))
          .empty());
}

TEST(AdblockListFeaturesTest, BareScriptletExceptionDisablesAll) {
  AdblockScriptletEngine engine;
  engine.AddRules("example.com##+js(nostif, ad)\nexample.com#@#+js()\n");
  EXPECT_TRUE(
      engine.BuildInjectionScriptForUrl(GURL("https://example.com/")).empty());
}

TEST(AdblockListFeaturesTest, DuplicateInvocationsAreEmittedOnce) {
  AdblockScriptletEngine engine;
  engine.AddRules("site.example,site.*##+js(nostif, adblock)\n");
  const std::string script =
      engine.BuildInjectionScriptForUrl(GURL("https://site.example/"));
  const size_t call = script.find("zephyrusScriptlets[\"nostif\"]");
  ASSERT_NE(call, std::string::npos);
  EXPECT_EQ(script.find("zephyrusScriptlets[\"nostif\"]", call + 1),
            std::string::npos);
}

// Z-04. A third-party list (here EasyList) must not be able to rewrite
// responses on a site it names; uBO's own list may.
TEST(AdblockListFeaturesTest, TrustedScriptletsOnlyFromUboLists) {
  const std::string rule =
      "bank.example##+js(trusted-replace-xhr-response, real, attacker)\n";
  const std::string combined = base::StrCat(
      {"! Zephyrus combined filter list\n",
       ListSectionMarker("https://easylist.to/easylist/easylist.txt"), "\n",
       rule,
       ListSectionMarker("https://raw.githubusercontent.com/uBlockOrigin/"
                         "uAssets/master/filters/quick-fixes.txt"),
       "\n", "shop.example##+js(trusted-set-cookie, consent, yes)\n"});
  AdblockScriptletEngine engine;
  engine.AddRules(combined, /*trust_unsectioned=*/false);
  EXPECT_EQ(engine.untrusted_scriptlets_dropped(), 1u);
  EXPECT_TRUE(
      engine.BuildInjectionScriptForUrl(GURL("https://bank.example/")).empty());
  EXPECT_NE(engine.BuildInjectionScriptForUrl(GURL("https://shop.example/"))
                .find("trusted-set-cookie"),
            std::string::npos);
}

// Unmarked text in a combined list is third-party, and a ".js" suffix or a
// look-alike repository does not launder a trusted scriptlet.
TEST(AdblockListFeaturesTest, TrustedScriptletGateHasNoBypasses) {
  AdblockScriptletEngine engine;
  engine.AddRules(
      base::StrCat(
          {"a.example##+js(trusted-set-cookie.js, k, v)\n",
           ListSectionMarker("https://raw.githubusercontent.com/uBlockOrigin/"
                             "uAssets-mirror/filters.txt"),
           "\n", "b.example##+js(trusted-click-element, #buy)\n",
           // Ordinary scriptlets from a third-party list are unaffected.
           "c.example##+js(set-constant, adsOk, true)\n"}),
      /*trust_unsectioned=*/false);
  EXPECT_EQ(engine.untrusted_scriptlets_dropped(), 2u);
  EXPECT_TRUE(
      engine.BuildInjectionScriptForUrl(GURL("https://a.example/")).empty());
  EXPECT_TRUE(
      engine.BuildInjectionScriptForUrl(GURL("https://b.example/")).empty());
  EXPECT_FALSE(
      engine.BuildInjectionScriptForUrl(GURL("https://c.example/")).empty());
}

// The browser's own built-in rules stay trusted (the YouTube rules need it).
TEST(AdblockListFeaturesTest, BuiltInRulesKeepTrustedScriptlets) {
  AdblockScriptletEngine engine;
  engine.AddRules("www.youtube.com##+js(trusted-prevent-dom-bypass, x, y)\n");
  EXPECT_EQ(engine.untrusted_scriptlets_dropped(), 0u);
  EXPECT_FALSE(
      engine.BuildInjectionScriptForUrl(GURL("https://www.youtube.com/"))
          .empty());
}

TEST(AdblockListFeaturesTest, JsSuffixedNamesResolve) {
  AdblockScriptletEngine engine;
  engine.AddRules("site.example##+js(set-constant.js, adsOk, true)\n");
  EXPECT_NE(engine.BuildInjectionScriptForUrl(GURL("https://site.example/"))
                .find("zephyrusScriptlets[\"set-constant\"]"),
            std::string::npos);
}

}  // namespace
}  // namespace zephyrus_adblock
