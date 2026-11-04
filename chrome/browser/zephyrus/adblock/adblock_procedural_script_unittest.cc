// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_procedural_script.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/environment.h"
#include "chrome/browser/zephyrus/adblock/adblock_cosmetic_engine.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_adblock {
namespace {

TEST(ProceduralCosmeticTest, ParsesTheExtendedSeparatorsAndKeepsThemOutOfCss) {
  AdblockCosmeticEngine engine;
  engine.AddRules(
      "example.com#?#div:has-text(Sponsored)\n"
      "example.com##.plain\n"
      "example.com##div:upward(2)\n"
      "example.com#?#section:has(> .ad)\n");
  const GURL url("https://example.com/");

  // The procedural ones, in order; the native :has() one is plain CSS.
  const std::vector<std::string> procedural =
      engine.GetProceduralSelectorsForUrl(url);
  ASSERT_EQ(procedural.size(), 2u);
  EXPECT_EQ(procedural[0], "div:has-text(Sponsored)");
  EXPECT_EQ(procedural[1], "div:upward(2)");

  const std::vector<std::string> css = engine.GetSelectorsForUrl(url);
  EXPECT_NE(std::find(css.begin(), css.end(), ".plain"), css.end());
  EXPECT_NE(std::find(css.begin(), css.end(), "section:has(> .ad)"), css.end());
  for (const std::string& s : css) {
    EXPECT_EQ(s.find(":has-text("), std::string::npos) << s;
    EXPECT_EQ(s.find(":upward("), std::string::npos) << s;
  }
}

TEST(ProceduralCosmeticTest, OnlyTheMatchingSiteGetsIt) {
  AdblockCosmeticEngine engine;
  engine.AddRules("example.com#?#div:has-text(Ad)\n");
  EXPECT_EQ(engine.GetProceduralSelectorsForUrl(GURL("https://example.com/"))
                .size(),
            1u);
  EXPECT_EQ(engine.GetProceduralSelectorsForUrl(GURL("https://sub.example.com/"))
                .size(),
            1u);
  EXPECT_TRUE(engine.GetProceduralSelectorsForUrl(GURL("https://other.org/"))
                  .empty());
  EXPECT_TRUE(engine.GetProceduralSelectorsForUrl(GURL("ftp://example.com/"))
                  .empty());
}

TEST(ProceduralCosmeticTest, ExceptionsAndCarveOutsApply) {
  AdblockCosmeticEngine engine;
  engine.AddRules(
      "example.com#?#div:has-text(Ad)\n"
      "shop.example.com#@?#div:has-text(Ad)\n"
      "example.com,~news.example.com#?#p:xpath(..)\n");
  EXPECT_EQ(engine.GetProceduralSelectorsForUrl(GURL("https://example.com/"))
                .size(),
            2u);
  // shop: the first is excepted, the second still applies.
  const std::vector<std::string> shop =
      engine.GetProceduralSelectorsForUrl(GURL("https://shop.example.com/"));
  ASSERT_EQ(shop.size(), 1u);
  EXPECT_EQ(shop[0], "p:xpath(..)");
  // news: the carve-out removes the second.
  const std::vector<std::string> news =
      engine.GetProceduralSelectorsForUrl(GURL("https://news.example.com/"));
  ASSERT_EQ(news.size(), 1u);
  EXPECT_EQ(news[0], "div:has-text(Ad)");
}

TEST(ProceduralCosmeticTest, HtmlFilteringIsNotAnElementRule) {
  AdblockCosmeticEngine engine;
  engine.AddRules(
      "example.com##^script:has-text(ads)\n"
      "example.com##^responseheader(x)\n");
  EXPECT_TRUE(engine.GetProceduralSelectorsForUrl(GURL("https://example.com/"))
                  .empty());
  EXPECT_TRUE(engine.GetSelectorsForUrl(GURL("https://example.com/")).empty());
}

TEST(ProceduralCosmeticTest, LimitsWhatOneListCanCost) {
  AdblockCosmeticEngine engine;
  std::string list;
  for (int i = 0; i < 400; ++i) {
    list += "example.com#?#div:has-text(x" + std::to_string(i) + ")\n";
  }
  list += "example.com#?#div:has-text(" + std::string(5000, 'a') + ")\n";
  engine.AddRules(list);
  // 200 per site, and the oversize selector is refused outright.
  EXPECT_EQ(engine.GetProceduralSelectorsForUrl(GURL("https://example.com/"))
                .size(),
            200u);
}

TEST(ProceduralCosmeticTest, TheScriptCarriesTheSelectorsAsDataOnly) {
  EXPECT_TRUE(BuildProceduralScript({}).empty());
  const std::string script = BuildProceduralScript(
      {R"j(div:has-text("quote"))j", "p:upward(1)", "</script>:xpath(..)"});
  // The rule text is a JSON string literal, quotes escaped.
  EXPECT_NE(script.find(R"j(["div:has-text(\"quote\")","p:upward(1)",)j"),
            std::string::npos)
      << script.substr(script.size() - 200);
  // No way to evaluate list text as code.
  const std::string_view runner = ProceduralRunnerSource();
  EXPECT_EQ(runner.find("eval("), std::string_view::npos);
  EXPECT_EQ(runner.find("new Function"), std::string_view::npos);
  EXPECT_EQ(runner.find("innerHTML"), std::string_view::npos);
  EXPECT_EQ(runner.find("document.write"), std::string_view::npos);
  EXPECT_TRUE(script.ends_with(");\n"));
}


// Env-gated: classifies every line of ZEPHYRUS_PROC_LINES ("id<TAB>rule") the way
// this engine reads it, one letter per line to ZEPHYRUS_PROC_OUT: P stored as
// procedural, C stored as plain CSS, - not kept. Used to set the coverage next to
// Brave's parser (research/brave_compare, bin parity).
TEST(ProceduralCosmeticTest, RealListCoverage) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  const std::optional<std::string> in = env->GetVar("ZEPHYRUS_PROC_LINES");
  const std::optional<std::string> out = env->GetVar("ZEPHYRUS_PROC_OUT");
  if (!in || !out) {
    GTEST_SKIP() << "set ZEPHYRUS_PROC_LINES and ZEPHYRUS_PROC_OUT";
  }
  std::ifstream file(*in);
  std::ofstream result(*out);
  std::string line;
  while (std::getline(file, line)) {
    const size_t tab = line.find(static_cast<char>(9));
    const std::string rule =
        tab == std::string::npos ? line : line.substr(1 + tab);
    AdblockCosmeticEngine engine;
    engine.AddRules(rule);
    result << (engine.procedural_count() ? 'P'
               : engine.rule_count()     ? 'C'
                                         : '-')
           << static_cast<char>(10);
  }
}

}  // namespace
}  // namespace zephyrus_adblock
