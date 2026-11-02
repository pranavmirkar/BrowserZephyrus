// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

// The `/regex/` network rules: parsed by AdblockFilterEngine, run by
// AdblockRegexMatcher, and folded into the verdict of the engine or the
// compiled index (important, then block, then exception).

#include "chrome/browser/zephyrus/adblock/adblock_regex_matcher.h"

#include <optional>
#include <string>
#include <vector>

#include "base/time/time.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_zindex.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_adblock {
namespace {

constexpr char kPage[] = "https://news.example.org/";

// A rule set, three ways: the engine with the regex matcher, the compiled index
// with the regex matcher, and the matcher's own hits.
struct Rules {
  explicit Rules(const std::string& text) {
    engine.AddRules(text);
    matcher = AdblockRegexMatcher::Build(engine);
    AdblockZIndexBuilder builder;
    engine.ForEachRule(
        [&](AdblockFilterEngine::RuleGroup group, const FilterRule& rule,
            std::string_view) { builder.Add(group, rule); });
    std::optional<std::vector<uint8_t>> built = builder.Finish();
    EXPECT_TRUE(built.has_value());
    if (built) {
      blob = std::move(*built);
    }
    EXPECT_TRUE(index.Load(blob));
  }

  RegexHits Hits(const std::string& url,
                 ResourceType type,
                 const std::string& page) const {
    return matcher->Match(GURL(url), GURL(page), type);
  }

  // The engine and the index must agree; returns what they say.
  bool Blocks(const std::string& url,
              ResourceType type = kTypeScript,
              const std::string& page = kPage) const {
    const RegexHits hits = Hits(url, type, page);
    const bool by_engine =
        engine.ShouldBlock(GURL(url), GURL(page), type, &hits);
    const bool by_index = index.ShouldBlock(GURL(url), GURL(page), type, &hits);
    EXPECT_EQ(by_engine, by_index) << url;
    return by_engine;
  }

  AdblockFilterEngine engine;
  scoped_refptr<const AdblockRegexMatcher> matcher;
  std::vector<uint8_t> blob;
  AdblockZIndex index;
};

TEST(AdblockRegexMatcherTest, ARegexRuleBlocksWhatItMatches) {
  Rules r("/\\/ads\\/[0-9]+\\.js/\n");
  EXPECT_EQ(r.matcher->rule_count(), 1u);
  EXPECT_TRUE(r.Blocks("https://cdn.example/ads/123.js"));
  EXPECT_FALSE(r.Blocks("https://cdn.example/ads/abc.js"));
  EXPECT_FALSE(r.Blocks("https://cdn.example/ads/123.css"));
}

TEST(AdblockRegexMatcherTest, ARegexIsNotAnchored) {
  Rules r("/track[0-9]/\n");
  EXPECT_TRUE(r.Blocks("https://cdn.example/a/track7/b"));
  EXPECT_TRUE(r.Blocks("https://track7.example/"));
  EXPECT_TRUE(r.Blocks("https://cdn.example/a?x=track7"));
}

TEST(AdblockRegexMatcherTest, ADollarInsideTheRegexIsAnAnchorNotTheOptions) {
  // `\.gif$` ends the URL; the `$` is part of the regex.
  Rules r("/\\.gif$/\n/\\.png$/$image\n");
  EXPECT_EQ(r.matcher->rule_count(), 2u);
  EXPECT_TRUE(r.Blocks("https://cdn.example/a.gif", kTypeImage));
  EXPECT_FALSE(r.Blocks("https://cdn.example/a.gif?x=1", kTypeImage));
  EXPECT_TRUE(r.Blocks("https://cdn.example/a.png", kTypeImage));
  EXPECT_FALSE(r.Blocks("https://cdn.example/a.png", kTypeScript));
}

TEST(AdblockRegexMatcherTest, OptionsScopeARegexRule) {
  Rules r("/ad[0-9]+\\.js/$script,third-party,domain=news.example.org\n");
  EXPECT_TRUE(r.Blocks("https://cdn.example/ad7.js"));
  EXPECT_FALSE(r.Blocks("https://cdn.example/ad7.js", kTypeImage))
      << "wrong type";
  EXPECT_FALSE(r.Blocks("https://news.example.org/ad7.js"))
      << "first party";
  EXPECT_FALSE(r.Blocks("https://cdn.example/ad7.js", kTypeScript,
                        "https://other.example/"))
      << "wrong $domain=";
}

TEST(AdblockRegexMatcherTest, ToScopesARegexRuleToTheDestinationHost) {
  Rules r("/\\/c\\/[a-z]+/$to=landing.example|~safe.landing.example\n");
  EXPECT_TRUE(r.Blocks("https://landing.example/c/abc"));
  EXPECT_TRUE(r.Blocks("https://sub.landing.example/c/abc"));
  EXPECT_FALSE(r.Blocks("https://safe.landing.example/c/abc"));
  EXPECT_FALSE(r.Blocks("https://other.example/c/abc"));
}

TEST(AdblockRegexMatcherTest, TheCaseOfTheUrlDoesNotMatterUnlessAskedFor) {
  Rules r("/\\/Banner[0-9]/\n/\\/Tracker[0-9]/$match-case\n");
  EXPECT_TRUE(r.Blocks("https://cdn.example/banner1"));
  EXPECT_TRUE(r.Blocks("https://cdn.example/BANNER1"));
  EXPECT_TRUE(r.Blocks("https://cdn.example/Tracker1"));
  EXPECT_FALSE(r.Blocks("https://cdn.example/tracker1"));
  EXPECT_FALSE(r.Blocks("https://cdn.example/TRACKER1"));
}

TEST(AdblockRegexMatcherTest, MatchCaseOnAPlainRuleDropsTheRule) {
  // The engine lowercases URL and pattern, so it cannot honour match-case;
  // blocking case-insensitively instead would over-block.
  AdblockFilterEngine engine;
  EXPECT_EQ(engine.AddRules("||ads.example^$match-case"), 0u);
  EXPECT_EQ(engine.block_rule_count(), 0u);
}

TEST(AdblockRegexMatcherTest, AnExceptionRegexCancelsABlock) {
  Rules r("||ads.example^\n@@/\\/ok\\/[0-9]+/\n");
  EXPECT_TRUE(r.Blocks("https://ads.example/x.js"));
  EXPECT_FALSE(r.Blocks("https://ads.example/ok/12/x.js"));
}

TEST(AdblockRegexMatcherTest, APlainExceptionCancelsARegexBlock) {
  Rules r("/\\/ad[0-9]+\\.js/\n@@||cdn.example/ad7.js\n");
  EXPECT_FALSE(r.Blocks("https://cdn.example/ad7.js"));
  EXPECT_TRUE(r.Blocks("https://cdn.example/ad8.js"));
}

TEST(AdblockRegexMatcherTest, ImportantBeatsAnException) {
  Rules r("/\\/pixel\\/[0-9]+/$important\n@@||cdn.example^\n");
  EXPECT_TRUE(r.Blocks("https://cdn.example/pixel/9"));
  // And an exception that is itself important does not turn into a block.
  Rules e("/\\/pixel\\/[0-9]+/\n@@/\\/pixel\\/1/$important\n");
  EXPECT_FALSE(e.Blocks("https://cdn.example/pixel/1"));
  EXPECT_TRUE(e.Blocks("https://cdn.example/pixel/2"));
}

TEST(AdblockRegexMatcherTest, ARegexWithNothingToPrefilterOnStillRuns) {
  // No literal of three characters: tried for every request.
  Rules r("/[0-9]{9}/\n");
  EXPECT_EQ(r.matcher->unfiltered_count(), 1u);
  EXPECT_TRUE(r.Blocks("https://cdn.example/a/123456789"));
  EXPECT_FALSE(r.Blocks("https://cdn.example/a/12345678"));
}

TEST(AdblockRegexMatcherTest, ARegexRE2CannotCompileIsLeftOutNotMisread) {
  // Lookahead and back-references are not regular; RE2 refuses them. The rule
  // is counted as skipped and never blocks anything.
  Rules r("/ad(?!ok)[0-9]/\n/(a)\\1/\n/\\/real\\/[0-9]/\n");
  EXPECT_EQ(r.matcher->rule_count(), 1u);
  EXPECT_EQ(r.matcher->skipped_count(), 2u);
  EXPECT_FALSE(r.Blocks("https://cdn.example/ad7"));
  EXPECT_FALSE(r.Blocks("https://cdn.example/aa"));
  EXPECT_TRUE(r.Blocks("https://cdn.example/real/7"));
}

TEST(AdblockRegexMatcherTest, ARegexIsNotAPathRule) {
  // `/ads/*` is a pattern that merely starts with a slash.
  AdblockFilterEngine engine;
  EXPECT_EQ(engine.AddRules("/ads/*"), 1u);
  EXPECT_EQ(engine.block_rule_count(), 1u);
  EXPECT_TRUE(engine.regex_rules().empty());
  // `/ads/` with options is a regex.
  AdblockFilterEngine regex;
  EXPECT_EQ(regex.AddRules("/ads/$script"), 1u);
  EXPECT_EQ(regex.block_rule_count(), 0u);
  EXPECT_EQ(regex.regex_rules().size(), 1u);
}

TEST(AdblockRegexMatcherTest, UnsupportedOptionsDropTheRegexRuleToo) {
  AdblockFilterEngine engine;
  EXPECT_EQ(engine.AddRules("/\\/a[0-9]/$csp=script-src 'none'\n"
                            "/\\/b[0-9]/$header=x\n"),
            0u);
  EXPECT_TRUE(engine.regex_rules().empty());
}

TEST(AdblockRegexMatcherTest, ATaggedRuleIsNeverARegex) {
  // `$removeparam` rules are found by ForEachMatchingRule, which does not see
  // regexes; accepting one there would make it silently never apply.
  AdblockFilterEngine engine;
  EXPECT_FALSE(engine.AddTaggedRule("/\\/p[0-9]/", 7));
  EXPECT_TRUE(engine.regex_rules().empty());
}

TEST(AdblockRegexMatcherTest, ARegexPageExceptionIsNotSupported) {
  AdblockFilterEngine engine;
  EXPECT_EQ(engine.AddRules("@@/\\/p[0-9]/$document"), 0u);
}

TEST(AdblockRegexMatcherTest, AnOverlongUrlIsAllowedNotScanned) {
  Rules r("/\\/ads\\/[0-9]+/\n");
  const std::string long_path(AdblockRegexMatcher::kMaxMatchedUrlBytes, 'a');
  EXPECT_TRUE(r.Blocks("https://cdn.example/ads/1"));
  EXPECT_FALSE(r.Blocks("https://cdn.example/ads/1/" + long_path));
}

TEST(AdblockRegexMatcherTest, ABacktrackingRegexCannotHangTheMatcher) {
  // Nested quantifiers freeze a backtracking engine for minutes on this input;
  // RE2 is linear, so the answer comes back at once.
  Rules r("/(x+x+)+y/\n");
  const std::string url = "https://cdn.example/" + std::string(8000, 'x');
  const base::TimeTicks start = base::TimeTicks::Now();
  EXPECT_FALSE(r.Blocks(url));
  EXPECT_LT(base::TimeTicks::Now() - start, base::Milliseconds(200));
  EXPECT_TRUE(r.Blocks(url + "y"));
}

TEST(AdblockRegexMatcherTest, ThereIsACapOnHowManyRegexesAreCompiled) {
  // A list cannot make the browser compile a million regexes.
  std::string rules;
  for (int i = 0; i < 5000; ++i) {
    rules += "/\\/item" + std::to_string(i) + "\\/x/\n";
  }
  AdblockFilterEngine engine;
  engine.AddRules(rules);
  scoped_refptr<const AdblockRegexMatcher> matcher =
      AdblockRegexMatcher::Build(engine);
  EXPECT_EQ(matcher->rule_count(), 4096u);
  EXPECT_EQ(matcher->skipped_count(), 5000u - 4096u);
  // The ones kept still work.
  EXPECT_TRUE(matcher->Match(GURL("https://cdn.example/item7/x"), GURL(kPage),
                             kTypeScript)
                  .block);
}

TEST(AdblockRegexMatcherTest, AnEmptyMatcherSaysNothing) {
  Rules r("||ads.example^\n");
  EXPECT_EQ(r.matcher->rule_count(), 0u);
  const RegexHits hits = r.Hits("https://ads.example/x", kTypeScript, kPage);
  EXPECT_FALSE(hits.important || hits.block || hits.exception);
  EXPECT_TRUE(r.Blocks("https://ads.example/x"));
}

TEST(AdblockRegexMatcherTest, BuildFromListTakesOnlyTheRegexLines) {
  const std::string list =
      "! comment\n||ads.example^\n/\\/r[0-9]/$script\n@@/\\/ok[0-9]/\n"
      "example.com##.ad\n/plain/path/*\n";
  scoped_refptr<const AdblockRegexMatcher> matcher =
      AdblockRegexMatcher::BuildFromList(list);
  EXPECT_EQ(matcher->rule_count(), 2u);
  const RegexHits hit = matcher->Match(GURL("https://cdn.example/r7"),
                                       GURL(kPage), kTypeScript);
  EXPECT_TRUE(hit.block);
  const RegexHits ok = matcher->Match(GURL("https://cdn.example/ok1"),
                                      GURL(kPage), kTypeScript);
  EXPECT_TRUE(ok.exception);
}

TEST(AdblockRegexMatcherTest, GetOrBuildSharesPerListKey) {
  AdblockRegexMatcher::ResetForTesting();
  const std::string list = "/\\/r[0-9]/\n";
  auto a = AdblockRegexMatcher::GetOrBuild(42, list);
  auto b = AdblockRegexMatcher::GetOrBuild(42, list);
  EXPECT_EQ(a.get(), b.get());
  auto c = AdblockRegexMatcher::GetOrBuild(43, list);
  EXPECT_NE(a.get(), c.get());
  // Key 0 is "no key": never shared.
  auto d = AdblockRegexMatcher::GetOrBuild(0, list);
  auto e = AdblockRegexMatcher::GetOrBuild(0, list);
  EXPECT_NE(d.get(), e.get());
  AdblockRegexMatcher::ResetForTesting();
}

}  // namespace
}  // namespace zephyrus_adblock
