// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// How long does ShouldBlock() actually take?
//
// An external audit (2026-08-24) reported the matcher as the fork's biggest
// self-inflicted latency source, on the grounds that it "scans all filter rules
// linearly" — ~50-85k rules with backtracking wildcards, "millions of pattern
// walks per navigation". Reading the code does not support that: AnyRuleMatches
// is a token->rules hash lookup with a linear fallback only for rules that
// yield no keyword, and over the shipped list that fallback is 34 rules out of
// 110,567 (99.97% bucket, median bucket size 1).
//
// But "reading the code does not support it" is an argument, not a measurement,
// and the same audit's own protocol says no speed claims without numbers. So
// this measures the real thing: the real engine, the shipped filter list, and a
// URL corpus shaped like a page load.
//
// The number that matters is p99, not the mean. Matching runs on the UI thread
// (see the class comment on AdblockFilterEngine, and the proxying factory
// created from WillCreateURLLoaderFactory), so a rare slow match is a dropped
// frame, and a mean hides exactly that.
//
// Run:
//   out/Release/unit_tests.exe --gtest_filter=AdblockMatcherPerfTest.*

#include <algorithm>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_adblock {
namespace {

// A page load's worth of subresource requests, deliberately including the
// shapes that would be SLOW if the audit's concern were right:
//   - hosts hitting the largest token buckets (analytics/metrics/tracking,
//     which are the 863/681/563-rule buckets)
//   - long query strings, which is what makes a backtracking walk expensive
//   - URLs that match nothing, so the full candidate set is exhausted before
//     returning false — the true worst case, since a match returns early
// Two deliberately long, query-heavy URLs. Kept out of the array literal so the
// line splits cannot be mistaken for a missing comma.
constexpr char kLongTrackerUrl[] =
    "https://tracking.example.com/pixel?utm_source=news&utm_medium=cpc"
    "&utm_campaign=spring_2026_brand_awareness&utm_content=variant_b"
    "&sid=8f3a2c1e9d7b&ts=1787500000"
    "&ref=https%3A%2F%2Fwww.example.org%2Fsome%2Fdeep%2Fpath";
constexpr char kLongMetricsUrl[] =
    "https://metrics.example.net/collect?v=2&tid=G-XXXX"
    "&cid=1234567890.9876543210&sr=2560x1440&ul=en-gb&de=UTF-8"
    "&dt=A%20Fairly%20Long%20Document%20Title";

const char* const kCorpus[] = {
    // Third-party trackers: expected to block, and land in the hot buckets.
    "https://www.google-analytics.com/analytics.js",
    "https://www.googletagmanager.com/gtm.js?id=GTM-ABCDEF",
    "https://connect.facebook.net/en_US/fbevents.js",
    "https://smetrics.example.com/b/ss/rsid/1/JS-2.22.0",
    "https://analytics.tiktok.com/i18n/pixel/events.js",
    "https://static.doubleclick.net/instream/ad_status.js",
    "https://px.ads.linkedin.com/collect?pid=12345&fmt=gif",
    "https://s.amazon-adsystem.com/iu3?d=generic&ex=visualiq.net",
    // Long query strings — the expensive shape for wildcard walks. Declared
    // above rather than inline: split string literals inside an array
    // initializer look like a missing comma to -Wstring-concatenation.
    kLongTrackerUrl,
    kLongMetricsUrl,
    // First-party and CDN assets: expected NOT to block. These are the common
    // case on a real page and the honest measure of steady-state cost.
    "https://example.com/static/js/main.4f8a2b1c.chunk.js",
    "https://example.com/static/css/app.9d3e7f10.css",
    "https://fonts.gstatic.com/s/roboto/v30/KFOmCnqEu92Fr1Mu4mxK.woff2",
    "https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js",
    "https://images.example.com/uploads/2026/08/photo-1920x1080.webp",
    "https://api.example.com/v2/articles?section=world&limit=20&offset=40",
    "https://github.githubassets.com/assets/app-0a1b2c3d.js",
    "https://i.ytimg.com/vi/dQw4w9WgXcQ/maxresdefault.jpg",
    // Matches nothing at all — worst case, no early exit.
    "https://entirely-unlisted-host-9f2a.example/asset/path/file.bin",
    "https://another-unmatched-host.invalid/a/b/c/d/e/f/g.json",
};

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

TEST(AdblockMatcherPerfTest, ShouldBlockLatencyOverShippedList) {
  const std::string list = ReadShippedFilterList();
  ASSERT_FALSE(list.empty())
      << "could not read the shipped zephyrus_filters.txt; the measurement is "
         "meaningless against a synthetic list, so this fails rather than "
         "reporting a fast number for the wrong input";

  AdblockFilterEngine engine;
  const base::TimeTicks parse_start = base::TimeTicks::Now();
  const size_t parsed = engine.AddRules(list);
  const base::TimeDelta parse_time = base::TimeTicks::Now() - parse_start;

  ASSERT_GT(parsed, 50000u) << "the shipped list should be ~110k rules; a much "
                               "smaller number means parsing changed shape";

  const GURL initiator("https://www.example.com/article/index.html");

  // Warm up: first calls fault in pages and populate caches, and including them
  // would put the cost of the measurement into the measurement.
  for (const char* url : kCorpus) {
    engine.ShouldBlock(GURL(url), initiator, kTypeScript);
  }

  constexpr int kIterations = 200;
  std::vector<double> samples_us;
  samples_us.reserve(kIterations * std::size(kCorpus));

  for (int i = 0; i < kIterations; ++i) {
    for (const char* url : kCorpus) {
      const GURL request(url);
      const base::TimeTicks start = base::TimeTicks::Now();
      const bool blocked = engine.ShouldBlock(request, initiator, kTypeScript);
      const base::TimeDelta elapsed = base::TimeTicks::Now() - start;
      samples_us.push_back(elapsed.InMicrosecondsF());
      // Consume the result so the call cannot be optimised away.
      ASSERT_TRUE(blocked || !blocked);
    }
  }

  std::sort(samples_us.begin(), samples_us.end());
  auto pct = [&](double p) {
    const size_t idx = static_cast<size_t>(p * (samples_us.size() - 1));
    return samples_us[idx];
  };
  double total = 0;
  for (double s : samples_us) {
    total += s;
  }

  printf("\n=== ShouldBlock() over the shipped filter list ===\n");
  printf("  rules parsed      : %zu (block %zu / exception %zu)\n", parsed,
         engine.block_rule_count(), engine.exception_rule_count());
  printf("  list parse time   : %.1f ms (startup, once per list update)\n",
         parse_time.InMillisecondsF());
  printf("  samples           : %zu (%zu urls x %d iterations)\n",
         samples_us.size(), std::size(kCorpus), kIterations);
  printf("  mean              : %.2f us\n", total / samples_us.size());
  printf("  p50               : %.2f us\n", pct(0.50));
  printf("  p95               : %.2f us\n", pct(0.95));
  printf("  p99               : %.2f us\n", pct(0.99));
  printf("  max               : %.2f us\n", samples_us.back());
  printf("  est. per page load (40 subresources, p99): %.2f ms\n",
         pct(0.99) * 40 / 1000.0);

  // Not a hard budget — this is a measurement test and the machine varies. But
  // a p99 above a millisecond would mean a single request can drop a frame on
  // the UI thread, which is the thing actually worth failing on.
  EXPECT_LT(pct(0.99), 1000.0)
      << "p99 above 1ms per request means the matcher can drop frames on the "
         "UI thread; at that point the async-matcher rework is justified";
}

}  // namespace
}  // namespace zephyrus_adblock
