// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §15: "Clear Browsing Data empties the DB" and "time-ranged clearing prunes
// the correct privacy rows", both asserted end to end.
//
// The database layer already has unit tests for this. What they cannot cover is
// the part that actually runs when a user clicks Delete: whether
// ChromeBrowsingDataRemoverDelegate reaches the privacy service at all, and
// whether it picks the right branch. That delegate hook is two lines of
// condition, and two lines of condition with no test is how privacy data
// quietly survives a clear.
//
// Driving BrowsingDataRemover directly rather than the settings UI is
// deliberate: the redesigned dialog puts "All time" behind a lazily-rendered
// menu that does not open under synthetic events, and a test that cannot select
// the range cannot test the branch that matters.

#include "base/run_loop.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/zephyrus/privacy/privacy_database.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browsing_data_remover_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

class PrivacyClearBrowsingDataBrowserTest : public InProcessBrowserTest {
 public:
  PrivacyClearBrowsingDataBrowserTest() {
    features_.InitAndEnableFeature(kZephyrusPrivacyIntelligence);
  }

 protected:
  PrivacyIntelligenceService* service() {
    return PrivacyIntelligenceServiceFactory::GetForBrowserContext(
        browser()->profile());
  }

  // Feeds events straight into the sink, which is exactly what the emission
  // point does. Deterministic, and it does not depend on a live network.
  void RecordEvents(int count) {
    auto sink = service()->sink();
    auto strings = service()->domain_strings();
    ASSERT_TRUE(sink);
    for (int i = 0; i < count; ++i) {
      const uint32_t site = 1000u;
      const uint32_t domain = 2000u + static_cast<uint32_t>(i % 8);
      strings->RecordSite(site, "news.example");
      strings->Record(domain, "tracker.example");
      RawEvent event{};
      event.site_id = site;
      event.domain_hash = domain;
      event.entity_id = kNoEntity;
      event.category = static_cast<uint8_t>(Category::kUnknown);
      // Alternate DETECTED and BLOCKED so both lifetime counters are
      // exercised. Recording only one status made an earlier version of this
      // test assert on a counter that was legitimately zero.
      event.status = static_cast<uint8_t>(
          (i % 2 == 0) ? TrackerStatus::kDetected : TrackerStatus::kBlocked);
      sink->Record(event);
    }
  }

  // The flush timer is 5s, and the first flush additionally waits for the
  // entity dataset to settle. Poll rather than sleeping a fixed amount.
  bool WaitForRowsOnDisk() {
    for (int attempt = 0; attempt < 60; ++attempt) {
      if (DailyRowCount() > 0) {
        return true;
      }
      base::RunLoop loop;
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, loop.QuitClosure(), base::Milliseconds(500));
      loop.Run();
    }
    return false;
  }

  int64_t DailyRowCount() {
    base::test::TestFuture<std::optional<DatabaseStats>> future;
    service()->GetDatabaseStats(future.GetCallback());
    const std::optional<DatabaseStats> stats = future.Take();
    return stats ? stats->daily_rows : 0;
  }

  uint64_t LifetimeDetected() {
    base::test::TestFuture<std::optional<DatabaseStats>> future;
    service()->GetDatabaseStats(future.GetCallback());
    const std::optional<DatabaseStats> stats = future.Take();
    return stats ? stats->lifetime_detected : 0;
  }

  uint64_t LifetimeBlocked() {
    base::test::TestFuture<std::optional<DatabaseStats>> future;
    service()->GetDatabaseStats(future.GetCallback());
    const std::optional<DatabaseStats> stats = future.Take();
    return stats ? stats->lifetime_blocked : 0;
  }

  void ClearHistory(base::Time begin, base::Time end) {
    content::BrowsingDataRemover* remover =
        browser()->profile()->GetBrowsingDataRemover();
    content::BrowsingDataRemoverCompletionObserver observer(remover);
    remover->RemoveAndReply(
        begin, end, chrome_browsing_data_remover::DATA_TYPE_HISTORY,
        chrome_browsing_data_remover::ALL_ORIGIN_TYPES, &observer);
    observer.BlockUntilCompletion();
    // The privacy service's own deletes are posted to the database sequence
    // after the remover returns; let them land.
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Seconds(2));
    loop.Run();
  }

  base::test::ScopedFeatureList features_;
};

// The branch that could never be driven through the settings UI.
IN_PROC_BROWSER_TEST_F(PrivacyClearBrowsingDataBrowserTest,
                       ClearingEverythingEmptiesTheDatabase) {
  ASSERT_TRUE(service());
  RecordEvents(200);
  ASSERT_TRUE(WaitForRowsOnDisk())
      << "nothing reached the database, so the clear below would prove nothing";
  ASSERT_GT(LifetimeDetected(), 0u);
  ASSERT_GT(LifetimeBlocked(), 0u);

  ClearHistory(base::Time(), base::Time::Max());

  EXPECT_EQ(0, DailyRowCount())
      << "clearing all history must empty the privacy database";
  EXPECT_EQ(0u, LifetimeBlocked());
  EXPECT_EQ(0u, LifetimeDetected())
      << "§5.2.1: an explicit full clear takes the lifetime counters too; they "
         "are exempt from RETENTION, not from deletion";
}

// The complement, and the reason the branch exists: a ranged clear must NOT
// zero the lifetime totals, because there is no honest way to subtract a time
// window from three numbers that carry no time dimension.
IN_PROC_BROWSER_TEST_F(PrivacyClearBrowsingDataBrowserTest,
                       RangedClearSparesTheLifetimeCounters) {
  ASSERT_TRUE(service());
  RecordEvents(200);
  ASSERT_TRUE(WaitForRowsOnDisk());
  const uint64_t before_detected = LifetimeDetected();
  const uint64_t before_blocked = LifetimeBlocked();
  ASSERT_GT(before_detected, 0u);
  ASSERT_GT(before_blocked, 0u);

  ClearHistory(base::Time::Now() - base::Hours(1), base::Time::Now());

  EXPECT_EQ(0, DailyRowCount())
      << "a range covering today must remove today's rows in full";
  EXPECT_EQ(before_blocked, LifetimeBlocked());
  EXPECT_EQ(before_detected, LifetimeDetected())
      << "a time-ranged clear must leave the lifetime counters alone";
}

}  // namespace
}  // namespace zephyrus_privacy
