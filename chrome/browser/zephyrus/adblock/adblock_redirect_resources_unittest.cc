// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

// `$redirect=` / `$redirect-rule=`: the table of stand-ins, how the engine reads
// the options, and that the compiled index chooses exactly as the engine does.

#include "chrome/browser/zephyrus/adblock/adblock_redirect_resources.h"

#include <cstdio>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_zindex.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_adblock {
namespace {

constexpr char kPage[] = "https://news.example/";

uint16_t Id(std::string_view name) {
  const RedirectResource* resource = FindRedirectResource(name);
  EXPECT_NE(resource, nullptr) << name;
  return resource ? resource->id : 0;
}

// --- The table -------------------------------------------------------------

TEST(AdblockRedirectResourcesTest, NamesAndAliasesFindTheirResource) {
  EXPECT_EQ(Id("noopjs"), Id("noop.js"));
  EXPECT_EQ(Id("nooptext"), Id("noop.txt"));
  EXPECT_EQ(Id("google-analytics.com/ga.js"), Id("google-analytics_ga.js"));
  EXPECT_EQ(Id("googletagservices.com/gpt.js"), Id("googletagservices_gpt.js"));
  EXPECT_EQ(Id("popads.net.js"), Id("popads.js"));
  EXPECT_EQ(FindRedirectResource("not-a-resource"), nullptr);
  EXPECT_EQ(FindRedirectResource(""), nullptr);
  EXPECT_EQ(FindRedirectResource("none"), nullptr)
      << "`none` means no stand-in, so it names none";
}

TEST(AdblockRedirectResourcesTest, EveryIdIsUniqueAndLooksUpBackToItself) {
  std::set<uint16_t> ids;
  for (uint16_t id = 1; id < 200; ++id) {
    if (const RedirectResource* resource = RedirectResourceById(id)) {
      EXPECT_EQ(resource->id, id);
      EXPECT_TRUE(ids.insert(id).second);
      EXPECT_EQ(FindRedirectResource(resource->name), resource);
    }
  }
  EXPECT_EQ(ids.size(), RedirectResourceCount());
  EXPECT_EQ(RedirectResourceById(kNoRedirectResource), nullptr);
  EXPECT_EQ(RedirectResourceById(60000), nullptr);
}

TEST(AdblockRedirectResourcesTest, BodiesAreWhatTheirTypeSays) {
  for (uint16_t id = 1; id < 200; ++id) {
    const RedirectResource* r = RedirectResourceById(id);
    if (!r) {
      continue;
    }
    const std::string_view body = RedirectResourceBody(*r);
    EXPECT_LT(body.size(), 16u * 1024) << r->name;
    EXPECT_NE(r->types, 0u) << r->name;
    if (r->mime == "text/javascript") {
      EXPECT_GT(body.size(), 10u) << r->name;
      EXPECT_EQ(body.find("</script"), std::string_view::npos) << r->name;
    } else if (r->mime == "image/png") {
      EXPECT_TRUE(body.starts_with("\x89PNG\r\n\x1a\n")) << r->name;
    } else if (r->mime == "image/gif") {
      EXPECT_TRUE(body.starts_with("GIF89a")) << r->name;
    } else if (r->mime == "video/mp4") {
      ASSERT_GT(body.size(), 12u);
      EXPECT_EQ(body.substr(4, 4), "ftyp") << r->name;
    } else if (r->mime == "audio/mpeg") {
      ASSERT_GT(body.size(), 4u);
      EXPECT_EQ(static_cast<uint8_t>(body[0]), 0xFF) << r->name;
    } else if (r->mime == "text/xml") {
      EXPECT_TRUE(body.starts_with("<?xml")) << r->name;
    }
  }
  EXPECT_EQ(RedirectResourceBody(*FindRedirectResource("noop.txt")), "");
  EXPECT_EQ(RedirectResourceBody(*FindRedirectResource("noop.json")), "{}");
}

TEST(AdblockRedirectResourcesTest, EmptyAnswersInTheShapeTheRequestExpects) {
  const RedirectResource& empty = *FindRedirectResource("empty");
  EXPECT_EQ(RedirectResourceMime(empty, kTypeScript), "text/javascript");
  EXPECT_EQ(RedirectResourceMime(empty, kTypeStylesheet), "text/css");
  EXPECT_EQ(RedirectResourceMime(empty, kTypeSubdocument), "text/html");
  EXPECT_EQ(RedirectResourceMime(empty, kTypeXhr), "text/plain");
  const RedirectResource& gif = *FindRedirectResource("1x1.gif");
  EXPECT_EQ(RedirectResourceMime(gif, kTypeImage), "image/gif");
}

TEST(AdblockRedirectResourcesTest, BlockedRequestsAreAnsweredInTheirShape) {
  const GURL tracker("https://tracker.example/collect?id=1");
  // A beacon or a fetch gets an empty success, which the empty stand-in can
  // really serve for that kind of request.
  // A fetch or XHR gets an empty JSON object, which a page can parse; a ping
  // gets nothing.
  const RedirectResource* fetched = QuietBlockResource(tracker, kTypeXhr);
  ASSERT_TRUE(fetched);
  EXPECT_EQ(RedirectResourceBody(*fetched), "{}");
  EXPECT_TRUE(RedirectResourceServes(*fetched, kTypeXhr));
  const RedirectResource* pinged = QuietBlockResource(tracker, kTypePing);
  ASSERT_TRUE(pinged);
  EXPECT_EQ(RedirectResourceBody(*pinged), "");
  // A plain-text stand-in served for a script is served as a script.
  EXPECT_EQ(RedirectResourceMime(*FindRedirectResource("noop.txt"), kTypeScript),
            "text/javascript");
  // An image gets a transparent image: a pixel for a pixel, an icon for an
  // icon (detectors load an ad server's favicon and call anything under 2px
  // wide a stand-in), and 2x2 for anything else.
  const RedirectResource* pixel = QuietBlockResource(
      GURL("https://tracker.example/1x1.gif?e=view"), kTypeImage);
  ASSERT_TRUE(pixel);
  EXPECT_EQ(pixel->name, "1x1.gif");
  EXPECT_EQ(RedirectResourceMime(*pixel, kTypeImage), "image/gif");
  const RedirectResource* icon = QuietBlockResource(
      GURL("https://googleads.g.doubleclick.net/favicon.ico?123"), kTypeImage);
  ASSERT_TRUE(icon);
  EXPECT_EQ(icon->name, "32x32.png");
  const RedirectResource* other = QuietBlockResource(
      GURL("https://ads.example/creative/300x250.jpg"), kTypeImage);
  ASSERT_TRUE(other);
  EXPECT_EQ(other->name, "2x2.png");
  // Frames, media, fonts, styles and page loads still fail, as does a script
  // that is not one of the known libraries.
  for (ResourceType type :
       {kTypeScript, kTypeStylesheet, kTypeSubdocument, kTypeFont, kTypeMedia,
        kTypeObject, kTypeOther, kTypeWebsocket, kTypePopup, kTypeDocument}) {
    EXPECT_FALSE(QuietBlockResource(tracker, type)) << type;
  }
}

TEST(AdblockRedirectResourcesTest, AKnownAdLibraryIsAnsweredByItsStandIn) {
  struct {
    const char* url;
    const char* resource;
  } cases[] = {
      {"https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js?client=ca-pub-1",
       "googlesyndication_adsbygoogle.js"},
      {"https://securepubads.g.doubleclick.net/tag/js/gpt.js",
       "googletagservices_gpt.js"},
      {"https://imasdk.googleapis.com/js/sdkloader/ima3.js", "google-ima.js"},
      {"https://www.googletagmanager.com/gtm.js?id=GTM-X",
       "googletagmanager_gtm.js"},
      {"https://www.google-analytics.com/analytics.js",
       "google-analytics_analytics.js"},
      {"https://www.adblockanalytics.com/google-adsense.js",
       "adblockanalytics_bait.js"},
  };
  for (const auto& c : cases) {
    const RedirectResource* stand_in =
        QuietBlockResource(GURL(c.url), kTypeScript);
    ASSERT_TRUE(stand_in) << c.url;
    EXPECT_EQ(stand_in->name, c.resource) << c.url;
    EXPECT_TRUE(RedirectResourceServes(*stand_in, kTypeScript)) << c.url;
    EXPECT_FALSE(RedirectResourceBody(*stand_in).empty()) << c.url;
  }
  // The same host with another path, a look-alike host, and the library's name
  // on someone else's site are not it.
  for (const char* url :
       {"https://pagead2.googlesyndication.com/pagead/show_ads.js",
        "https://pagead2.googlesyndication.com.evil.example/pagead/js/adsbygoogle.js",
        "https://cdn.example/pagead/js/adsbygoogle.js"}) {
    EXPECT_FALSE(QuietBlockResource(GURL(url), kTypeScript)) << url;
  }
}

TEST(AdblockRedirectResourcesTest, TheKillSwitchFailsBlockedRequestsAgain) {
  base::test::ScopedFeatureList off;
  off.InitAndDisableFeature(kZephyrusAdblockQuietBlock);
  const GURL tracker("https://tracker.example/x");
  for (ResourceType type : {kTypeXhr, kTypePing, kTypeImage}) {
    EXPECT_FALSE(QuietBlockResource(tracker, type)) << type;
  }
  EXPECT_FALSE(QuietBlockResource(
      GURL("https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js"),
      kTypeScript));
}

TEST(AdblockRedirectResourcesTest, AStandInOnlyAnswersWhatItCan) {
  EXPECT_TRUE(RedirectResourceServes(*FindRedirectResource("noop.js"), kTypeScript));
  EXPECT_FALSE(RedirectResourceServes(*FindRedirectResource("noop.js"), kTypeImage));
  EXPECT_TRUE(RedirectResourceServes(*FindRedirectResource("1x1.gif"), kTypeImage));
  EXPECT_FALSE(RedirectResourceServes(*FindRedirectResource("1x1.gif"), kTypeScript));
  EXPECT_TRUE(RedirectResourceServes(*FindRedirectResource("noop.html"), kTypeSubdocument));
  EXPECT_TRUE(RedirectResourceServes(*FindRedirectResource("noop-1s.mp4"), kTypeMedia));
  EXPECT_TRUE(RedirectResourceServes(*FindRedirectResource("empty"), kTypeFont));
}

TEST(AdblockRedirectResourcesTest, TheChoiceIsPriorityThenLowestId) {
  const std::vector<RedirectCandidate> candidates = {
      {Id("noop.js"), 0}, {Id("google-ima.js"), 10}, {Id("noop.txt"), 10}};
  // Highest priority first; of the two at 10, the lower id.
  EXPECT_EQ(ChooseRedirect(candidates, {}, kTypeScript),
            std::optional<uint16_t>(Id("noop.txt")));
  // The one that cannot serve a script is passed over, not chosen.
  EXPECT_EQ(ChooseRedirect({{Id("1x1.gif"), 50}, {Id("noop.js"), 0}}, {},
                           kTypeScript),
            std::optional<uint16_t>(Id("noop.js")));
  EXPECT_EQ(ChooseRedirect({{Id("1x1.gif"), 50}}, {}, kTypeScript), std::nullopt);
  // An exception for one resource leaves the others; the nameless one leaves none.
  EXPECT_EQ(ChooseRedirect(candidates, {Id("noop.txt")}, kTypeScript),
            std::optional<uint16_t>(Id("google-ima.js")));
  EXPECT_EQ(ChooseRedirect(candidates, {kNoRedirectResource}, kTypeScript),
            std::nullopt);
  EXPECT_EQ(ChooseRedirect({}, {}, kTypeScript), std::nullopt);
}

// --- The engine, and the index that must agree with it ----------------------

struct Rules {
  explicit Rules(const std::string& text) {
    engine.AddRules(text);
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

  // What the request gets, the engine and the index agreeing: nullopt = allowed,
  // 0 = blocked outright, otherwise blocked and answered with that stand-in.
  std::optional<uint16_t> Verdict(const std::string& url,
                                  ResourceType type = kTypeScript,
                                  const std::string& page = kPage) const {
    const bool engine_blocks = engine.ShouldBlock(GURL(url), GURL(page), type);
    const bool index_blocks = index.ShouldBlock(GURL(url), GURL(page), type);
    EXPECT_EQ(engine_blocks, index_blocks) << url;
    const std::optional<uint16_t> engine_redirect =
        engine.ResolveRedirect(GURL(url), GURL(page), type);
    const std::optional<uint16_t> index_redirect =
        index.ResolveRedirect(GURL(url), GURL(page), type);
    EXPECT_EQ(engine_redirect, index_redirect) << url;
    if (!engine_blocks) {
      return std::nullopt;
    }
    return engine_redirect.value_or(0);
  }

  AdblockFilterEngine engine;
  std::vector<uint8_t> blob;
  AdblockZIndex index;
};

TEST(AdblockRedirectRulesTest, RedirectBlocksAndNamesItsStandIn) {
  Rules r("||ads.example/a.js$script,redirect=noopjs\n");
  EXPECT_EQ(r.Verdict("https://ads.example/a.js"), Id("noop.js"));
  EXPECT_EQ(r.Verdict("https://ads.example/other.css"), std::nullopt);
  EXPECT_EQ(r.engine.redirect_rule_count(), 1u);
  EXPECT_EQ(r.engine.block_rule_count(), 1u) << "the block half";
}

TEST(AdblockRedirectRulesTest, RedirectRuleBlocksNothingItself) {
  Rules r("||ads.example/a.js$script,redirect-rule=noopjs\n");
  EXPECT_EQ(r.Verdict("https://ads.example/a.js"), std::nullopt)
      << "nothing else blocks it";
  EXPECT_EQ(r.engine.block_rule_count(), 0u);
  EXPECT_EQ(r.engine.redirect_rule_count(), 1u);
}

TEST(AdblockRedirectRulesTest, RedirectRuleServesARequestSomethingElseBlocks) {
  Rules r("||ads.example^\n||ads.example/a.js$script,redirect-rule=noopjs\n");
  EXPECT_EQ(r.Verdict("https://ads.example/a.js"), Id("noop.js"));
  EXPECT_EQ(r.Verdict("https://ads.example/b.js"), 0)
      << "blocked, and no directive names a stand-in for it";
  EXPECT_EQ(r.Verdict("https://ads.example/a.js", kTypeImage), 0)
      << "the directive is for scripts";
}

TEST(AdblockRedirectRulesTest, TheHighestPriorityWins) {
  Rules r(
      "||ads.example/a.js$script,redirect=noopjs\n"
      "||ads.example/a.js$script,redirect=google-ima.js:5\n"
      "||ads.example/a.js$script,redirect=noop.txt:-1\n");
  EXPECT_EQ(r.Verdict("https://ads.example/a.js"), Id("google-ima.js"));
}

TEST(AdblockRedirectRulesTest, ANamedExceptionCancelsOnlyThatStandIn) {
  Rules r(
      "||ads.example/a.js$script,redirect=google-ima.js:5\n"
      "||ads.example/a.js$script,redirect=noopjs\n"
      "@@||ads.example/a.js$script,redirect-rule=google-ima.js\n");
  EXPECT_EQ(r.Verdict("https://ads.example/a.js"), Id("noop.js"))
      << "the next best, not a plain block";
}

TEST(AdblockRedirectRulesTest, ANamelessExceptionCancelsEveryStandIn) {
  Rules r(
      "||ads.example/a.js$script,redirect=noopjs\n"
      "@@||ads.example/a.js$script,redirect-rule\n");
  EXPECT_EQ(r.Verdict("https://ads.example/a.js"), 0)
      << "still blocked: an exception for the redirect never unblocks";
}

TEST(AdblockRedirectRulesTest, ARedirectExceptionDoesNotUnblock) {
  Rules r(
      "||ads.example^\n"
      "@@||ads.example/a.js$redirect-rule=noopjs\n");
  EXPECT_EQ(r.Verdict("https://ads.example/a.js"), 0);
  EXPECT_EQ(r.engine.exception_rule_count(), 0u);
}

TEST(AdblockRedirectRulesTest, AnOrdinaryExceptionStillUnblocksARedirectRule) {
  Rules r(
      "||ads.example/a.js$script,redirect=noopjs\n"
      "@@||ads.example/a.js$script\n");
  EXPECT_EQ(r.Verdict("https://ads.example/a.js"), std::nullopt);
}

TEST(AdblockRedirectRulesTest, OptionsScopeTheDirectiveLikeAnyRule) {
  Rules r(
      "||ads.example/a.js$script,3p,domain=news.example,redirect=noopjs\n");
  EXPECT_EQ(r.Verdict("https://ads.example/a.js"), Id("noop.js"));
  EXPECT_EQ(r.Verdict("https://ads.example/a.js", kTypeScript,
                      "https://other.example/"),
            std::nullopt);
  EXPECT_EQ(r.Verdict("https://news.example/a.js"), std::nullopt) << "1st party";
}

TEST(AdblockRedirectRulesTest, ABlockedRequestOfAnotherShapeGetsNoStandIn) {
  // Untyped: it blocks anything, and the picture can only answer a picture.
  Rules r("||ads.example/pixel$redirect=1x1.gif\n");
  EXPECT_EQ(r.Verdict("https://ads.example/pixel", kTypeImage), Id("1x1.gif"));
  EXPECT_EQ(r.Verdict("https://ads.example/pixel", kTypeScript), 0);
}

TEST(AdblockRedirectRulesTest, NoneIsAPlainBlock) {
  Rules r("||ads.example/a.js$script,redirect=none\n");
  EXPECT_EQ(r.Verdict("https://ads.example/a.js"), 0);
  EXPECT_EQ(r.engine.redirect_rule_count(), 0u);
}

TEST(AdblockRedirectRulesTest, LinesItCannotActOnAreDropped) {
  AdblockFilterEngine engine;
  EXPECT_EQ(engine.AddRules(
                "||a.example/x.js$script,redirect=no-such-resource\n"
                "||b.example/x.js$script,redirect=noopjs:abc\n"
                "||c.example/x.js$script,~redirect=noopjs\n"
                "||d.example/x.js$script,redirect-rule=none\n"
                "||e.example/x.js$script,redirect-rule\n"
                "@@||f.example/x.js$script,redirect=noopjs\n"),
            0u);
  EXPECT_EQ(engine.block_rule_count(), 0u);
  EXPECT_EQ(engine.redirect_rule_count(), 0u);
}

TEST(AdblockRedirectRulesTest, ARegexRuleWithRedirectBlocksButServesNoStandIn) {
  AdblockFilterEngine engine;
  EXPECT_EQ(engine.AddRules("/\\/ad[0-9]\\.js/$script,redirect=noopjs\n"), 1u);
  ASSERT_EQ(engine.regex_rules().size(), 1u);
  EXPECT_EQ(engine.regex_rules()[0].redirect_resource, kNoRedirectResource);
  // A directive on a regex cannot be carried; the standalone form is refused.
  EXPECT_EQ(engine.AddRules("/\\/ad[0-9]\\.js/$script,redirect-rule=noopjs\n"), 0u);
}

TEST(AdblockRedirectRulesTest, TheIndexAgreesOnAMixedSetOfRules) {
  Rules r(
      "||a.example^$script,redirect=noopjs\n"
      "||a.example/video.js$script,redirect=google-ima.js:10\n"
      "||b.example^$image,redirect=1x1.gif\n"
      "||c.example^\n"
      "||c.example/ga.js$script,redirect-rule=google-analytics.com/ga.js\n"
      "@@||c.example/ok.js$script\n"
      "@@||a.example/safe.js$script,redirect-rule\n");
  for (const char* url :
       {"https://a.example/x.js", "https://a.example/video.js",
        "https://a.example/safe.js", "https://b.example/p.gif",
        "https://c.example/ga.js", "https://c.example/ok.js",
        "https://c.example/other", "https://d.example/x.js"}) {
    for (ResourceType type : {kTypeScript, kTypeImage, kTypeXhr, kTypeSubdocument}) {
      r.Verdict(url, type);  // Verdict() checks engine == index
    }
  }
  EXPECT_EQ(r.Verdict("https://a.example/video.js"), Id("google-ima.js"));
  EXPECT_EQ(r.Verdict("https://a.example/safe.js"), 0);
  EXPECT_EQ(r.Verdict("https://b.example/p.gif", kTypeImage), Id("1x1.gif"));
  EXPECT_EQ(r.Verdict("https://c.example/ga.js"), Id("google-analytics_ga.js"));
}

// --- The shipped lists ---------------------------------------------------------

// Every stand-in a rule of the shipped lists names must exist: an unknown name
// drops the rule, and a dropped rule lets its request through.
TEST(AdblockRedirectRulesTest, EveryNameInTheShippedListsResolves) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  const std::optional<std::string> list_path = env->GetVar("ZEPHYRUS_H2H_LIST");
  if (!list_path) {
    GTEST_SKIP() << "set ZEPHYRUS_H2H_LIST to run";
  }
  std::string list;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath::FromUTF8Unsafe(*list_path),
                                     &list));
  list = PreprocessFilterList(list);
  size_t lines = 0;
  size_t unknown = 0;
  std::set<std::string> missing;
  for (const std::string_view line : base::SplitStringPiece(
           list, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (line.starts_with('!') || line.find("redirect") == std::string_view::npos) {
      continue;
    }
    for (const char* key : {",redirect=", "$redirect=", ",redirect-rule=",
                            "$redirect-rule="}) {
      const size_t at = line.find(key);
      if (at == std::string_view::npos) {
        continue;
      }
      std::string_view value = line.substr(at + std::string_view(key).size());
      value = value.substr(0, value.find_first_of(",: "));
      ++lines;
      if (value != "none" && !FindRedirectResource(value)) {
        ++unknown;
        missing.insert(std::string(value));
      }
    }
  }
  std::printf("  redirect options in the list: %zu, with an unknown name: %zu\n",
              lines, unknown);
  for (const std::string& name : missing) {
    ADD_FAILURE() << "no stand-in named " << name;
  }
  EXPECT_GT(lines, 1000u);

  // And the engine acts on them: how many directives and block halves it holds.
  AdblockFilterEngine engine;
  engine.AddRules(list);
  std::printf("  engine: %zu redirect directives\n", engine.redirect_rule_count());
  EXPECT_GT(engine.redirect_rule_count(), 1000u);
}

}  // namespace
}  // namespace zephyrus_adblock
