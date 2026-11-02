// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

// AdblockZIndex must say exactly what AdblockFilterEngine says. These tests
// build the index from the engine's own parsed rules and compare the two:
//  * on small hand-written rule sets that exercise each kind of rule, with an
//    absolute expectation as well, so "both wrong the same way" cannot pass;
//  * on probes derived from EVERY rule of the shipped list, with the real
//    GURL and the real public-suffix list;
//  * on corrupted blobs, which must be rejected or used without crashing.

#include "chrome/browser/zephyrus/adblock/adblock_zindex.h"

#include <algorithm>
#include <map>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/environment.h"
#include "base/strings/string_split.h"
#include "base/path_service.h"
#include "base/time/time.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_param_filter.h"
#include "chrome/browser/zephyrus/adblock/adblock_regex_matcher.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include <psapi.h>
#endif

namespace zephyrus_adblock {
namespace {

using RuleGroup = AdblockFilterEngine::RuleGroup;

// An engine and the index built from it, side by side.
struct Pair {
  explicit Pair(const std::string& rules) {
    engine.AddRules(rules);
    AdblockZIndexBuilder builder;
    engine.ForEachRule(
        [&](RuleGroup group, const FilterRule& rule, std::string_view) {
          builder.Add(group, rule);
        });
    std::optional<std::vector<uint8_t>> built = builder.Finish();
    EXPECT_TRUE(built.has_value());
    if (built) {
      blob = std::move(*built);
    }
    EXPECT_TRUE(index.Load(blob));
  }

  bool EngineBlocks(const std::string& url,
                    const std::string& page,
                    ResourceType type) const {
    return engine.ShouldBlock(GURL(url), GURL(page), type);
  }
  bool IndexBlocks(const std::string& url,
                   const std::string& page,
                   ResourceType type) const {
    return index.ShouldBlock(GURL(url), GURL(page), type);
  }

  // Both agree, and both say `expected`.
  void Expect(bool expected,
              const std::string& url,
              const std::string& page = "https://news.example.org/",
              ResourceType type = kTypeScript) const {
    EXPECT_EQ(EngineBlocks(url, page, type), expected)
        << "ENGINE " << url << " on " << page;
    EXPECT_EQ(IndexBlocks(url, page, type), expected)
        << "INDEX " << url << " on " << page;
  }

  AdblockFilterEngine engine;
  std::vector<uint8_t> blob;
  AdblockZIndex index;
};

TEST(AdblockZIndexTest, HostnameAnchorsAndSeparators) {
  Pair p("||ads.example^\n||tracker.example/pixel^\n");
  p.Expect(true, "https://ads.example/x.js");
  p.Expect(true, "https://sub.ads.example/x.js");
  p.Expect(false, "https://notads.example/x.js");
  p.Expect(false, "https://ads.example.evil/x.js");
  p.Expect(true, "https://tracker.example/pixel?id=1");
  p.Expect(false, "https://tracker.example/pixels");
}

TEST(AdblockZIndexTest, WildcardsAndAnchors) {
  Pair p("/banner/*/img^\n|https://start.example/ad\n.swf|\n");
  p.Expect(true, "https://cdn.example/banner/300x250/img?x=1");
  p.Expect(false, "https://cdn.example/banner/300x250/imgs");
  p.Expect(true, "https://start.example/ad/1");
  p.Expect(false, "https://other.example/?u=https://start.example/ad");
  p.Expect(true, "https://cdn.example/movie.swf");
  p.Expect(false, "https://cdn.example/movie.swf?x=1");
}

TEST(AdblockZIndexTest, ExceptionsAndImportant) {
  Pair p(
      "||ads.example^\n@@||ads.example/allowed^\n"
      "||strong.example^$important\n@@||strong.example^\n");
  p.Expect(true, "https://ads.example/x.js");
  p.Expect(false, "https://ads.example/allowed/x.js");
  p.Expect(true, "https://strong.example/x.js");
}

TEST(AdblockZIndexTest, TypesAndParty) {
  Pair p(
      "||img.example^$image\n||tp.example^$third-party\n"
      "||fp.example^$~third-party\n||notscript.example^$~script\n");
  p.Expect(true, "https://img.example/a.png", "https://news.example.org/",
           kTypeImage);
  p.Expect(false, "https://img.example/a.js", "https://news.example.org/",
           kTypeScript);
  p.Expect(true, "https://tp.example/a.js", "https://news.example.org/");
  p.Expect(false, "https://tp.example/a.js", "https://tp.example/");
  p.Expect(true, "https://fp.example/a.js", "https://fp.example/");
  p.Expect(false, "https://fp.example/a.js", "https://news.example.org/");
  p.Expect(false, "https://notscript.example/a.js", "https://news.example.org/",
           kTypeScript);
  p.Expect(true, "https://notscript.example/a.png", "https://news.example.org/",
           kTypeImage);
}

TEST(AdblockZIndexTest, DomainOptionsAndDenyallow) {
  Pair p(
      "/ads.js$domain=a.example|~b.a.example\n"
      "||cdn.example^$denyallow=ok.example\n"
      "*$script,3p,domain=entity.*\n");
  p.Expect(true, "https://x.example/ads.js", "https://a.example/");
  p.Expect(true, "https://x.example/ads.js", "https://www.a.example/");
  p.Expect(false, "https://x.example/ads.js", "https://b.a.example/");
  p.Expect(false, "https://x.example/ads.js", "https://other.example/");
  p.Expect(true, "https://cdn.example/a.js");
  p.Expect(false, "https://ok.example/a.js");
  // An entity rule with no literal to key on: found through the initiator.
  p.Expect(true, "https://anything.example/a.js", "https://entity.co.uk/");
  p.Expect(true, "https://anything.example/a.js", "https://www.entity.com/");
  p.Expect(false, "https://anything.example/a.js", "https://entity.example.org/x");
}

TEST(AdblockZIndexTest, ToScopesByDestinationHost) {
  // Same cases as the engine's own tests, through the compiled index; Pair
  // checks both agree and both say what is expected.
  Pair p(
      "/c/*?s1=$doc,to=landing.example\n"
      "/d/*?s2=$doc,to=example|~safe.example\n"
      "/ad.js$script,domain=news.example,to=cdn.example\n"
      "||ads.example^\n@@||ads.example^$to=ok.ads.example\n");
  const std::string page = "https://news.example.org/";
  p.Expect(true, "https://landing.example/c/x?s1=1", page, kTypeDocument);
  p.Expect(true, "https://sub.landing.example/c/x?s1=1", page, kTypeDocument);
  p.Expect(false, "https://other.example/c/x?s1=1", page, kTypeDocument);
  p.Expect(false, "https://notlanding.example/c/x?s1=1", page, kTypeDocument);
  p.Expect(true, "https://a.example/d/x?s2=1", page, kTypeDocument);
  p.Expect(false, "https://safe.example/d/x?s2=1", page, kTypeDocument);
  p.Expect(false, "https://www.safe.example/d/x?s2=1", page, kTypeDocument);
  p.Expect(true, "https://cdn.example/ad.js", "https://news.example/");
  p.Expect(false, "https://other.example/ad.js", "https://news.example/");
  p.Expect(false, "https://cdn.example/ad.js", "https://elsewhere.example/");
  p.Expect(true, "https://ads.example/a.js");
  p.Expect(false, "https://ok.ads.example/a.js");
}

TEST(AdblockZIndexTest, DocumentExceptionsAndPageLevelBlocks) {
  Pair p(
      "||ads.example^\n@@||site.example^$document\n"
      "@@||nohide.example^$elemhide\n||blockedpage.example^\n");
  p.Expect(false, "https://ads.example/a.js", "https://site.example/page");
  p.Expect(true, "https://ads.example/a.js", "https://elsewhere.example/");
  EXPECT_EQ(p.engine.GetDocumentExceptions(GURL("https://nohide.example/p")),
            p.index.GetDocumentExceptions(GURL("https://nohide.example/p")));
  EXPECT_NE(p.index.GetDocumentExceptions(GURL("https://nohide.example/p")),
            0u);
  EXPECT_EQ(p.index.GetDocumentExceptions(GURL("https://plain.example/p")), 0u);
  // A pure-hostname rule may block a whole page; a path rule may not.
  p.Expect(true, "https://blockedpage.example/", "https://news.example.org/",
           kTypeDocument);
}

TEST(AdblockZIndexTest, AMatchDeepInALongUrlIsNotMissed) {
  // The first version of the prototype kept at most 96 tokens of a URL and
  // missed a rule keyed after them.
  // Not `/.../`: a rule that starts and ends with a slash is a regex, which the
  // engine does not support yet.
  Pair p("/needle-in-the-haystack^\n");
  std::string url = "https://site.example/";
  for (int i = 0; i < 400; ++i) {
    url += "abc" + std::to_string(i) + "/";
  }
  url += "needle-in-the-haystack/x";
  p.Expect(true, url);
}

TEST(AdblockZIndexTest, NothingLoadedMeansNothingBlocked) {
  AdblockZIndex empty;
  EXPECT_FALSE(
      empty.ShouldBlock(GURL("https://ads.example/"), GURL(), kTypeScript));
  EXPECT_EQ(empty.GetDocumentExceptions(GURL("https://x.example/")), 0u);
}

// --- Against the shipped list -----------------------------------------------

std::string ReadShippedFilterList() {
  base::FilePath root;
  if (!base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root)) {
    return std::string();
  }
  const base::FilePath path = root.AppendASCII("chrome")
                                  .AppendASCII("browser")
                                  .AppendASCII("zephyrus")
                                  .AppendASCII("adblock")
                                  .AppendASCII("lists")
                                  .AppendASCII("zephyrus_filters.txt");
  std::string text;
  if (!base::ReadFileToString(path, &text)) {
    return std::string();
  }
  return text;
}

// Private bytes of this process, or 0 where it cannot be read.
size_t PrivateBytes() {
#if BUILDFLAG(IS_WIN)
  PROCESS_MEMORY_COUNTERS_EX counters = {};
  counters.cb = sizeof(counters);
  if (K32GetProcessMemoryInfo(
          GetCurrentProcess(),
          reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
          sizeof(counters))) {
    return counters.PrivateUsage;
  }
#endif
  return 0;
}

// A URL that `rule` should match.
std::string Synthesize(const FilterRule& r) {
  std::string body;
  for (const char c : r.pattern) {
    if (c == '*') {
      body += "xq7";
    } else if (c == '^') {
      body += "/";
    } else {
      body += c;
    }
  }
  if (r.anchor == FilterRule::kAnchorHostname) {
    if (body.find('/') == std::string::npos) {
      body += "/";
    }
    return "https://" + body;
  }
  if (r.anchor == FilterRule::kAnchorStart) {
    return body.rfind("http", 0) == 0 ? body : "https://" + body;
  }
  // A rule limited by $to= only matches a request to one of those hosts, so the
  // probe goes to the first.
  const std::string host =
      r.to_included.empty() ? "site.example" : r.to_included[0];
  return "https://" + host + "/" + body;
}

std::string OriginOf(const std::string& url) {
  const size_t scheme = url.find("://");
  const size_t end = url.find('/', scheme + 3);
  return url.substr(0, end == std::string::npos ? url.size() : end) + "/";
}

// An initiator page chosen to satisfy the rule's own domain and party options.
std::string InitiatorFor(const FilterRule& r, const std::string& url) {
  if (!r.domains_included.empty()) {
    std::string d = r.domains_included[0];
    if (d.size() > 2 && d.compare(d.size() - 2, 2, ".*") == 0) {
      d = d.substr(0, d.size() - 2) + ".com";
    }
    return "https://" + d + "/";
  }
  if (r.party == 2) {
    return "https://third-party.test/";
  }
  if (r.party == 1) {
    return OriginOf(url);
  }
  return "https://news.example.org/";
}

// Probes derived from EVERY rule of `list`, run through the engine and the index.
void CheckAgreementOnEveryRule(const std::string& list) {
  const size_t private_before = PrivateBytes();
  auto engine = std::make_unique<AdblockFilterEngine>();
  ASSERT_GT(engine->AddRules(list), 50000u);
  const size_t engine_private = PrivateBytes() - private_before;

  std::vector<std::pair<RuleGroup, FilterRule>> all;
  AdblockZIndexBuilder builder;
  engine->ForEachRule(
      [&](RuleGroup group, const FilterRule& rule, std::string_view) {
        builder.Add(group, rule);
        all.emplace_back(group, rule);
      });
  const base::TimeTicks build_start = base::TimeTicks::Now();
  std::optional<std::vector<uint8_t>> built = builder.Finish();
  const base::TimeDelta build_time = base::TimeTicks::Now() - build_start;
  ASSERT_TRUE(built.has_value());
  AdblockZIndex index;
  const size_t private_blob_before = PrivateBytes();
  const base::TimeTicks load_start = base::TimeTicks::Now();
  ASSERT_TRUE(index.Load(*built));
  const base::TimeDelta load_time = base::TimeTicks::Now() - load_start;
  (void)private_blob_before;

  size_t probes = 0;
  size_t mismatches = 0;
  size_t blocked = 0;
  base::TimeDelta engine_time;
  base::TimeDelta index_time;
  auto compare = [&](const std::string& url, const std::string& initiator,
                     ResourceType type) {
    const GURL u(url);
    const GURL i(initiator);
    const base::TimeTicks t0 = base::TimeTicks::Now();
    const bool a = engine->ShouldBlock(u, i, type);
    const base::TimeTicks t1 = base::TimeTicks::Now();
    const bool b = index.ShouldBlock(u, i, type);
    const base::TimeTicks t2 = base::TimeTicks::Now();
    engine_time += t1 - t0;
    index_time += t2 - t1;
    ++probes;
    blocked += a;
    if (a != b) {
      if (++mismatches <= 20) {
        ADD_FAILURE() << "engine=" << a << " index=" << b << " url=" << url
                      << " initiator=" << initiator << " type=" << type;
      }
    }
  };

  std::mt19937 rng(99);
  const ResourceType kTypes[] = {kTypeScript, kTypeImage, kTypeXhr,
                                 kTypeSubdocument, kTypeOther};
  std::string junk;
  for (int k = 0; k < 150; ++k) {
    junk += "abc" + std::to_string(100 + k) + "/";
  }
  size_t document_checked = 0;
  size_t document_mismatches = 0;
  for (const auto& [group, rule] : all) {
    const std::string u = Synthesize(rule);
    const ResourceType type =
        rule.type_mask ? static_cast<ResourceType>(rule.type_mask &
                                                   -static_cast<int>(rule.type_mask))
                       : kTypes[rng() % 5];
    compare(u, InitiatorFor(rule, u), type);        // the rule's own request
    compare(u, OriginOf(u), type);                  // a first-party initiator
    compare(u, InitiatorFor(rule, u), kTypes[rng() % 5]);  // another type
    std::string mutated = u;
    if (mutated.size() > 12) {
      const size_t at = 8 + rng() % (mutated.size() - 8);
      mutated[at] = mutated[at] == 'a' ? 'b' : 'a';
    }
    compare(mutated, InitiatorFor(rule, u), type);  // one character off
    compare(u + "?q=1&utm=2", InitiatorFor(rule, u), type);
    if (rule.anchor == FilterRule::kAnchorNone) {   // match after 150 tokens
      const size_t path_at = u.find('/', std::string_view("https://").size()) + 1;
      compare(u.substr(0, path_at) + junk + u.substr(path_at),
              InitiatorFor(rule, u), type);
    }
    if (group == RuleGroup::kDocument) {
      ++document_checked;
      document_mismatches += engine->GetDocumentExceptions(GURL(u)) !=
                             index.GetDocumentExceptions(GURL(u));
    }
  }
  EXPECT_EQ(mismatches, 0u);
  EXPECT_EQ(document_mismatches, 0u);
  EXPECT_GT(blocked, probes / 10) << "the probes should hit real rules";

  std::printf(
      "\n=== AdblockZIndex vs AdblockFilterEngine, shipped list ===\n"
      "  rules               : %zu\n"
      "  engine memory       : %.1f MB private, per profile\n"
      "  blob                : %.2f MB (built in %.0f ms, loaded in %.2f ms)\n"
      "  probes              : %zu (%zu blocked by the engine)\n"
      "  mismatches          : %zu (document exceptions: %zu of %zu)\n"
      "  mean per probe      : engine %.2f us, index %.2f us (%.1fx)\n\n",
      all.size(), engine_private / 1e6, built->size() / 1e6,
      build_time.InMillisecondsF(),
      load_time.InMillisecondsF(), probes, blocked, mismatches,
      document_mismatches, document_checked,
      engine_time.InMicrosecondsF() / probes,
      index_time.InMicrosecondsF() / probes,
      engine_time.InMicrosecondsF() / index_time.InMicrosecondsF());
}

TEST(AdblockZIndexTest, AgreesWithTheEngineOnEveryRuleOfTheShippedList) {
  const std::string list = ReadShippedFilterList();
  ASSERT_FALSE(list.empty()) << "could not read the shipped zephyrus_filters.txt";
  CheckAgreementOnEveryRule(list);
}

// The list the browser actually runs after its first update: the shipped one plus
// the uBlock Origin and regional lists, which use options the shipped one does
// not ($to=, $from=...). Point ZEPHYRUS_FULL_LIST at the downloaded filters.txt.
TEST(AdblockZIndexTest, AgreesWithTheEngineOnEveryRuleOfTheFullList) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  const std::optional<std::string> path = env->GetVar("ZEPHYRUS_FULL_LIST");
  if (!path) {
    GTEST_SKIP() << "set ZEPHYRUS_FULL_LIST to the downloaded filters.txt";
  }
  std::string list;
  ASSERT_TRUE(
      base::ReadFileToString(base::FilePath::FromUTF8Unsafe(*path), &list));
  CheckAgreementOnEveryRule(PreprocessFilterList(list));
}

TEST(AdblockZIndexTest, LatencyOnARealisticPage) {
  const std::string list = ReadShippedFilterList();
  ASSERT_FALSE(list.empty());
  Pair p(list);
  // The shapes of one page load: trackers, long query strings, ordinary assets
  // and requests that match nothing. The same mix as adblock_matcher_perftest.
  const char* const kUrls[] = {
      "https://www.google-analytics.com/analytics.js",
      "https://www.googletagmanager.com/gtm.js?id=GTM-ABCDEF",
      "https://connect.facebook.net/en_US/fbevents.js",
      "https://analytics.tiktok.com/i18n/pixel/events.js",
      "https://static.doubleclick.net/instream/ad_status.js",
      "https://px.ads.linkedin.com/collect?pid=12345&fmt=gif",
      "https://tracking.example.com/pixel?utm_source=news&utm_medium=cpc"
      "&utm_campaign=spring_2026_brand_awareness&utm_content=variant_b"
      "&sid=8f3a2c1e9d7b&ts=1787500000"
      "&ref=https%3A%2F%2Fwww.example.org%2Fsome%2Fdeep%2Fpath",
      "https://example.com/static/js/main.4f8a2b1c.chunk.js",
      "https://fonts.gstatic.com/s/roboto/v30/KFOmCnqEu92Fr1Mu4mxK.woff2",
      "https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js",
      "https://images.example.com/uploads/2026/08/photo-1920x1080.webp",
      "https://api.example.com/v2/articles?section=world&limit=20&offset=40",
      "https://i.ytimg.com/vi/dQw4w9WgXcQ/maxresdefault.jpg",
      "https://entirely-unlisted-host-9f2a.example/asset/path/file.bin"};
  const GURL page("https://www.example.com/article/index.html");
  std::vector<GURL> urls;
  for (const char* u : kUrls) {
    urls.emplace_back(u);
  }
  size_t blocked = 0;
  for (const GURL& u : urls) {  // agreement first, and warm both
    const bool a = p.engine.ShouldBlock(u, page, kTypeScript);
    EXPECT_EQ(a, p.index.ShouldBlock(u, page, kTypeScript)) << u;
    blocked += a;
  }
  constexpr int kRounds = 400;
  volatile size_t sink = 0;
  base::TimeTicks t0 = base::TimeTicks::Now();
  for (int r = 0; r < kRounds; ++r) {
    for (const GURL& u : urls) {
      sink = sink + p.engine.ShouldBlock(u, page, kTypeScript);
    }
  }
  const base::TimeDelta engine_time = base::TimeTicks::Now() - t0;
  t0 = base::TimeTicks::Now();
  for (int r = 0; r < kRounds; ++r) {
    for (const GURL& u : urls) {
      sink = sink + p.index.ShouldBlock(u, page, kTypeScript);
    }
  }
  const base::TimeDelta index_time = base::TimeTicks::Now() - t0;
  const double n = static_cast<double>(kRounds) * urls.size();
  std::printf(
      "\n=== One realistic page (%zu urls, %zu blocked) ===\n"
      "  engine : %.2f us per request\n"
      "  index  : %.2f us per request (%.1fx)\n\n",
      urls.size(), blocked, engine_time.InMicrosecondsF() / n,
      index_time.InMicrosecondsF() / n,
      engine_time.InMicrosecondsF() / index_time.InMicrosecondsF());
}

// --- Head to head with another engine -----------------------------------------
//
// Not a unit test: it measures. With ZEPHYRUS_H2H_LIST (a filter list) and
// ZEPHYRUS_H2H_CORPUS (url, page, type per line: real recorded traffic) set, it
// writes the preprocessed list and both matchers' decisions into
// ZEPHYRUS_H2H_OUT and prints their latency, for comparison with another
// engine on the SAME list and the SAME requests (see research/brave_compare,
// bin_h2h.rs). Skipped otherwise.

ResourceType TypeFromName(std::string_view name) {
  if (name == "script") return kTypeScript;
  if (name == "image") return kTypeImage;
  if (name == "stylesheet") return kTypeStylesheet;
  if (name == "font") return kTypeFont;
  if (name == "media") return kTypeMedia;
  if (name == "xhr") return kTypeXhr;
  if (name == "document") return kTypeDocument;
  if (name == "subdocument") return kTypeSubdocument;
  if (name == "popup") return kTypePopup;
  if (name == "ping") return kTypePing;
  if (name == "websocket") return kTypeWebsocket;
  if (name == "object") return kTypeObject;
  return kTypeOther;
}

TEST(AdblockZIndexTest, HeadToHeadCorpus) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  const std::optional<std::string> list_path = env->GetVar("ZEPHYRUS_H2H_LIST");
  const std::optional<std::string> corpus_path = env->GetVar("ZEPHYRUS_H2H_CORPUS");
  const std::optional<std::string> out_dir = env->GetVar("ZEPHYRUS_H2H_OUT");
  if (!list_path || !corpus_path || !out_dir) {
    GTEST_SKIP() << "set ZEPHYRUS_H2H_LIST, _CORPUS and _OUT to run";
  }
  std::string list;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath::FromUTF8Unsafe(*list_path),
                                     &list));
  list = PreprocessFilterList(list);
  const base::FilePath out = base::FilePath::FromUTF8Unsafe(*out_dir);
  ASSERT_TRUE(base::WriteFile(out.AppendASCII("preprocessed.txt"), list));
  std::string corpus_text;
  ASSERT_TRUE(base::ReadFileToString(
      base::FilePath::FromUTF8Unsafe(*corpus_path), &corpus_text));

  struct Row {
    GURL url;
    GURL page;
    ResourceType type;
  };
  std::vector<Row> rows;
  for (const std::string_view line : base::SplitStringPiece(
           corpus_text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const std::vector<std::string_view> cols = base::SplitStringPiece(
        line, "\t", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    if (cols.size() < 3) {
      continue;
    }
    rows.push_back({GURL(cols[0]), GURL(cols[1]), TypeFromName(cols[2])});
  }
  ASSERT_GT(rows.size(), 100u);

  AdblockFilterEngine engine;
  engine.AddRules(list);
  AdblockZIndexBuilder builder;
  engine.ForEachRule(
      [&](RuleGroup group, const FilterRule& rule, std::string_view) {
        builder.Add(group, rule);
      });
  std::optional<std::vector<uint8_t>> blob = builder.Finish();
  ASSERT_TRUE(blob.has_value());
  AdblockZIndex index;
  ASSERT_TRUE(index.Load(*blob));

  auto measure = [&](const std::string& name, auto&& decide,
                     const std::string& file) {
    for (const Row& r : rows) {  // warm up
      decide(r);
    }
    std::vector<double> micros;
    std::string decisions;
    size_t blocked = 0;
    for (int pass = 0; pass < 3; ++pass) {
      for (const Row& r : rows) {
        const base::TimeTicks t0 = base::TimeTicks::Now();
        const bool b = decide(r);
        micros.push_back((base::TimeTicks::Now() - t0).InMicrosecondsF());
        if (pass == 0) {
          decisions += b ? "1\n" : "0\n";
          blocked += b;
        }
      }
    }
    std::sort(micros.begin(), micros.end());
    double sum = 0;
    for (double m : micros) {
      sum += m;
    }
    std::printf(
        "  %-26s mean %6.2f us  p50 %6.2f  p99 %7.2f  p99.9 %8.2f | blocked %zu of %zu (%.1f%%)\n",
        name.c_str(), sum / micros.size(), micros[micros.size() / 2],
        micros[micros.size() * 99 / 100], micros[micros.size() * 999 / 1000],
        blocked, rows.size(), 100.0 * blocked / rows.size());
    ASSERT_TRUE(base::WriteFile(out.AppendASCII(file), decisions));
  };
  std::printf("\n=== head to head: %zu real requests, %.2f MB list ===\n",
              rows.size(), list.size() / 1e6);
  measure("Zephyrus engine (before)",
          [&](const Row& r) { return engine.ShouldBlock(r.url, r.page, r.type); },
          "zeph_engine.txt");
  measure("Zephyrus compiled index",
          [&](const Row& r) { return index.ShouldBlock(r.url, r.page, r.type); },
          "zeph_index.txt");
  std::printf("  index blob %.2f MB\n\n", blob->size() / 1e6);

  // The /regex/ rules: compile cost, then what they add to every request.
  const base::TimeTicks r0 = base::TimeTicks::Now();
  scoped_refptr<const AdblockRegexMatcher> regex =
      AdblockRegexMatcher::Build(engine);
  const base::TimeDelta regex_build = base::TimeTicks::Now() - r0;
  std::printf(
      "  regex rules: %zu compiled (%zu checked by ICU), %zu skipped, %zu "
      "without a prefilter | built in %.1f ms\n",
      regex->rule_count(), regex->verified_count(), regex->skipped_count(),
      regex->unfiltered_count(), regex_build.InMillisecondsF());
  measure("regex step alone",
          [&](const Row& r) {
            const RegexHits h = regex->Match(r.url, r.page, r.type);
            return h.block || h.important;
          },
          "zeph_regex_only.txt");
  measure("Zephyrus index + regex",
          [&](const Row& r) {
            const RegexHits h = regex->Match(r.url, r.page, r.type);
            return index.ShouldBlock(r.url, r.page, r.type, &h);
          },
          "zeph_index_regex.txt");
  measure("Zephyrus engine + regex",
          [&](const Row& r) {
            const RegexHits h = regex->Match(r.url, r.page, r.type);
            return engine.ShouldBlock(r.url, r.page, r.type, &h);
          },
          "zeph_engine_regex.txt");

  // Tracking-parameter removal on the same requests: what it costs, and what it
  // would take off these real pages.
  const base::TimeTicks p0 = base::TimeTicks::Now();
  AdblockParamFilter params;
  params.AddBuiltInRules();
  params.AddRules(list);
  const base::TimeDelta param_build = base::TimeTicks::Now() - p0;
  size_t with_query = 0;
  size_t stripped = 0;
  std::map<std::string, int> removed_names;
  std::vector<std::string> examples;
  std::vector<double> micros;
  for (int pass = 0; pass < 3; ++pass) {
    for (const Row& r : rows) {
      // A blocked request never gets as far as being cleaned.
      if (index.ShouldBlock(r.url, r.page, r.type)) {
        continue;
      }
      const base::TimeTicks t0 = base::TimeTicks::Now();
      const std::optional<GURL> cleaned = params.Strip(r.url, r.page, r.type);
      micros.push_back((base::TimeTicks::Now() - t0).InMicrosecondsF());
      if (pass != 0) {
        continue;
      }
      with_query += r.url.has_query();
      if (!cleaned) {
        continue;
      }
      ++stripped;
      // which parameter names went
      for (const std::string_view piece : base::SplitStringPiece(
               r.url.query(), "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
        const std::string_view name = piece.substr(0, piece.find('='));
        if (cleaned->query().find(piece) == std::string_view::npos) {
          ++removed_names[std::string(name)];
        }
      }
      if (examples.size() < 6) {
        examples.push_back(r.url.spec().substr(0, 150) + "\n      -> " +
                           cleaned->spec().substr(0, 150));
      }
    }
  }
  std::sort(micros.begin(), micros.end());
  double sum = 0;
  for (double m : micros) {
    sum += m;
  }
  std::printf(
      "=== Tracking-parameter removal on the same %zu requests ===\n"
      "  rules %zu (%zu skipped), built in %.1f ms\n"
      "  cost per request: mean %.2f us  p50 %.2f  p99 %.2f  p99.9 %.2f\n"
      "  %zu of %zu requests carry a query; %zu (%.1f%%) had parameters removed\n",
      rows.size(), params.rule_count(), params.skipped_count(),
      param_build.InMillisecondsF(), sum / micros.size(),
      micros[micros.size() / 2], micros[micros.size() * 99 / 100],
      micros[micros.size() * 999 / 1000], with_query, rows.size(), stripped,
      100.0 * stripped / rows.size());
  std::vector<std::pair<int, std::string>> ranked;
  for (const auto& [name, count] : removed_names) {
    ranked.emplace_back(count, name);
  }
  std::sort(ranked.rbegin(), ranked.rend());
  std::printf("  averaged over ALL %zu requests (blocked ones skip it): %.2f us\n",
              rows.size(), sum / 3 / rows.size());
  std::printf("  most removed:");
  for (size_t i = 0; i < ranked.size() && i < 12; ++i) {
    std::printf(" %s x%d", ranked[i].second.c_str(), ranked[i].first);
  }
  std::printf("\n");
  for (const std::string& e : examples) {
    std::printf("    %s\n", e.c_str());
  }
  std::printf("\n");
}

TEST(AdblockZIndexTest, ACorruptedBlobIsRejectedOrHarmless) {
  const std::string list = ReadShippedFilterList();
  ASSERT_FALSE(list.empty());
  AdblockFilterEngine engine;
  engine.AddRules(list);
  AdblockZIndexBuilder builder;
  engine.ForEachRule(
      [&](RuleGroup group, const FilterRule& rule, std::string_view) {
        builder.Add(group, rule);
      });
  std::optional<std::vector<uint8_t>> built = builder.Finish();
  ASSERT_TRUE(built.has_value());

  const std::vector<GURL> urls = {
      GURL("https://www.google-analytics.com/analytics.js"),
      GURL("https://example.com/static/js/main.4f8a2b1c.chunk.js"),
      GURL("https://px.ads.linkedin.com/collect?pid=12345&fmt=gif"),
      GURL("https://entirely-unlisted-host-9f2a.example/asset/path/file.bin")};
  const GURL page("https://www.example.com/article");

  std::mt19937 rng(7);
  int rejected = 0;
  int used = 0;
  for (int round = 0; round < 300; ++round) {
    std::vector<uint8_t> copy = *built;
    for (int flip = 0; flip < 8; ++flip) {
      copy[rng() % copy.size()] ^= 1u << (rng() % 8);
    }
    AdblockZIndex index;
    if (!index.Load(copy)) {
      ++rejected;
      continue;
    }
    ++used;
    // Every access is a bounds-checked span: an out-of-range read is a CHECK
    // failure and fails this test by crashing it.
    for (const GURL& url : urls) {
      index.ShouldBlock(url, page, kTypeScript);
    }
  }
  EXPECT_GT(rejected, 0);
  std::printf("  corrupted blobs: %d rejected, %d loaded and used safely\n",
              rejected, used);
}

}  // namespace
}  // namespace zephyrus_adblock
