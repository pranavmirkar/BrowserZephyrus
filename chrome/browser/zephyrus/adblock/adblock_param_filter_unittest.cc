// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_param_filter.h"

#include <optional>
#include <string>

#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_adblock {
namespace {

GURL Page() {
  return GURL("https://news.example.org/");
}

std::string Stripped(const AdblockParamFilter& filter,
                     const std::string& url,
                     const GURL& initiator = GURL(),
                     ResourceType type = kTypeDocument) {
  const std::optional<GURL> out = filter.Strip(GURL(url), initiator, type);
  return out ? out->spec() : "(unchanged)";
}

TEST(AdblockParamFilterTest, TheBuiltInSetRemovesTrackersAndKeepsState) {
  AdblockParamFilter filter;
  filter.AddBuiltInRules();
  EXPECT_EQ(Stripped(filter,
                     "https://shop.example/item?id=7&utm_source=mail&"
                     "utm_campaign=spring&fbclid=AbC123&color=red"),
            "https://shop.example/item?id=7&color=red");
  EXPECT_EQ(Stripped(filter, "https://shop.example/?gclid=x&msclkid=y"),
            "https://shop.example/")
      << "an emptied query leaves no '?'";
  EXPECT_EQ(Stripped(filter, "https://shop.example/a?utm_source=x#section-2"),
            "https://shop.example/a#section-2")
      << "the fragment is not a parameter and stays";
}

TEST(AdblockParamFilterTest, ParametersThatCarryStateAreLeftAlone) {
  AdblockParamFilter filter;
  filter.AddBuiltInRules();
  for (const char* url : {"https://a.example/?id=7", "https://a.example/?q=cats&page=2",
                          "https://a.example/login?code=abc&state=xyz",
                          "https://a.example/?ref=home&source=nav&utm=notatracker",
                          "https://a.example/plain", "https://a.example/?"}) {
    EXPECT_EQ(Stripped(filter, url), "(unchanged)") << url;
  }
}

TEST(AdblockParamFilterTest, OtherPartsOfTheQueryKeepTheirExactSpelling) {
  AdblockParamFilter filter;
  filter.AddBuiltInRules();
  EXPECT_EQ(Stripped(filter,
                     "https://a.example/s?q=hello%20world&fbclid=1&x=%E2%9C%93&y"),
            "https://a.example/s?q=hello%20world&x=%E2%9C%93&y");
  EXPECT_EQ(Stripped(filter, "https://a.example/?utm%5Fsource=1&k=v"),
            "https://a.example/?k=v")
      << "a percent-encoded name is the same name";
}

TEST(AdblockParamFilterTest, StrippingTwiceChangesNothingMore) {
  AdblockParamFilter filter;
  filter.AddBuiltInRules();
  const GURL once = *filter.Strip(
      GURL("https://a.example/?a=1&gclid=2&utm_source=3"), GURL(), kTypeDocument);
  EXPECT_FALSE(filter.Strip(once, GURL(), kTypeDocument))
      << "a redirect that strips must not be able to loop";
}

TEST(AdblockParamFilterTest, AListRuleAppliesOnlyWhereItsPatternDoes) {
  AdblockParamFilter filter;
  filter.AddRules("||example.com^$removeparam=trk\n");
  EXPECT_EQ(Stripped(filter, "https://example.com/p?trk=1&a=2"),
            "https://example.com/p?a=2");
  EXPECT_EQ(Stripped(filter, "https://www.example.com/p?trk=1"),
            "https://www.example.com/p");
  EXPECT_EQ(Stripped(filter, "https://notexample.com/p?trk=1"), "(unchanged)");
  EXPECT_EQ(Stripped(filter, "https://other.org/p?trk=1"), "(unchanged)");
}

TEST(AdblockParamFilterTest, OptionsScopeARuleToTypeAndInitiator) {
  AdblockParamFilter filter;
  filter.AddRules(
      "||video.example^$xhr,removeparam=ad_config,domain=player.example\n");
  const GURL player("https://player.example/watch");
  EXPECT_EQ(Stripped(filter, "https://video.example/v?ad_config=1&id=9", player,
                     kTypeXhr),
            "https://video.example/v?id=9");
  EXPECT_EQ(Stripped(filter, "https://video.example/v?ad_config=1&id=9", player,
                     kTypeImage),
            "(unchanged)")
      << "it is for XHR only";
  EXPECT_EQ(Stripped(filter, "https://video.example/v?ad_config=1&id=9", Page(),
                     kTypeXhr),
            "(unchanged)")
      << "it is for player.example only";
  EXPECT_EQ(Stripped(filter, "https://video.example/v?ad_config=1&id=9"),
            "(unchanged)")
      << "a page load with no initiator is not from player.example";
}

TEST(AdblockParamFilterTest, RegexRulesMatchNameAndValue) {
  AdblockParamFilter filter;
  filter.AddRules(
      "||tv.example^$removeparam=/^ad/\n||tv.example^$removeparam=/^SID=/i\n");
  EXPECT_EQ(Stripped(filter, "https://tv.example/s?adId=1&adnet=2&keep=3&sid=9"),
            "https://tv.example/s?keep=3");
  EXPECT_EQ(Stripped(filter, "https://tv.example/s?keep=3"), "(unchanged)");
}

TEST(AdblockParamFilterTest, AnExceptionCancelsOneParameterOrAll) {
  AdblockParamFilter one;
  one.AddRules(
      "$removeparam=aaa\n$removeparam=bbb\n@@||ok.example^$removeparam=aaa\n");
  EXPECT_EQ(Stripped(one, "https://ok.example/?aaa=1&bbb=2&c=3"),
            "https://ok.example/?aaa=1&c=3")
      << "only the excepted parameter is spared";
  EXPECT_EQ(Stripped(one, "https://other.example/?aaa=1&bbb=2&c=3"),
            "https://other.example/?c=3");

  AdblockParamFilter all;
  all.AddRules("$removeparam=aaa\n@@||exempt.example^$removeparam\n");
  EXPECT_EQ(Stripped(all, "https://exempt.example/?aaa=1"), "(unchanged)");
  EXPECT_EQ(Stripped(all, "https://other.example/?aaa=1"),
            "https://other.example/");
}

TEST(AdblockParamFilterTest, UnsafeOrUnknownRulesAreDroppedAndCounted) {
  AdblockParamFilter filter;
  filter.AddRules(
      "||a.example^$removeparam\n"           // would strip every query
      "||b.example^$removeparam=~keep\n"     // all-except
      "||c.example^$removeparam=name=value\n"
      "||d.example^$removeparam=x,method=post\n"
      "||e.example^$removeparam=/([/\n"      // not a regex
      "||f.example^$removeparam=good\n");
  EXPECT_EQ(filter.rule_count(), 1u);
  EXPECT_EQ(filter.skipped_count(), 5u);
  EXPECT_EQ(Stripped(filter, "https://a.example/?q=1"), "(unchanged)");
  EXPECT_EQ(Stripped(filter, "https://f.example/?good=1&q=1"),
            "https://f.example/?q=1");
}

TEST(AdblockParamFilterTest, ARegexMayEndInADollar) {
  // uBlock's lists write regexes that end in `$`; the last `$` of the line then
  // belongs to the regex, not to the options.
  AdblockParamFilter filter;
  filter.AddRules("||media.example/s/*$xhr,removeparam=/^ad[a-z]*(=.*)?$/\n");
  EXPECT_EQ(filter.rule_count(), 1u);
  EXPECT_EQ(Stripped(filter, "https://media.example/s/1?adnet=7&keep=1&ad=2",
                     GURL(), kTypeXhr),
            "https://media.example/s/1?keep=1");
}

TEST(AdblockParamFilterTest, ALookaheadRegexIsDroppedNotMisread) {
  // RE2 has no lookahead (it guarantees linear time), so a rule that needs one
  // is skipped and counted rather than approximated.
  AdblockParamFilter filter;
  filter.AddRules(
      "||media.example/s/*$xhr,removeparam=/^((?!formats|profile).)*$/\n");
  EXPECT_EQ(filter.rule_count(), 0u);
  EXPECT_EQ(filter.skipped_count(), 1u);
}

TEST(AdblockParamFilterTest, ARemoveparamLineIsNeverABlockRule) {
  // The block engine must keep dropping these lines: admitting one would block
  // the whole request instead of cleaning it.
  AdblockFilterEngine engine;
  EXPECT_EQ(engine.AddRules("||example.com^$removeparam=trk\n$removeparam=gclid\n"),
            0u);
  EXPECT_FALSE(engine.ShouldBlock(GURL("https://example.com/p?trk=1"), Page(),
                                  kTypeDocument));
}

TEST(AdblockParamFilterTest, OnlyWebAddressesAndOnlyThoseWithAQuery) {
  AdblockParamFilter filter;
  filter.AddBuiltInRules();
  EXPECT_EQ(Stripped(filter, "chrome://settings/?utm_source=1"), "(unchanged)");
  EXPECT_EQ(Stripped(filter, "file:///c:/a.html?utm_source=1"), "(unchanged)");
  EXPECT_EQ(Stripped(filter, "https://a.example/"), "(unchanged)");
}

}  // namespace
}  // namespace zephyrus_adblock
