// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The per-page accumulator behind §6.2. It sits on the request path and feeds
// numbers the user reads, so the properties worth pinning are the ones that
// would silently produce a wrong figure rather than a crash.

#include "chrome/browser/zephyrus/privacy/privacy_tab_helper.h"

#include <string>

#include "base/strings/stringprintf.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_privacy {
namespace {

class PrivacyTabHelperTest : public ChromeRenderViewHostTestHarness {
 public:
  void SetUp() override {
    ChromeRenderViewHostTestHarness::SetUp();
    NavigateAndCommit(GURL("https://news.example/"));
    PrivacyTabHelper::CreateForWebContents(web_contents());
  }

 protected:
  PrivacyTabHelper* helper() {
    return PrivacyTabHelper::FromWebContents(web_contents());
  }

  const PrivacyTabHelper::DomainRow* Find(
      const std::vector<PrivacyTabHelper::DomainRow>& rows,
      std::string_view domain) {
    for (const auto& row : rows) {
      if (row.domain == domain) {
        return &row;
      }
    }
    return nullptr;
  }
};

TEST_F(PrivacyTabHelperTest, CountsPerDomainAndPerStatus) {
  helper()->RecordRequest("ads.example", TrackerStatus::kBlocked);
  helper()->RecordRequest("ads.example", TrackerStatus::kBlocked);
  helper()->RecordRequest("ads.example", TrackerStatus::kDetected);
  helper()->RecordRequest("cdn.example", TrackerStatus::kAllowed);

  const auto rows = helper()->PageRows();
  ASSERT_EQ(2u, rows.size());
  const auto* ads = Find(rows, "ads.example");
  ASSERT_TRUE(ads);
  EXPECT_EQ(2u, ads->blocked);
  EXPECT_EQ(1u, ads->detected);
  EXPECT_EQ(3u, ads->requests());

  const auto totals = helper()->totals();
  EXPECT_EQ(2u, totals.blocked);
  EXPECT_EQ(1u, totals.detected);
  EXPECT_EQ(1u, totals.allowed);
  EXPECT_EQ(2u, totals.distinct_domains);
}

// §2.1: a heuristic match is excluded from headline numbers, so it must not
// reach the request count either — that column feeds both scores.
TEST_F(PrivacyTabHelperTest, PotentialIsExcludedFromEveryCount) {
  helper()->RecordRequest("maybe.example", TrackerStatus::kPotential);
  const auto rows = helper()->PageRows();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(0u, rows[0].requests());
  EXPECT_EQ(0u, helper()->totals().blocked);
  EXPECT_EQ(0u, helper()->totals().detected);
}

// The regression this exists for. Past the cap a domain is never inserted, so
// the lookup misses again on its next request. Counting distinct domains at
// that point counts REQUESTS, and the arithmetic panel — whose whole purpose is
// to be inspectable — would show a number the page could not possibly justify.
TEST_F(PrivacyTabHelperTest, DistinctDomainsSaturatesAndNeverCountsRequests) {
  // Fill well past the cap, then hammer the overflow domains repeatedly.
  for (int i = 0; i < 400; ++i) {
    helper()->RecordRequest(base::StringPrintf("d%d.example", i),
                            TrackerStatus::kBlocked);
  }
  for (int round = 0; round < 50; ++round) {
    for (int i = 300; i < 400; ++i) {
      helper()->RecordRequest(base::StringPrintf("d%d.example", i),
                              TrackerStatus::kBlocked);
    }
  }

  const auto totals = helper()->totals();
  EXPECT_EQ(256u, totals.distinct_domains)
      << "distinct domains must saturate at the cap, not climb with requests";
  // Request totals are NOT capped: the headline count must stay true.
  EXPECT_EQ(400u + 50u * 100u, totals.blocked);
}

// The tail is named, not blank: a blank row is indistinguishable from a lookup
// failure, and the user is entitled to know the list was truncated.
TEST_F(PrivacyTabHelperTest, OverflowFoldsIntoANamedBucket) {
  for (int i = 0; i < 300; ++i) {
    helper()->RecordRequest(base::StringPrintf("d%d.example", i),
                            TrackerStatus::kBlocked);
  }
  const auto rows = helper()->PageRows();
  const auto* other = Find(rows, "(other)");
  ASSERT_TRUE(other) << "the truncated tail must be visible as its own row";
  EXPECT_EQ(300u - 256u, other->requests());
  EXPECT_EQ(257u, rows.size()) << "256 named domains plus the tail";
}

// §6.2 describes THIS page. Carrying counts across a navigation would attribute
// one site's trackers to another.
TEST_F(PrivacyTabHelperTest, NavigationResetsEverything) {
  helper()->RecordRequest("ads.example", TrackerStatus::kBlocked);
  ASSERT_EQ(1u, helper()->totals().blocked);

  NavigateAndCommit(GURL("https://other.example/"));

  EXPECT_TRUE(helper()->PageRows().empty());
  const auto totals = helper()->totals();
  EXPECT_EQ(0u, totals.blocked);
  EXPECT_EQ(0u, totals.distinct_domains);
}

TEST_F(PrivacyTabHelperTest, EmptyDomainIsIgnored) {
  helper()->RecordRequest("", TrackerStatus::kBlocked);
  EXPECT_TRUE(helper()->PageRows().empty());
  EXPECT_EQ(0u, helper()->totals().blocked);
}

}  // namespace
}  // namespace zephyrus_privacy
