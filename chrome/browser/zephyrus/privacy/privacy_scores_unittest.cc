// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §6.3 requires both scores to be pure functions "so §12.1 can test them
// exhaustively". This is that test.
//
// "Exhaustively" cannot mean all of uint32^4, so it means all of 0..25 on each
// of the four inputs — 457k combinations, which covers every threshold in both
// ladders with margin on either side — plus the saturation ends checked
// separately. What is asserted is mostly not specific outputs but PROPERTIES,
// because the band cut-offs are a product decision that will be tuned, and a
// test that pins them would have to be rewritten every time the product
// changes. The properties are the part that must never change: they are what
// makes the number honest.

#include "chrome/browser/zephyrus/privacy/privacy_scores.h"

#include <stdint.h>

#include <array>
#include <optional>

#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

// The sweep bound. 25 clears the highest threshold in either ladder (20) with
// room to show the score has stopped climbing.
constexpr uint32_t kSweep = 25;

IntensityInputs Inputs(uint32_t e, uint32_t d, uint32_t f, uint32_t c) {
  IntensityInputs in;
  in.distinct_entities = e;
  in.distinct_third_party_domains = d;
  in.fingerprinting_attempts = f;
  in.tracking_cookies_attempted = c;
  return in;
}

// The invariant the popup's honesty rests on. "Minimal" must mean nothing was
// observed, not "not much was observed" — otherwise the page that touched one
// fingerprinting API reads as clean.
TEST(PrivacyScoresTest, MinimalMeansNothingWasObserved) {
  EXPECT_EQ(IntensityBand::kMinimal,
            ComputeTrackingIntensity(Inputs(0, 0, 0, 0)).band);

  for (uint32_t e = 0; e <= kSweep; ++e) {
    for (uint32_t d = 0; d <= kSweep; ++d) {
      for (uint32_t f = 0; f <= kSweep; ++f) {
        for (uint32_t c = 0; c <= kSweep; ++c) {
          const bool nothing_observed = (e == 0 && d == 0 && f == 0 && c == 0);
          const TrackingIntensity got =
              ComputeTrackingIntensity(Inputs(e, d, f, c));
          ASSERT_EQ(nothing_observed, got.band == IntensityBand::kMinimal)
              << "entities=" << e << " domains=" << d << " fp=" << f
              << " cookies=" << c;
          ASSERT_EQ(nothing_observed, got.points == 0);
        }
      }
    }
  }
}

// Observing MORE must never score a page as SAFER. Without this, a page could
// cross a threshold and improve, which is the kind of defect nobody notices
// until a user does.
TEST(PrivacyScoresTest, MonotonicInEveryInput) {
  for (uint32_t e = 0; e <= kSweep; ++e) {
    for (uint32_t d = 0; d <= kSweep; ++d) {
      for (uint32_t f = 0; f <= kSweep; ++f) {
        for (uint32_t c = 0; c <= kSweep; ++c) {
          const uint8_t base = ComputeTrackingIntensity(Inputs(e, d, f, c))
                                   .points;
          ASSERT_LE(base,
                    ComputeTrackingIntensity(Inputs(e + 1, d, f, c)).points);
          ASSERT_LE(base,
                    ComputeTrackingIntensity(Inputs(e, d + 1, f, c)).points);
          ASSERT_LE(base,
                    ComputeTrackingIntensity(Inputs(e, d, f + 1, c)).points);
          ASSERT_LE(base,
                    ComputeTrackingIntensity(Inputs(e, d, f, c + 1)).points);
        }
      }
    }
  }
}

// §6.3: "expandable to the exact contributing counts". The arithmetic the popup
// shows has to add up to the total it shows, or the expandable panel becomes a
// second, disagreeing implementation of the score.
TEST(PrivacyScoresTest, TermsAddUpToTheTotal) {
  for (uint32_t e = 0; e <= kSweep; ++e) {
    for (uint32_t d = 0; d <= kSweep; ++d) {
      for (uint32_t f = 0; f <= kSweep; ++f) {
        for (uint32_t c = 0; c <= kSweep; ++c) {
          const TrackingIntensity got =
              ComputeTrackingIntensity(Inputs(e, d, f, c));
          uint32_t summed = 0;
          uint32_t summed_max = 0;
          for (const IntensityTerm& term : got.terms) {
            ASSERT_LE(term.points, term.max_points);
            summed += term.points;
            summed_max += term.max_points;
          }
          ASSERT_EQ(summed, got.points);
          ASSERT_EQ(summed_max, got.max_points);
          // The observed counts must survive into the breakdown unchanged —
          // the panel reports them to the user as raw counts.
          ASSERT_EQ(e, got.term(TermIndex::kEntities).observed);
          ASSERT_EQ(d, got.term(TermIndex::kThirdPartyDomains).observed);
          ASSERT_EQ(f, got.term(TermIndex::kFingerprinting).observed);
          ASSERT_EQ(c, got.term(TermIndex::kTrackingCookies).observed);
        }
      }
    }
  }
}

// §16 acceptance: "both scores reproducible from identical input". Also the
// property that makes the function safe to call from the UI thread on every
// popup open rather than caching a result.
TEST(PrivacyScoresTest, SamePointsAlwaysMeansSameBand) {
  std::array<std::optional<IntensityBand>, 64> band_for_points = {};
  for (uint32_t e = 0; e <= kSweep; ++e) {
    for (uint32_t d = 0; d <= kSweep; ++d) {
      for (uint32_t f = 0; f <= kSweep; ++f) {
        for (uint32_t c = 0; c <= kSweep; ++c) {
          const TrackingIntensity got =
              ComputeTrackingIntensity(Inputs(e, d, f, c));
          ASSERT_LT(got.points, 64u);
          if (band_for_points[got.points].has_value()) {
            ASSERT_EQ(*band_for_points[got.points], got.band)
                << "points=" << static_cast<int>(got.points);
          } else {
            band_for_points[got.points] = got.band;
          }
        }
      }
    }
  }
}

// A count arriving as UINT32_MAX means something upstream is broken, but the
// score still has to be a score rather than a wrapped byte.
TEST(PrivacyScoresTest, SaturatesWithoutOverflow) {
  constexpr uint32_t kMax = UINT32_MAX;
  const TrackingIntensity got = ComputeTrackingIntensity(
      Inputs(kMax, kMax, kMax, kMax));
  EXPECT_EQ(IntensityBand::kSevere, got.band);
  EXPECT_EQ(got.max_points, got.points);
  for (const IntensityTerm& term : got.terms) {
    EXPECT_EQ(term.max_points, term.points);
  }
}

// Volume alone must not reach the top band: a page can contact a great many
// third parties without fingerprinting, and calling that Severe leaves no room
// to distinguish the page that does both.
TEST(PrivacyScoresTest, SevereNeedsMoreThanVolume) {
  const TrackingIntensity volume_only =
      ComputeTrackingIntensity(Inputs(UINT32_MAX, UINT32_MAX, 0, 0));
  EXPECT_LT(volume_only.band, IntensityBand::kSevere);

  const TrackingIntensity with_fingerprinting =
      ComputeTrackingIntensity(Inputs(UINT32_MAX, UINT32_MAX, 3, 0));
  EXPECT_GT(with_fingerprinting.band, volume_only.band);
}

// -- Protection Applied ------------------------------------------------------

// The case that must not be a percentage. A page with no third-party requests
// needs no protection; "0% protected" would be false and alarming, and "100%"
// would be an unearned claim.
TEST(PrivacyScoresTest, NoRequestsHasNoPercentage) {
  EXPECT_FALSE(ComputeProtectionApplied(0, 0).has_value());
}

// The endpoints are reserved for the cases that are literally true. Ordinary
// rounding gives 999/1000 as "100% blocked", which is exactly the unearned §2
// claim this clamp exists to prevent.
TEST(PrivacyScoresTest, EndpointsAreReservedForTheTruth) {
  const auto almost_all = ComputeProtectionApplied(999, 1);
  ASSERT_TRUE(almost_all.has_value());
  EXPECT_EQ(99, almost_all->percent) << "one request was allowed through";

  const auto almost_none = ComputeProtectionApplied(1, 999);
  ASSERT_TRUE(almost_none.has_value());
  EXPECT_EQ(1, almost_none->percent) << "one request really was blocked";

  const auto all = ComputeProtectionApplied(10, 0);
  ASSERT_TRUE(all.has_value());
  EXPECT_EQ(100, all->percent);

  const auto none = ComputeProtectionApplied(0, 10);
  ASSERT_TRUE(none.has_value());
  EXPECT_EQ(0, none->percent);
}

TEST(PrivacyScoresTest, PercentageIsExhaustivelyWellFormed) {
  for (uint32_t blocked = 0; blocked <= 300; ++blocked) {
    for (uint32_t allowed = 0; allowed <= 300; ++allowed) {
      const auto got = ComputeProtectionApplied(blocked, allowed);
      if (blocked == 0 && allowed == 0) {
        ASSERT_FALSE(got.has_value());
        continue;
      }
      ASSERT_TRUE(got.has_value());
      ASSERT_LE(got->percent, 100);
      // The two reserved endpoints, both directions.
      ASSERT_EQ(allowed == 0, got->percent == 100)
          << "blocked=" << blocked << " allowed=" << allowed;
      ASSERT_EQ(blocked == 0, got->percent == 0)
          << "blocked=" << blocked << " allowed=" << allowed;
      // The counts must survive so the popup can list them beside the ratio.
      ASSERT_EQ(blocked, got->blocked);
      ASSERT_EQ(allowed, got->allowed);
    }
  }
}

// Blocking more, with the same amount let through, can only improve the ratio.
TEST(PrivacyScoresTest, PercentageIsMonotonic) {
  for (uint32_t allowed = 0; allowed <= 100; ++allowed) {
    for (uint32_t blocked = 0; blocked < 100; ++blocked) {
      const auto lower = ComputeProtectionApplied(blocked, allowed);
      const auto higher = ComputeProtectionApplied(blocked + 1, allowed);
      ASSERT_TRUE(higher.has_value());
      if (lower.has_value()) {
        ASSERT_LE(lower->percent, higher->percent);
      }
    }
  }
}

TEST(PrivacyScoresTest, PercentageSaturatesWithoutOverflow) {
  constexpr uint32_t kMax = UINT32_MAX;
  const auto both = ComputeProtectionApplied(kMax, kMax);
  ASSERT_TRUE(both.has_value());
  EXPECT_EQ(50, both->percent);

  const auto all_blocked = ComputeProtectionApplied(kMax, 0);
  ASSERT_TRUE(all_blocked.has_value());
  EXPECT_EQ(100, all_blocked->percent);

  // The clamp has to survive a ratio far below one percent.
  const auto tiny = ComputeProtectionApplied(1, kMax);
  ASSERT_TRUE(tiny.has_value());
  EXPECT_EQ(1, tiny->percent);
}

// -- §6.6 What was protected ------------------------------------------------

// The rule that makes the list trustworthy: a category appears only if
// something was actually prevented. "Device fingerprint: 0" invites the reader
// to believe something was done about it.
TEST(PrivacyScoresTest, NothingProtectedIsAnEmptyList) {
  EXPECT_TRUE(ComputeWhatWasProtected(ProtectionInputs()).empty());
}

TEST(PrivacyScoresTest, OnlyNonZeroCategoriesAppear) {
  ProtectionInputs inputs;
  inputs.advertising_requests_blocked = 12;
  inputs.cross_site_requests_blocked = 30;

  const std::vector<ProtectedItem> got = ComputeWhatWasProtected(inputs);
  ASSERT_EQ(2u, got.size());
  // §6.6's order, not insertion order, so the list reads the same every time.
  EXPECT_EQ(ProtectedCategory::kAdvertisingIdentifiers, got[0].category);
  EXPECT_EQ(12u, got[0].count);
  EXPECT_EQ(ProtectedCategory::kBrowsingActivity, got[1].category);
  EXPECT_EQ(30u, got[1].count);
}

// Detection is not protection. Phase 2 observes fingerprinting surfaces but
// perturbs nothing, so the field that feeds this category stays zero and the
// category must not appear — claiming "Device fingerprint" protected while the
// page read the real values is the §2 violation this whole section guards.
TEST(PrivacyScoresTest, DeviceFingerprintNeedsRandomizationNotDetection) {
  ProtectionInputs inputs;
  inputs.fingerprint_surfaces_randomized = 0;  // detected-only, as in Phase 2
  EXPECT_TRUE(ComputeWhatWasProtected(inputs).empty());

  inputs.fingerprint_surfaces_randomized = 3;
  const std::vector<ProtectedItem> got = ComputeWhatWasProtected(inputs);
  ASSERT_EQ(1u, got.size());
  EXPECT_EQ(ProtectedCategory::kDeviceFingerprint, got[0].category);
}

TEST(PrivacyScoresTest, EveryCategoryCanBeReported) {
  ProtectionInputs inputs;
  inputs.third_party_cookies_blocked = 1;
  inputs.advertising_requests_blocked = 2;
  inputs.fingerprint_surfaces_randomized = 3;
  inputs.cross_site_requests_blocked = 4;
  inputs.referrers_stripped = 5;

  const std::vector<ProtectedItem> got = ComputeWhatWasProtected(inputs);
  ASSERT_EQ(5u, got.size());
  for (size_t i = 0; i < got.size(); ++i) {
    EXPECT_EQ(static_cast<uint32_t>(i + 1), got[i].count)
        << "categories must come back in §6.6's declared order";
  }
}

}  // namespace
}  // namespace zephyrus_privacy
