// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_urlskip_filter.h"

#include <algorithm>
#include <optional>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "base/time/time.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_adblock {
namespace {

constexpr char kPage[] = "https://news.example.org/";
constexpr char kTrusted[] =
    "! ===== https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/"
    "filters/privacy.txt =====\n";
constexpr char kThirdParty[] =
    "! ===== https://lists.example/evil.txt =====\n";

std::optional<std::string> Steps(const std::string& steps,
                                 const std::string& url) {
  const std::optional<GURL> out =
      AdblockUrlSkipFilter::ApplySteps(steps, GURL(url));
  if (!out) {
    return std::nullopt;
  }
  return out->spec();
}

// --- The steps -------------------------------------------------------------

TEST(AdblockUrlSkipTest, AQueryParameterIsTheDestination) {
  EXPECT_EQ(Steps("?url",
                  "https://t.example/c?id=7&url=https%3A%2F%2Fnews.example%2Fa"
                  "%3Fx%3D1&z=2"),
            "https://news.example/a?x=1");
}

TEST(AdblockUrlSkipTest, ANestedDestinationNeedsOneStepPerLevel) {
  const std::string inner = "https%3A%2F%2Fnews.example%2Fa";
  const std::string outer =
      "https://t.example/c?to=https%3A%2F%2Fr.example%2F%3Fnext%3D" + inner;
  // The first step gives r.example's address, whose `next` is the target.
  EXPECT_EQ(Steps("?to ?next", outer), "https://news.example/a");
  EXPECT_EQ(Steps("?to ?missing", outer), std::nullopt);
}

TEST(AdblockUrlSkipTest, AMissingParameterMeansNoSkip) {
  EXPECT_EQ(Steps("?url", "https://t.example/c?other=1"), std::nullopt);
  EXPECT_EQ(Steps("?url", "https://t.example/c"), std::nullopt);
}

TEST(AdblockUrlSkipTest, Base64AndItsUrlSafeForm) {
  const std::string target = "https://n.example/????";
  std::string encoded;
  encoded = base::Base64Encode(std::string_view(target));
  ASSERT_NE(encoded.find('/'), std::string::npos) << "the case under test";
  ASSERT_EQ(encoded.find('+'), std::string::npos) << "'+' would read as a space";
  ASSERT_NE(encoded.find('='), std::string::npos) << "the padding under test";
  std::string safe = encoded;
  std::replace(safe.begin(), safe.end(), '+', '-');
  std::replace(safe.begin(), safe.end(), '/', '_');
  EXPECT_EQ(Steps("?u -base64", "https://t.example/c?u=" + encoded),
            GURL(target).spec());
  // Links usually drop the padding; atob accepts that, so do we.
  while (!encoded.empty() && encoded.back() == '=') {
    encoded.pop_back();
  }
  EXPECT_EQ(Steps("?u -base64", "https://t.example/c?u=" + encoded),
            GURL(target).spec());
  EXPECT_EQ(Steps("?u -safebase64", "https://t.example/c?u=" + safe),
            GURL(target).spec());
  EXPECT_EQ(Steps("?u -base64", "https://t.example/c?u=%21%21notbase64"),
            std::nullopt);
}

TEST(AdblockUrlSkipTest, PlusHttpsAddsOrUpgradesTheScheme) {
  EXPECT_EQ(Steps("?u +https", "https://t.example/c?u=news.example/a"),
            "https://news.example/a");
  EXPECT_EQ(Steps("?u +https", "https://t.example/c?u=http%3A%2F%2Fnews.example"),
            "https://news.example/");
  EXPECT_EQ(Steps("?u +https", "https://t.example/c?u=https%3A%2F%2Fnews.example"),
            "https://news.example/");
}

TEST(AdblockUrlSkipTest, ARegexTakesItsFirstCaptureGroup) {
  EXPECT_EQ(Steps(R"(/\/url\/(http.+)/ -uricomponent)",
                  "https://t.example/url/https%3A%2F%2Fnews.example%2Fa"),
            "https://news.example/a");
  EXPECT_EQ(Steps(R"(/\/url\/(http.+)/)", "https://t.example/other/x"),
            std::nullopt);
  // A regex with no group has nothing to extract: the rule is refused.
  EXPECT_EQ(Steps(R"(/\/url\/http.+/)", "https://t.example/url/https://x.example"),
            std::nullopt);
}

TEST(AdblockUrlSkipTest, ARegexStepCanHoldASpace) {
  EXPECT_EQ(Steps(R"(/to=(https?:\/\/[^& ]+)/)",
                  "https://t.example/c?to=https://news.example/a"),
            "https://news.example/a");
}

TEST(AdblockUrlSkipTest, TheFragmentAndTheNameOfAParameter) {
  EXPECT_EQ(Steps("#", "https://t.example/x#https://news.example/a"),
            "https://news.example/a");
  EXPECT_EQ(Steps("#", "https://t.example/x"), std::nullopt);
  EXPECT_EQ(Steps("&1 -uricomponent",
                  "https://t.example/x?https%3A%2F%2Fnews.example%2Fa&b=2"),
            "https://news.example/a");
  EXPECT_EQ(Steps("&3", "https://t.example/x?a=1&b=2"), std::nullopt);
}

TEST(AdblockUrlSkipTest, UriComponentRefusesWhatJavaScriptWouldThrowOn) {
  // The query step decodes once: "%252" becomes "%2", a `%` with no second hex
  // digit, which decodeURIComponent throws on.
  EXPECT_EQ(Steps("?u -uricomponent", "https://t.example/c?u=%252"),
            std::nullopt);
  // "%2525zz" becomes "%25zz", which decodes fine to "%zz": not an address.
  EXPECT_EQ(Steps("?u -uricomponent", "https://t.example/c?u=%2525zz"),
            std::nullopt);
}

TEST(AdblockUrlSkipTest, OnlyAWebAddressOtherThanTheOneAskedForIsATarget) {
  EXPECT_EQ(Steps("?u", "https://t.example/c?u=javascript%3Aalert(1)"),
            std::nullopt);
  EXPECT_EQ(Steps("?u", "https://t.example/c?u=data%3Atext%2Fhtml%2Chi"),
            std::nullopt);
  EXPECT_EQ(Steps("?u", "https://t.example/c?u=ftp%3A%2F%2Fx.example%2F"),
            std::nullopt);
  EXPECT_EQ(Steps("?u", "https://t.example/c?u=%2F%2Fnews.example%2F"),
            std::nullopt)
      << "no scheme: not an address";
  EXPECT_EQ(Steps("?u", "https://t.example/c?u=https%3A%2F%2Ft.example%2Fc%3Fu"
                        "%3Dhttps%253A%252F%252Fx"),
            "https://t.example/c?u=https%3A%2F%2Fx");
  EXPECT_EQ(Steps("#", "https://t.example/c#https://t.example/c"), std::nullopt)
      << "the same page";
  EXPECT_EQ(
      Steps("?u", "https://t.example/c?u=https%3A%2F%2Fbank.example%40evil.example%2F"),
      std::nullopt)
      << "credentials in the target";
  // The query step leaves a control character escaped; a decode step does not,
  // and then the result is refused.
  EXPECT_EQ(Steps("?u", "https://t.example/c?u=https%3A%2F%2Fnews.example%2F%0Aa"),
            "https://news.example/%0Aa");
  EXPECT_EQ(Steps("?u -uricomponent",
                  "https://t.example/c?u=https%253A%252F%252Fnews.example%252F%250Aa"),
            std::nullopt)
      << "control characters";
}

TEST(AdblockUrlSkipTest, AStepNobodyKnowsDropsTheWholeRule) {
  EXPECT_EQ(Steps("?u -rot13", "https://t.example/c?u=https%3A%2F%2Fnews.example"),
            std::nullopt);
  EXPECT_EQ(Steps("", "https://t.example/c?u=x"), std::nullopt);
  EXPECT_EQ(Steps("?a ?a ?a ?a ?a ?a ?a ?a ?a", "https://t.example/c?a=x"),
            std::nullopt)
      << "more steps than any rule has";
}

// --- Rules -------------------------------------------------------------------

// A filter built from list text.
struct Loaded : AdblockUrlSkipFilter {
  explicit Loaded(const std::string& rules, bool trust_unsectioned = true) {
    AddRules(rules, trust_unsectioned);
  }
};

std::optional<std::string> Skip(const AdblockUrlSkipFilter& filter,
                                const std::string& url,
                                ResourceType type = kTypeDocument,
                                bool blocked = false,
                                const std::string& page = kPage) {
  const std::optional<GURL> out =
      filter.Skip(GURL(url), GURL(page), type, blocked);
  return out ? std::optional<std::string>(out->spec()) : std::nullopt;
}

constexpr char kClick[] =
    "https://t.example/click?url=https%3A%2F%2Fnews.example%2Fa&z=1";

TEST(AdblockUrlSkipTest, ARuleSkipsTheNavigationsItMatches) {
  const Loaded filter("||t.example/click?$doc,urlskip=?url\n");
  EXPECT_EQ(filter.rule_count(), 1u);
  EXPECT_EQ(Skip(filter, kClick), "https://news.example/a");
  EXPECT_EQ(Skip(filter, "https://t.example/other?url=https%3A%2F%2Fx.example"),
            std::nullopt);
  EXPECT_EQ(Skip(filter, "https://elsewhere.example/click?url=https%3A%2F%2Fx.example"),
            std::nullopt);
  EXPECT_EQ(Skip(filter, kClick, kTypeScript), std::nullopt)
      << "$doc is for page loads";
}

TEST(AdblockUrlSkipTest, ARuleWithoutATypeAppliesToAFrameToo) {
  const Loaded filter("||t.example/click?$urlskip=?url\n");
  EXPECT_EQ(Skip(filter, kClick, kTypeDocument), "https://news.example/a");
  EXPECT_EQ(Skip(filter, kClick, kTypeSubdocument), "https://news.example/a");
}

TEST(AdblockUrlSkipTest, OptionsAfterTheValueStillScopeTheRule) {
  const Loaded filter("||t.example/click?$urlskip=?url,doc,domain=news.example.org\n");
  EXPECT_EQ(Skip(filter, kClick), "https://news.example/a");
  EXPECT_EQ(Skip(filter, kClick, kTypeDocument, false, "https://other.example/"),
            std::nullopt);
  const Loaded to_filter("||t.example/click?$doc,urlskip=?url,to=elsewhere.example\n");
  EXPECT_EQ(Skip(to_filter, kClick), std::nullopt);
}

TEST(AdblockUrlSkipTest, ARegexStepWithACommaIsNotCutAtTheComma) {
  const Loaded filter(
      R"(||t.example/go/$doc,urlskip=/\/go\/x{0,2}(https?[^#]*)/ -uricomponent)"
      "\n");
  EXPECT_EQ(filter.rule_count(), 1u);
  EXPECT_EQ(Skip(filter, "https://t.example/go/https%3A%2F%2Fnews.example%2Fa"),
            "https://news.example/a");
}

TEST(AdblockUrlSkipTest, ABlockedRequestIsOnlySkippedByABlockedRule) {
  const Loaded plain("||t.example/click?$doc,urlskip=?url\n");
  const Loaded forced("||t.example/click?$doc,urlskip=?url -blocked\n");
  EXPECT_EQ(Skip(plain, kClick, kTypeDocument, /*blocked=*/false),
            "https://news.example/a");
  EXPECT_EQ(Skip(plain, kClick, kTypeDocument, /*blocked=*/true), std::nullopt);
  EXPECT_EQ(Skip(forced, kClick, kTypeDocument, /*blocked=*/true),
            "https://news.example/a");
  EXPECT_EQ(Skip(forced, kClick, kTypeDocument, /*blocked=*/false),
            "https://news.example/a");
}

TEST(AdblockUrlSkipTest, ARuleWhoseStepsFailLeavesTheRequestAlone) {
  const Loaded filter("||t.example/click?$doc,urlskip=?nope\n");
  EXPECT_EQ(Skip(filter, kClick), std::nullopt);
}

TEST(AdblockUrlSkipTest, TheEarliestRuleThatCanSkipWins) {
  const Loaded filter(
      "||t.example/click?$doc,urlskip=?missing\n"
      "||t.example/click?$doc,urlskip=?url\n"
      "||t.example/click?$doc,urlskip=?z\n");
  EXPECT_EQ(Skip(filter, kClick), "https://news.example/a")
      << "the first rule cannot skip, the second can";
}

TEST(AdblockUrlSkipTest, ARuleWhosePatternIsARegexIsMatchedWithRE2) {
  const Loaded filter(
      R"(/^https?:\/\/t\.example\/r\/[a-z]+\?u=/$doc,urlskip=?u)"
      "\n");
  EXPECT_EQ(filter.rule_count(), 1u);
  EXPECT_EQ(Skip(filter, "https://t.example/r/abc?u=https%3A%2F%2Fnews.example%2F"),
            "https://news.example/");
  EXPECT_EQ(Skip(filter, "https://t.example/r/ABC1?u=https%3A%2F%2Fnews.example%2F"),
            std::nullopt)
      << "the regex does not match";
  EXPECT_EQ(Skip(filter, "https://t.example/r/abc?u=https%3A%2F%2Fnews.example%2F",
                 kTypeScript),
            std::nullopt)
      << "$doc";
}

TEST(AdblockUrlSkipTest, OnlyTrustedSectionsMayRedirect) {
  const std::string rule = "||t.example/click?$doc,urlskip=?url\n";
  AdblockUrlSkipFilter filter;
  filter.AddRules(std::string(kThirdParty) + rule, /*trust_unsectioned=*/true);
  EXPECT_EQ(filter.rule_count(), 0u);
  EXPECT_EQ(filter.untrusted_count(), 1u);
  EXPECT_EQ(Skip(filter, kClick), std::nullopt);

  AdblockUrlSkipFilter both;
  both.AddRules(std::string(kThirdParty) + rule + kTrusted +
                    "||u.example/click?$doc,urlskip=?url\n",
                /*trust_unsectioned=*/false);
  EXPECT_EQ(both.rule_count(), 1u);
  EXPECT_EQ(both.untrusted_count(), 1u);
  EXPECT_EQ(Skip(both, kClick), std::nullopt);
  EXPECT_EQ(Skip(both, "https://u.example/click?url=https%3A%2F%2Fnews.example%2F"),
            "https://news.example/");

  // Lines before any section marker take the caller's word.
  EXPECT_EQ(Loaded(rule, /*trust_unsectioned=*/false).rule_count(), 0u);
  EXPECT_EQ(Loaded(rule, /*trust_unsectioned=*/true).rule_count(), 1u);
}

TEST(AdblockUrlSkipTest, LinesItCannotUseAreCountedNotMisread) {
  AdblockUrlSkipFilter filter;
  filter.AddRules(
      "@@||t.example/click?$doc,urlskip=?url\n"            // an exception
      "||t.example/a?$doc,urlskip=?url -rot13\n"            // unknown step
      "||t.example/b?$doc,strict1p,urlskip=?url\n"          // option not read
      "||t.example/c?$doc,urlskip=/x(?!y)(z)/\n"            // lookahead step
      "||t.example/d?$doc,urlskip=\n"                       // nothing to do
      "||t.example/ok?$doc,urlskip=?url\n",
      /*trust_unsectioned=*/true);
  EXPECT_EQ(filter.rule_count(), 1u);
  EXPECT_EQ(filter.skipped_count(), 5u);
}

TEST(AdblockUrlSkipTest, ADeadFilterSkipsNothing) {
  const AdblockUrlSkipFilter filter;
  EXPECT_EQ(Skip(filter, kClick), std::nullopt);
  const Loaded ok("||t.example/click?$doc,urlskip=?url\n");
  EXPECT_EQ(Skip(ok, "about:blank"), std::nullopt);
  EXPECT_EQ(Skip(ok, "file:///etc/passwd"), std::nullopt);
}

// The shipped list. ZEPHYRUS_H2H_LIST is the (preprocessed) list;
// ZEPHYRUS_URLSKIP_CORPUS, if set, holds `url<TAB>expected destination` lines made
// from the rules by research/urlskip_corpus.py, each of which must skip to exactly
// that destination. Lines whose expected column is `-` must not skip at all.
TEST(AdblockUrlSkipTest, TheShippedListLoadsAndSkipsWhatItSays) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  const std::optional<std::string> list_path = env->GetVar("ZEPHYRUS_H2H_LIST");
  if (!list_path) {
    GTEST_SKIP() << "set ZEPHYRUS_H2H_LIST (and ZEPHYRUS_URLSKIP_CORPUS) to run";
  }
  std::string list;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath::FromUTF8Unsafe(*list_path),
                                     &list));
  list = PreprocessFilterList(list);
  AdblockUrlSkipFilter filter;
  filter.AddRules(list, /*trust_unsectioned=*/false);
  std::printf("  urlskip rules: %zu usable, %zu not usable, %zu from untrusted "
              "sections\n",
              filter.rule_count(), filter.skipped_count(),
              filter.untrusted_count());
  EXPECT_GT(filter.rule_count(), 150u);

  // What it costs a navigation: Skip() on every URL of a recorded corpus, as if
  // each were a page load (an upper bound: only navigations ask).
  if (const std::optional<std::string> real = env->GetVar("ZEPHYRUS_H2H_CORPUS")) {
    std::string text;
    ASSERT_TRUE(
        base::ReadFileToString(base::FilePath::FromUTF8Unsafe(*real), &text));
    std::vector<GURL> urls;
    for (const std::string_view line : base::SplitStringPiece(
             text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      urls.emplace_back(line.substr(0, line.find('\t')));
    }
    size_t skipped = 0;
    for (const GURL& url : urls) {  // warm up
      skipped += filter.Skip(url, GURL(kPage), kTypeDocument, false).has_value();
    }
    const base::TimeTicks start = base::TimeTicks::Now();
    for (int pass = 0; pass < 3; ++pass) {
      for (const GURL& url : urls) {
        filter.Skip(url, GURL(kPage), kTypeDocument, false);
      }
    }
    const double micros = (base::TimeTicks::Now() - start).InMicrosecondsF() /
                          (3.0 * static_cast<double>(urls.size()));
    std::printf("  Skip() over %zu recorded URLs: mean %.2f us per call, %zu of "
                "them skipped\n",
                urls.size(), micros, skipped);
  }

  const std::optional<std::string> corpus_path =
      env->GetVar("ZEPHYRUS_URLSKIP_CORPUS");
  if (!corpus_path) {
    return;
  }
  std::string corpus;
  ASSERT_TRUE(base::ReadFileToString(
      base::FilePath::FromUTF8Unsafe(*corpus_path), &corpus));
  size_t total = 0;
  size_t skipped_right = 0;
  size_t left_alone_right = 0;
  size_t wrong = 0;
  for (const std::string_view line : base::SplitStringPiece(
           corpus, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const std::vector<std::string_view> cols = base::SplitStringPiece(
        line, "\t", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    if (cols.size() < 2) {
      continue;
    }
    ++total;
    const std::optional<GURL> got = filter.Skip(
        GURL(cols[0]), GURL(kPage), kTypeDocument, /*blocked=*/false);
    if (cols[1] == "-") {
      (got ? wrong : left_alone_right)++;
      if (got && wrong <= 10) {
        ADD_FAILURE() << "should not skip: " << cols[0] << " -> " << got->spec();
      }
    } else if (got && got->spec() == GURL(cols[1]).spec()) {
      ++skipped_right;
    } else {
      ++wrong;
      if (wrong <= 10) {
        ADD_FAILURE() << cols[0] << " expected " << cols[1] << " got "
                      << (got ? got->spec() : "(no skip)");
      }
    }
  }
  std::printf("  corpus: %zu urls, %zu skipped to the expected address, %zu "
              "correctly left alone, %zu wrong\n",
              total, skipped_right, left_alone_right, wrong);
  EXPECT_GT(total, 100u);
  EXPECT_EQ(wrong, 0u);
}

}  // namespace
}  // namespace zephyrus_adblock
