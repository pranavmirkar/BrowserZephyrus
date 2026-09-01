// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_aggregator.h"

#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "chrome/browser/zephyrus/privacy/domain_string_table.h"
#include "chrome/browser/zephyrus/privacy/entity_resolver.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

RawEvent Event(uint32_t site, uint32_t domain, TrackerStatus status) {
  RawEvent e{};
  e.site_id = site;
  e.domain_hash = domain;
  e.entity_id = kNoEntity;
  e.category = static_cast<uint8_t>(Category::kUnknown);
  e.status = static_cast<uint8_t>(status);
  return e;
}

class PrivacyAggregatorTest : public testing::Test {
 protected:
  PrivacyAggregatorTest()
      : strings_(base::MakeRefCounted<DomainStringTable>()),
        resolver_(EntityResolver::CreateNull()),
        aggregator_(strings_, resolver_.get()) {}

  scoped_refptr<DomainStringTable> strings_;
  std::unique_ptr<EntityResolver> resolver_;
  PrivacyAggregator aggregator_;
};

// --- Folding ----------------------------------------------------------------

TEST_F(PrivacyAggregatorTest, CollapsesRepeatedEventsIntoOneRow) {
  strings_->Record(11, "ads.example");
  for (int i = 0; i < 50; ++i) {
    aggregator_.Add(Event(1, 11, TrackerStatus::kBlocked));
  }
  const auto rows = aggregator_.TakeRows(20000);
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(50u, rows[0].blocked);
  EXPECT_EQ("ads.example", rows[0].request_domain);
  EXPECT_EQ(20000, rows[0].day);
}

TEST_F(PrivacyAggregatorTest, SeparatesSitesAndDomains) {
  strings_->Record(11, "a.example");
  strings_->Record(22, "b.example");
  aggregator_.Add(Event(1, 11, TrackerStatus::kBlocked));
  aggregator_.Add(Event(1, 22, TrackerStatus::kBlocked));
  aggregator_.Add(Event(2, 11, TrackerStatus::kBlocked));
  EXPECT_EQ(3u, aggregator_.TakeRows(1).size());
}

TEST_F(PrivacyAggregatorTest, TakeRowsResetsButKeepsSessionTotals) {
  aggregator_.Add(Event(1, 11, TrackerStatus::kBlocked));
  EXPECT_EQ(1u, aggregator_.TakeRows(1).size());
  EXPECT_TRUE(aggregator_.TakeRows(1).empty()) << "rows must not be re-emitted";
  // The session headline is cumulative and survives a flush; only the
  // per-row accumulation resets.
  EXPECT_EQ(1u, aggregator_.session_totals().blocked);
}

// --- Status handling --------------------------------------------------------

TEST_F(PrivacyAggregatorTest, CountsEachStatusSeparately) {
  aggregator_.Add(Event(1, 11, TrackerStatus::kDetected));
  aggregator_.Add(Event(1, 11, TrackerStatus::kBlocked));
  aggregator_.Add(Event(1, 11, TrackerStatus::kBlocked));
  aggregator_.Add(Event(1, 11, TrackerStatus::kAllowed));
  aggregator_.Add(Event(1, 11, TrackerStatus::kRandomized));

  const auto rows = aggregator_.TakeRows(1);
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(1u, rows[0].detected);
  EXPECT_EQ(2u, rows[0].blocked);
  EXPECT_EQ(1u, rows[0].allowed);
  EXPECT_EQ(1u, rows[0].randomized);
}

// §6.5: a heuristic match is not a fact and must stay out of headline numbers.
TEST_F(PrivacyAggregatorTest, PotentialIsExcludedFromEveryCount) {
  aggregator_.Add(Event(1, 11, TrackerStatus::kPotential));
  const auto rows = aggregator_.TakeRows(1);
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(0u, rows[0].detected);
  EXPECT_EQ(0u, rows[0].blocked);
  EXPECT_EQ(0u, rows[0].allowed);
  EXPECT_EQ(0u, rows[0].randomized);

  const auto& totals = aggregator_.session_totals();
  EXPECT_EQ(0u, totals.detected + totals.blocked + totals.allowed +
                    totals.randomized);
}

// --- Bounds (§8.6, §11.3) ---------------------------------------------------

TEST_F(PrivacyAggregatorTest, FoldsBeyondThePerSiteDomainCap) {
  // A page hammering unique subdomains — the flooding attack.
  const int kFlood = PrivacyAggregator::kMaxDomainsPerSite + 500;
  for (int i = 1; i <= kFlood; ++i) {
    aggregator_.Add(Event(1, static_cast<uint32_t>(i), TrackerStatus::kBlocked));
  }

  const auto rows = aggregator_.TakeRows(1);
  // Capped domains plus the single "other" bucket — not kFlood rows.
  EXPECT_LE(rows.size(), PrivacyAggregator::kMaxDomainsPerSite + 1);
  EXPECT_GT(aggregator_.folded_into_other(), 0u);

  // Nothing was lost: every event is still counted somewhere.
  uint64_t total = 0;
  for (const auto& row : rows) {
    total += row.blocked;
  }
  EXPECT_EQ(static_cast<uint64_t>(kFlood), total);
}

TEST_F(PrivacyAggregatorTest, FoldedRowsAreLabelledNotBlank) {
  const int kFlood = PrivacyAggregator::kMaxDomainsPerSite + 10;
  for (int i = 1; i <= kFlood; ++i) {
    aggregator_.Add(Event(1, static_cast<uint32_t>(i), TrackerStatus::kBlocked));
  }
  const auto rows = aggregator_.TakeRows(1);
  bool found_other = false;
  for (const auto& row : rows) {
    if (row.request_domain == "(other)") {
      found_other = true;
    }
  }
  // A blank name is indistinguishable from a failed lookup; the collapsed tail
  // must say what it is.
  EXPECT_TRUE(found_other);
}

TEST_F(PrivacyAggregatorTest, DropsSitesBeyondTheCapButStillCountsThem) {
  const int kSites = PrivacyAggregator::kMaxSites + 50;
  for (int i = 1; i <= kSites; ++i) {
    aggregator_.Add(Event(static_cast<uint32_t>(i), 11, TrackerStatus::kBlocked));
  }
  EXPECT_LE(aggregator_.tracked_sites(), PrivacyAggregator::kMaxSites);
  EXPECT_GT(aggregator_.dropped_sites(), 0u);
  // The headline must not under-report because of a memory bound: every event
  // happened, whatever the map could hold.
  EXPECT_EQ(static_cast<uint64_t>(kSites),
            aggregator_.session_totals().blocked);
}

// --- Names ------------------------------------------------------------------

TEST_F(PrivacyAggregatorTest, UnknownDomainStringYieldsAnEmptyNameNotALoss) {
  // Never recorded — e.g. dropped at the string table's cap.
  aggregator_.Add(Event(1, 999, TrackerStatus::kBlocked));
  const auto rows = aggregator_.TakeRows(1);
  ASSERT_EQ(1u, rows.size());
  EXPECT_TRUE(rows[0].request_domain.empty());
  EXPECT_EQ(1u, rows[0].blocked) << "the event must survive a missing label";
}

TEST_F(PrivacyAggregatorTest, NullResolverLeavesEntityUnresolved) {
  strings_->Record(11, "ads.example");
  aggregator_.Add(Event(1, 11, TrackerStatus::kBlocked));
  const auto rows = aggregator_.TakeRows(1);
  ASSERT_EQ(1u, rows.size());
  // §4.1: with no dataset the feature still works, it just shows bare domains.
  EXPECT_EQ(kNoEntity, rows[0].entity_id);
  EXPECT_EQ(Category::kUnknown, rows[0].category);
  EXPECT_EQ("ads.example", rows[0].request_domain);
}


// The lifetime headline counts trackers we SAW, which includes the ones that
// completed. §2.1 makes DETECTED a superset of ALLOWED, and §5.2.1's lifetime
// row has no `allowed` column to put them in.
//
// Regression test with a real history: while the emission point guessed at
// request time, every unblocked request arrived as kDetected and the lifetime
// delta happened to count them all. When the request observer started
// reporting true outcomes, the same arithmetic began counting only the
// requests that FAILED, and the "since March" number silently collapsed.
TEST_F(PrivacyAggregatorTest, LifetimeDetectedIncludesAllowed) {
  auto add = [&](TrackerStatus status) {
    RawEvent event{};
    event.site_id = 1;
    event.domain_hash = 2;
    event.status = static_cast<uint8_t>(status);
    aggregator_.Add(event);
  };
  add(TrackerStatus::kAllowed);
  add(TrackerStatus::kAllowed);
  add(TrackerStatus::kAllowed);
  add(TrackerStatus::kDetected);
  add(TrackerStatus::kBlocked);

  const PrivacyAggregator::LifetimeDelta delta =
      aggregator_.TakeLifetimeDelta();
  EXPECT_EQ(4u, delta.detected)
      << "three allowed plus one detected are four trackers seen; counting "
         "only the leftover kDetected would report one";
  EXPECT_EQ(1u, delta.blocked);

  // The watermark must move past the allowed events too, or the next flush
  // re-counts them and the lifetime total inflates on every flush.
  add(TrackerStatus::kAllowed);
  const PrivacyAggregator::LifetimeDelta second =
      aggregator_.TakeLifetimeDelta();
  EXPECT_EQ(1u, second.detected);
}

}  // namespace
}  // namespace zephyrus_privacy
