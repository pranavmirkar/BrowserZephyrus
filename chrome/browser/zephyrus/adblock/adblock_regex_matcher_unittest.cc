// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

// The `/regex/` network rules: parsed by AdblockFilterEngine, run by
// AdblockRegexMatcher, and folded into the verdict of the engine or the
// compiled index (important, then block, then exception).

#include "chrome/browser/zephyrus/adblock/adblock_regex_matcher.h"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "base/time/time.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_zindex.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/i18n/unicode/regex.h"
#include "third_party/re2/src/re2/re2.h"
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

// --- Lookahead and back-references (RE2 cannot; ICU checks them) ---------------

TEST(AdblockRegexMatcherTest, ANegativeLookaheadIsHonoured) {
  Rules r("/ad(?!ok)[0-9]/\n");
  EXPECT_EQ(r.matcher->rule_count(), 1u);
  EXPECT_EQ(r.matcher->verified_count(), 1u);
  EXPECT_TRUE(r.Blocks("https://cdn.example/ad7"));
  EXPECT_FALSE(r.Blocks("https://cdn.example/adok7"))
      << "the widened regex alone would have blocked this";
}

TEST(AdblockRegexMatcherTest, APositiveLookaheadIsHonoured) {
  // Five letters or digits, at least one a digit.
  Rules r("/^https:\\/\\/cdn\\.example\\/(?=[a-z]*[0-9])[a-z0-9]{5}\\.js$/"
          "$script,match-case\n");
  EXPECT_EQ(r.matcher->verified_count(), 1u);
  EXPECT_TRUE(r.Blocks("https://cdn.example/ab1de.js"));
  EXPECT_TRUE(r.Blocks("https://cdn.example/abcd5.js"));
  EXPECT_FALSE(r.Blocks("https://cdn.example/abcde.js"));
  EXPECT_FALSE(r.Blocks("https://cdn.example/ab1de.js", kTypeImage));
}

TEST(AdblockRegexMatcherTest, ALookaheadFollowsTheRulesCaseSetting) {
  // Without $match-case [A-Z] also reads lower case; with it, it does not.
  Rules r("/\\/p(?=[A-Z])[a-zA-Z]+\\/x/\n/\\/q(?=[A-Z])[a-zA-Z]+\\/x/$match-case\n");
  EXPECT_TRUE(r.Blocks("https://cdn.example/pabc/x"));
  EXPECT_FALSE(r.Blocks("https://cdn.example/qabc/x"));
  EXPECT_TRUE(r.Blocks("https://cdn.example/qAbc/x"));
}

TEST(AdblockRegexMatcherTest, ABackReferenceIsHonoured) {
  Rules r("/(\\d)\\1\\1\\.js/\n");
  EXPECT_EQ(r.matcher->verified_count(), 1u);
  EXPECT_TRUE(r.Blocks("https://cdn.example/a777.js"));
  EXPECT_FALSE(r.Blocks("https://cdn.example/a778.js"));
}

TEST(AdblockRegexMatcherTest, ALookaroundInsideGroupsAndClassesIsFound) {
  // A `)` inside a class and an escaped one do not close the group early; a
  // lookahead sits inside an alternation and a non-capturing group.
  Rules r("/\\/v(?:1(?=[()]a)|2(?!\\)))\\/z/\n");
  EXPECT_EQ(r.matcher->verified_count(), 1u);
  EXPECT_FALSE(r.Blocks("https://cdn.example/v1/z"));
  EXPECT_TRUE(r.Blocks("https://cdn.example/v2/z"));
}

TEST(AdblockRegexMatcherTest, ALookaheadRuleTakesPartInExceptionsAndOptions) {
  Rules r("/\\/t(?=[0-9])[a-z0-9]+\\//$script,third-party,domain=news.example.org\n"
          "@@/\\/t(?=[0-9])5[a-z0-9]*\\//\n");
  EXPECT_TRUE(r.Blocks("https://cdn.example/t7a/"));
  EXPECT_FALSE(r.Blocks("https://cdn.example/t5a/")) << "the exception rule";
  EXPECT_FALSE(r.Blocks("https://cdn.example/tzz/")) << "no digit ahead";
  EXPECT_FALSE(r.Blocks("https://cdn.example/t7a/", kTypeScript,
                        "https://other.example/"))
      << "wrong $domain=";
}

TEST(AdblockRegexMatcherTest, ABacktrackingLookaheadRuleCannotHangTheMatcher) {
  // RE2 accepts the widened regex, so ICU is asked, and ICU backtracks without
  // end here (the lookahead always fails). Its step limit ends it: the answer is
  // "no match" and it comes back at once.
  Rules r("/(x+x+)+y(?=z)/\n");
  const std::string url = "https://cdn.example/" + std::string(60, 'x') + "y";
  const base::TimeTicks start = base::TimeTicks::Now();
  EXPECT_FALSE(r.Blocks(url));
  EXPECT_LT(base::TimeTicks::Now() - start, base::Milliseconds(500));
  EXPECT_TRUE(r.Blocks(url + "z"));
}

TEST(AdblockRegexMatcherTest, ARegexNeitherEngineReadsIsLeftOutNotMisread) {
  // An unbalanced group is no regex for either; it is counted and never blocks.
  Rules r("/ad(?=x/\n/\\/real\\/[0-9]/\n");
  EXPECT_EQ(r.matcher->rule_count(), 1u);
  EXPECT_EQ(r.matcher->skipped_count(), 1u);
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

TEST(AdblockRegexMatcherTest, ThereIsACapOnHowManyRulesICUIsAskedAbout) {
  std::string rules;
  for (int i = 0; i < 600; ++i) {
    rules += "/\\/k" + std::to_string(i) + "(?=x)x\\/y/\n";
  }
  AdblockFilterEngine engine;
  engine.AddRules(rules);
  scoped_refptr<const AdblockRegexMatcher> matcher =
      AdblockRegexMatcher::Build(engine);
  EXPECT_EQ(matcher->verified_count(), 512u);
  EXPECT_EQ(matcher->skipped_count(), 600u - 512u);
  EXPECT_TRUE(matcher->Match(GURL("https://cdn.example/k7x/y"), GURL(kPage),
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

// ICU has to read a regex the way RE2 does, or trusting it for the rules RE2
// cannot read proves nothing. Every regex of a real list that BOTH compile is
// run in both on every URL of a corpus, and they must agree.
// ZEPHYRUS_H2H_LIST and ZEPHYRUS_H2H_CORPUS (url<TAB>page<TAB>type lines).
TEST(AdblockRegexMatcherTest, IcuReadsTheRealRegexesLikeRE2Does) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  const std::optional<std::string> list_path = env->GetVar("ZEPHYRUS_H2H_LIST");
  const std::optional<std::string> corpus_path =
      env->GetVar("ZEPHYRUS_H2H_CORPUS");
  if (!list_path || !corpus_path) {
    GTEST_SKIP() << "set ZEPHYRUS_H2H_LIST and ZEPHYRUS_H2H_CORPUS to run";
  }
  std::string list;
  std::string corpus;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath::FromUTF8Unsafe(*list_path),
                                     &list));
  ASSERT_TRUE(base::ReadFileToString(
      base::FilePath::FromUTF8Unsafe(*corpus_path), &corpus));
  list = PreprocessFilterList(list);
  std::vector<std::string> urls;
  for (const std::string_view line : base::SplitStringPiece(
           corpus, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const GURL url(line.substr(0, line.find('\t')));
    if (url.is_valid()) {
      urls.push_back(url.spec());  // the form the matcher is given
    }
  }
  ASSERT_GT(urls.size(), 100u);

  AdblockFilterEngine engine;
  engine.AddRules(list);
  size_t both = 0;
  size_t icu_only_failures = 0;
  size_t matched = 0;
  size_t disagreements = 0;
  for (const FilterRule& rule : engine.regex_rules()) {
    RE2::Options options;
    options.set_case_sensitive(rule.match_case);
    options.set_log_errors(false);
    RE2 re2(rule.pattern, options);
    if (!re2.ok()) {
      continue;
    }
    UErrorCode status = U_ZERO_ERROR;
    UParseError parse_error;
    std::unique_ptr<icu::RegexPattern> icu_pattern(icu::RegexPattern::compile(
        icu::UnicodeString::fromUTF8(rule.pattern),
        rule.match_case ? 0u : static_cast<uint32_t>(UREGEX_CASE_INSENSITIVE),
        parse_error, status));
    if (U_FAILURE(status)) {
      ++icu_only_failures;
      ADD_FAILURE() << "ICU cannot compile a regex RE2 can: " << rule.pattern;
      continue;
    }
    ++both;
    for (const std::string& url : urls) {
      const bool by_re2 = RE2::PartialMatch(url, re2);
      status = U_ZERO_ERROR;
      const icu::UnicodeString text = icu::UnicodeString::fromUTF8(url);
      std::unique_ptr<icu::RegexMatcher> matcher(
          icu_pattern->matcher(text, status));
      const bool by_icu = U_SUCCESS(status) && matcher->find(status) &&
                          U_SUCCESS(status);
      matched += by_re2;
      if (by_re2 != by_icu && ++disagreements <= 10) {
        ADD_FAILURE() << "RE2 " << by_re2 << " ICU " << by_icu << " for "
                      << rule.pattern << " on " << url;
      }
    }
  }
  std::printf("  ICU vs RE2: %zu regexes x %zu urls, %zu matches, %zu "
              "disagreements, %zu ICU compile failures\n",
              both, urls.size(), matched, disagreements, icu_only_failures);
  EXPECT_GT(both, 100u);
  EXPECT_EQ(disagreements, 0u);
}

}  // namespace
}  // namespace zephyrus_adblock
