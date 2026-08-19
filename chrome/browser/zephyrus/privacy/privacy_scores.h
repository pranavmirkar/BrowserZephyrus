// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_SCORES_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_SCORES_H_

#include <stdint.h>

#include <array>
#include <optional>
#include <vector>

namespace zephyrus_privacy {

// §6.3, the two scores. Pure functions, no I/O, no dependency beyond the
// standard library — deliberately, so §12.1 can test them exhaustively and §2
// can be checked against them.
//
// **Why two.** A site with 50 trackers, all blocked, is at once the worst site
// you visited today and the safest one. Tracking Intensity is a property of the
// SITE; Protection Applied is a property of OUR RESPONSE. Collapsing them into
// a single number forces a lie in one direction or the other.
//
// **Why the arithmetic is a return value and not a comment.** §6.3 requires "no
// weights the user cannot inspect" and "show the arithmetic on tap". So the
// band is not the output — the breakdown is, and the band is derived from it.
// The popup renders the same terms this file added up, which makes the score
// impossible to misreport in the UI: there is no second copy of the weights.

// -- Tracking Intensity ------------------------------------------------------

enum class IntensityBand : uint8_t {
  // Nothing was observed. Not "a little" — literally zero on every input. See
  // the invariant on ComputeTrackingIntensity().
  kMinimal = 0,
  kLow = 1,
  kModerate = 2,
  kHigh = 3,
  kSevere = 4,
  kMaxValue = kSevere,
};

// The four inputs named in §6.3, for the current page.
struct IntensityInputs {
  // Distinct companies behind the third-party requests, resolved through the
  // entity dataset. The strongest single signal, because one company across
  // many domains is what actually builds a cross-site profile.
  uint32_t distinct_entities = 0;
  // Distinct third-party eTLD+1s contacted, whether or not an owner is known.
  // Counted separately from entities so that unattributed domains — which §4.2
  // forbids guessing an owner for — still register.
  uint32_t distinct_third_party_domains = 0;
  // Fingerprinting API surfaces touched (§6.5, §9.2.1).
  uint32_t fingerprinting_attempts = 0;
  // Cookies a tracker tried to set or read. "Attempted", not "set": the count
  // must not depend on whether we happened to stop it, or Protection Applied
  // would be double-counted into the site's own score.
  uint32_t tracking_cookies_attempted = 0;
};

// One row of the arithmetic, as the popup will render it.
struct IntensityTerm {
  // The raw observed count this term scored.
  uint32_t observed = 0;
  // Points it contributed, and the most it could have contributed.
  uint8_t points = 0;
  uint8_t max_points = 0;
};

// The four terms, in the order §6.3 lists them. Indexed by TermIndex so the UI
// and the tests refer to the same slot by name rather than by a bare integer.
enum class TermIndex : size_t {
  kEntities = 0,
  kThirdPartyDomains = 1,
  kFingerprinting = 2,
  kTrackingCookies = 3,
};
inline constexpr size_t kIntensityTermCount = 4;

struct TrackingIntensity {
  IntensityBand band = IntensityBand::kMinimal;
  uint8_t points = 0;
  uint8_t max_points = 0;
  std::array<IntensityTerm, kIntensityTermCount> terms = {};

  const IntensityTerm& term(TermIndex i) const {
    return terms[static_cast<size_t>(i)];
  }
};

// Scores a page. Deterministic, total, and saturating — no input value can
// overflow the result, so a page with four billion requests scores Severe
// rather than wrapping.
//
// **Invariant, asserted by test:** band == kMinimal if and only if every input
// is zero. Any observation at all lands at kLow or above. This is what stops
// the popup saying "Minimal tracking" about a page where something was in fact
// observed, which would be a §2 false claim rather than a rounding choice.
TrackingIntensity ComputeTrackingIntensity(const IntensityInputs& inputs);

// -- Protection Applied ------------------------------------------------------

struct ProtectionApplied {
  // blocked / (blocked + allowed), as a whole percentage.
  //
  // Clamped away from the endpoints when they would be a lie: 999 blocked of
  // 1000 is 99%, never 100%, and 1 blocked of 1000 is 1%, never 0%. Displaying
  // "100% blocked" while something was allowed is precisely the sort of
  // unearned claim §2 exists to prevent, and ordinary rounding produces it.
  uint8_t percent = 0;
  uint32_t blocked = 0;
  uint32_t allowed = 0;
};

// Returns nullopt when nothing was requested at all.
//
// **Why not 0%.** With no requests there is no ratio; 0/0 is not "unprotected".
// A page with no third-party requests needs no protection, and reporting "0%
// protected" on it is both false and alarming. The caller must render the
// no-requests case as its own string, so the type refuses to supply a number
// that does not exist.
std::optional<ProtectionApplied> ComputeProtectionApplied(uint32_t blocked,
                                                          uint32_t allowed);

// -- §6.6 What was protected -------------------------------------------------
//
// Translates raw observations into the thing the user actually keeps. "We
// blocked 23 requests to doubleclick.net" is a fact about our machinery; "we
// protected your advertising identifiers" is a fact about them.

enum class ProtectedCategory : uint8_t {
  kTrackingCookies = 0,
  kAdvertisingIdentifiers = 1,
  kDeviceFingerprint = 2,
  kBrowsingActivity = 3,
  kReferrerInformation = 4,
  kMaxValue = kReferrerInformation,
};

// Counts of things that were actually PREVENTED. Every field is a count of
// protection performed, never of something merely observed — which is the
// whole distinction §6.6 turns on. A fingerprinting surface that was detected
// and not perturbed contributes nothing here, because nothing was protected.
struct ProtectionInputs {
  uint32_t third_party_cookies_blocked = 0;
  uint32_t advertising_requests_blocked = 0;
  // RANDOMIZED, not detected. Zero until §6.5 randomization ships in Phase 4;
  // wiring detection into this field would claim a protection that is not
  // happening.
  uint32_t fingerprint_surfaces_randomized = 0;
  uint32_t cross_site_requests_blocked = 0;
  uint32_t referrers_stripped = 0;
};

struct ProtectedItem {
  ProtectedCategory category;
  uint32_t count = 0;
};

// Returns only the categories with a nonzero count, in the order §6.6 lists
// them.
//
// **Empty is a valid, correct answer.** §6.6: "Never list a category with zero
// events 'for completeness.' An empty list is an honest list." A row reading
// "Device fingerprint: 0" invites the reader to believe something was done
// about it, which is exactly the unearned credit §2 exists to prevent.
std::vector<ProtectedItem> ComputeWhatWasProtected(
    const ProtectionInputs& inputs);

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_SCORES_H_
