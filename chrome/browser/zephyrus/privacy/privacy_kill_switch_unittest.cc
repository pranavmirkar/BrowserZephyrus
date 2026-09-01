// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §13.3: "If sustained ring drops exceed threshold, flush latency regresses
// badly, or repeated DB failures occur, disable collection for the session and
// record the reason in internals. A privacy feature that silently degrades
// browsing is worse than one that turns itself off loudly."
//
// Half of these tests assert that the switch does NOT fire, and those are the
// more important half. A kill switch that trips on one busy page load silently
// costs the user their entire privacy history for the session, with no error
// and nothing to notice — strictly worse than the degradation it was added to
// prevent. So every trigger is paired with the burst that must be tolerated.
//
// The evaluators are driven directly (this fixture is a friend) because a
// starved task sequence and a failing disk cannot be produced reliably from
// outside, and a kill switch nobody can prove fires is no better than the
// missing one it replaced.

#include <memory>

#include "base/files/scoped_temp_dir.h"
#include "base/test/scoped_feature_list.h"
#include "base/time/time.h"
#include "chrome/browser/zephyrus/privacy/privacy_crypto_impl.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "chrome/browser/zephyrus/privacy/privacy_event_sink.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/browser/zephyrus/privacy/privacy_ring_buffer.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {

// Deliberately NOT in an anonymous namespace: the service befriends
// zephyrus_privacy::PrivacyKillSwitchTest, and a fixture inside an anonymous
// namespace is a DIFFERENT entity that the friend declaration does not cover.
class PrivacyKillSwitchTest : public testing::Test {
 public:
  PrivacyKillSwitchTest() {
    features_.InitAndEnableFeature(kZephyrusPrivacyIntelligence);
  }

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    PrivacyCryptoImpl::RegisterProfilePrefs(prefs_.registry());
    // Memory-only, as in privacy_page_signals_unittest: no database and no
    // disk. The kill switch lives entirely on the service sequence.
    service_ = std::make_unique<PrivacyIntelligenceService>(
        temp_dir_.GetPath(), &prefs_, /*os_crypt=*/nullptr);
  }

  void TearDown() override {
    service_->Shutdown();
    service_.reset();
  }

 protected:
  // EVERY private access goes through this fixture, never through a TEST_F
  // body. Friendship is not inherited, and TEST_F defines a class DERIVED from
  // the fixture — so the body of a test is not a friend even though the
  // fixture is.
  KillSwitchReason reason() const { return service_->kill_switch_; }
  bool sink_disabled() const { return service_->sink_->disabled_for_session(); }
  void EvaluateOverflowOnce() { service_->EvaluateRingOverflow(); }

  bool RecordOneEvent() {
    RawEvent event{};
    event.site_id = 7;
    event.domain_hash = 9;
    return service_->sink_->Record(event);
  }

  // Fills the ring until it overflows by at least `drops`, then tells the
  // service to evaluate — one simulated drain interval's worth of loss.
  void DriveOverflowDrains(int drains, uint64_t drops_each) {
    for (int i = 0; i < drains; ++i) {
      const uint64_t target = service_->sink_->dropped_count() + drops_each;
      RawEvent event{};
      event.site_id = 1;
      event.domain_hash = 2;
      while (service_->sink_->dropped_count() < target) {
        // Once the ring is full every push is a drop, so this terminates.
        service_->sink_->Record(event);
      }
      service_->EvaluateRingOverflow();
    }
  }

  void DriveSlowDrains(int count, base::TimeDelta round_trip) {
    for (int i = 0; i < count; ++i) {
      service_->EvaluateDrainLatency(round_trip);
    }
  }

  void DriveFlushResults(int count, bool ok) {
    for (int i = 0; i < count; ++i) {
      service_->OnFlushResult(ok);
    }
  }

  content::BrowserTaskEnvironment task_environment_;
  base::test::ScopedFeatureList features_;
  base::ScopedTempDir temp_dir_;
  sync_preferences::TestingPrefServiceSyncable prefs_;
  std::unique_ptr<PrivacyIntelligenceService> service_;
};

// ---------------------------------------------------------------------------
// The switch must stay OUT of the way of a healthy pipeline.
// ---------------------------------------------------------------------------

TEST_F(PrivacyKillSwitchTest, HealthyPipelineNeverTrips) {
  DriveSlowDrains(50, base::Milliseconds(1));
  DriveFlushResults(50, /*ok=*/true);
  EvaluateOverflowOnce();

  EXPECT_EQ(KillSwitchReason::kNone, reason());
  EXPECT_FALSE(sink_disabled());
  EXPECT_EQ(KillSwitchReason::kNone, service_->GetStats().kill_switch);
}

// One pathological page load must not condemn the session. This is the test
// that stands between a real user and losing their history to a burst.
TEST_F(PrivacyKillSwitchTest, OneOverflowingDrainDoesNotTrip) {
  DriveOverflowDrains(/*drains=*/1, PrivacyRingBuffer::kCapacity * 4);

  EXPECT_EQ(KillSwitchReason::kNone, reason())
      << "a single burst of dropped events is a backlog, not a broken "
         "pipeline";
  EXPECT_FALSE(sink_disabled());
}

// Loss that stays under the threshold is normal operation, however long it
// goes on: the ring is allowed to shed load.
TEST_F(PrivacyKillSwitchTest, SustainedButSmallLossDoesNotTrip) {
  DriveOverflowDrains(/*drains=*/20, PrivacyRingBuffer::kCapacity / 8);

  EXPECT_EQ(KillSwitchReason::kNone, reason());
}

TEST_F(PrivacyKillSwitchTest, OneSlowDrainDoesNotTrip) {
  DriveSlowDrains(/*count=*/1, base::Seconds(30));

  EXPECT_EQ(KillSwitchReason::kNone, reason())
      << "one slow round trip is a busy machine, not a starved sequence";
}

// A failure that clears is exactly what retrying is for.
TEST_F(PrivacyKillSwitchTest, IntermittentDatabaseFailuresDoNotTrip) {
  for (int i = 0; i < 20; ++i) {
    DriveFlushResults(3, /*ok=*/false);
    DriveFlushResults(1, /*ok=*/true);  // recovered; the counter must reset
  }

  EXPECT_EQ(KillSwitchReason::kNone, reason());
}

// ---------------------------------------------------------------------------
// It must actually fire when the pipeline is genuinely failing.
// ---------------------------------------------------------------------------

TEST_F(PrivacyKillSwitchTest, SustainedRingOverflowTrips) {
  DriveOverflowDrains(/*drains=*/3, PrivacyRingBuffer::kCapacity);

  EXPECT_EQ(KillSwitchReason::kRingOverflow, reason());
  EXPECT_EQ(KillSwitchReason::kRingOverflow, service_->GetStats().kill_switch)
      << "§13.3 requires the reason be recorded where internals can show it; a "
         "switch that fires silently is the failure it exists to prevent";
}

TEST_F(PrivacyKillSwitchTest, SustainedSlowDrainsTrip) {
  DriveSlowDrains(/*count=*/3, base::Seconds(5));

  EXPECT_EQ(KillSwitchReason::kFlushLatency, reason());
}

TEST_F(PrivacyKillSwitchTest, RepeatedDatabaseFailuresTrip) {
  DriveFlushResults(/*count=*/5, /*ok=*/false);

  EXPECT_EQ(KillSwitchReason::kDatabaseFailures, reason());
}

// ---------------------------------------------------------------------------
// What tripping actually has to DO.
// ---------------------------------------------------------------------------

// The point of cutting at the producer: the network thread must stop paying.
// Stopping only the drain would leave it pushing into a ring nobody empties.
TEST_F(PrivacyKillSwitchTest, TrippingStopsTheProducer) {
  ASSERT_FALSE(sink_disabled());
  DriveFlushResults(/*count=*/5, /*ok=*/false);

  EXPECT_TRUE(sink_disabled());

  EXPECT_FALSE(RecordOneEvent())
      << "a disabled sink must refuse events outright, not queue them";
}

// "For the session" is load-bearing: a switch that flapped back on would
// reintroduce the very cost it tripped over, repeatedly.
TEST_F(PrivacyKillSwitchTest, StaysOffAndKeepsTheFirstReason) {
  DriveFlushResults(/*count=*/5, /*ok=*/false);
  ASSERT_EQ(KillSwitchReason::kDatabaseFailures, reason());

  // Healthy signals afterwards must not resurrect collection.
  DriveFlushResults(50, /*ok=*/true);
  DriveSlowDrains(50, base::Milliseconds(1));
  EXPECT_EQ(KillSwitchReason::kDatabaseFailures, reason());
  EXPECT_TRUE(sink_disabled());

  // A second, different failure must not overwrite the diagnosis: the first
  // reason is the one that describes what went wrong.
  DriveSlowDrains(3, base::Seconds(5));
  EXPECT_EQ(KillSwitchReason::kDatabaseFailures, reason())
      << "the first reason is the diagnosis; later symptoms are consequences";
}

}  // namespace zephyrus_privacy
