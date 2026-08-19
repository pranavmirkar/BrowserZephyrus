// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_scores.h"

#include <stdint.h>

#include <array>
#include <vector>

namespace zephyrus_privacy {
namespace {

// A weight is a list of ascending thresholds. `observed` scores one point for
// each threshold it reaches, so the points are just "how far up this ladder did
// the page get" — which is the whole reason the scheme is a ladder and not a
// polynomial. §6.3 requires the user be able to inspect the weights, and a
// user can read a ladder.
//
// Written out rather than computed so that every number a user can see on the
// arithmetic panel appears literally in the source.
struct Ladder {
  // Ascending. Reaching entry N scores N+1 points.
  std::array<uint32_t, 4> thresholds;
  // How many of the thresholds are in use. The rest are ignored.
  uint8_t rungs;
};

// Domains and entities are volume signals: a handful is ordinary on the modern
// web, and the score should only climb once a page is unusual.
constexpr Ladder kEntitiesLadder = {{1, 3, 6, 10}, 4};
constexpr Ladder kDomainsLadder = {{1, 5, 10, 20}, 4};

// Fingerprinting and tracking cookies are kind signals, not volume signals.
// They start higher and top out lower: one fingerprinting attempt is already
// notable in a way that one third-party domain is not, but the tenth attempt
// says little the third did not.
constexpr Ladder kFingerprintingLadder = {{1, 3, 0, 0}, 2};
constexpr Ladder kCookiesLadder = {{1, 5, 15, 0}, 3};

IntensityTerm Score(uint32_t observed, const Ladder& ladder) {
  IntensityTerm term;
  term.observed = observed;
  term.max_points = ladder.rungs;
  for (uint8_t i = 0; i < ladder.rungs; ++i) {
    if (observed >= ladder.thresholds[i]) {
      term.points = static_cast<uint8_t>(i + 1);
    }
  }
  return term;
}

// Band cut-offs over the 13 available points. Chosen so that the lightest
// possible observation (one term at one point) is Low, and Severe needs
// several kinds of signal at once rather than one input run to its maximum:
// the worst a page can score on volume alone is 4 + 4 = 8 points, which is
// High. Reaching Severe requires fingerprinting or cookies on top.
IntensityBand BandFor(uint8_t points) {
  if (points == 0) {
    return IntensityBand::kMinimal;
  }
  if (points <= 3) {
    return IntensityBand::kLow;
  }
  if (points <= 6) {
    return IntensityBand::kModerate;
  }
  if (points <= 9) {
    return IntensityBand::kHigh;
  }
  return IntensityBand::kSevere;
}

}  // namespace

TrackingIntensity ComputeTrackingIntensity(const IntensityInputs& inputs) {
  TrackingIntensity result;
  result.terms[static_cast<size_t>(TermIndex::kEntities)] =
      Score(inputs.distinct_entities, kEntitiesLadder);
  result.terms[static_cast<size_t>(TermIndex::kThirdPartyDomains)] =
      Score(inputs.distinct_third_party_domains, kDomainsLadder);
  result.terms[static_cast<size_t>(TermIndex::kFingerprinting)] =
      Score(inputs.fingerprinting_attempts, kFingerprintingLadder);
  result.terms[static_cast<size_t>(TermIndex::kTrackingCookies)] =
      Score(inputs.tracking_cookies_attempted, kCookiesLadder);

  for (const IntensityTerm& term : result.terms) {
    result.points = static_cast<uint8_t>(result.points + term.points);
    result.max_points = static_cast<uint8_t>(result.max_points +
                                             term.max_points);
  }
  result.band = BandFor(result.points);
  return result;
}

std::optional<ProtectionApplied> ComputeProtectionApplied(uint32_t blocked,
                                                          uint32_t allowed) {
  if (blocked == 0 && allowed == 0) {
    return std::nullopt;
  }

  ProtectionApplied result;
  result.blocked = blocked;
  result.allowed = allowed;

  // 64-bit so that blocked + allowed cannot wrap, and so the multiply by 100
  // has room. Both inputs are counts of requests on one page, but the type
  // system should not be the only thing standing between a bug upstream and a
  // nonsense percentage.
  const uint64_t total = static_cast<uint64_t>(blocked) + allowed;
  uint64_t percent = (static_cast<uint64_t>(blocked) * 100u) / total;

  // Truncating division already refuses to round up to 100, but it happily
  // rounds a small nonzero ratio down to 0. Clamp both ends: the endpoints are
  // reserved for the cases that are literally true.
  if (percent == 0 && blocked > 0) {
    percent = 1;
  }
  if (percent == 100 && allowed > 0) {
    percent = 99;
  }
  result.percent = static_cast<uint8_t>(percent);
  return result;
}

std::vector<ProtectedItem> ComputeWhatWasProtected(
    const ProtectionInputs& inputs) {
  // Declared in §6.6's order so the list reads the same every time. Written as
  // a table rather than five if-statements so that the mapping from an
  // observation to the claim it licenses is visible in one place — that
  // mapping IS the feature, and burying it in control flow is how a category
  // eventually gets shown for an event that did not earn it.
  const std::array<ProtectedItem, 5> all = {{
      {ProtectedCategory::kTrackingCookies, inputs.third_party_cookies_blocked},
      {ProtectedCategory::kAdvertisingIdentifiers,
       inputs.advertising_requests_blocked},
      {ProtectedCategory::kDeviceFingerprint,
       inputs.fingerprint_surfaces_randomized},
      {ProtectedCategory::kBrowsingActivity,
       inputs.cross_site_requests_blocked},
      {ProtectedCategory::kReferrerInformation, inputs.referrers_stripped},
  }};

  std::vector<ProtectedItem> protected_items;
  for (const ProtectedItem& item : all) {
    if (item.count > 0) {
      protected_items.push_back(item);
    }
  }
  return protected_items;
}

}  // namespace zephyrus_privacy
