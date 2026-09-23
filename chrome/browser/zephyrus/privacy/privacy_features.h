// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_FEATURES_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_FEATURES_H_

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"

namespace zephyrus_privacy {

// Spec §13.1. Three states, not a boolean: COLLECT_ONLY exists so the pipeline
// can be validated against real usage — performance, drop rate, DB growth —
// before any UI depends on the data. The feature ships in that state first.
enum class Mode {
  // No interception hook, no service, zero cost. Not "enabled but idle".
  kDisabled,
  // Full pipeline, no UI surfaces.
  kCollectOnly,
  // Everything.
  kEnabled,
};

BASE_DECLARE_FEATURE(kZephyrusPrivacyIntelligence);

// Sub-flags (§13.2). Fingerprinting, CNAME uncloaking and the two WebUI hosts
// carry different risk and must be independently disableable without reverting
// the whole feature.
BASE_DECLARE_FEATURE(kZephyrusPrivacyFingerprinting);
// Phase 4, and deliberately SEPARATE from the flag above.
//
// The Phase 2 flag governs DETECTION: noticing that a page touched a
// fingerprinting surface and reporting it. This one governs RANDOMIZATION:
// changing what the page is told. They carry entirely different risk —
// detection cannot break a site, and perturbing canvas bytes can break image
// editors, chart libraries and games — so they must be switchable
// independently. One flag would mean the only way to stop suspected breakage
// was to also go blind.
//
// §6.5: "Ships behind a flag, default off, until the §12.6 breakage corpus is
// clean." Turning this on before that corpus passes is the decision the flag
// exists to prevent.
BASE_DECLARE_FEATURE(kZephyrusPrivacyFingerprintRandomization);
BASE_DECLARE_FEATURE(kZephyrusPrivacyCnameUncloaking);
BASE_DECLARE_FEATURE(kZephyrusPrivacyDashboard);
// Separate from the dashboard on purpose: different audience, different
// security posture, and internals should be restrictable to non-stable
// channels without taking the dashboard with it.
BASE_DECLARE_FEATURE(kZephyrusPrivacyInternals);

// The current mode. Authoritative, and re-reads the feature state every call:
// FeatureParam::Get() does a field-trial parameter lookup and a string compare,
// which is far too expensive for the request path (§8.1 allows zero
// allocations and <20µs p99 for the whole interception).
//
// Use this from UI, settings and startup. NOT per request.
Mode GetMode();

// The request-path accessor: resolves once and caches. Deliberately ignores
// any later change to the feature state, because a per-request re-read is the
// thing being avoided — a mode change takes effect on restart.
Mode GetModeForHotPath();

// Test-only. Clears the cache above so a ScopedFeatureList override applies.
void ResetHotPathModeCacheForTesting();

// True in kCollectOnly and kEnabled. The single question the emission point
// asks.
bool IsCollectionEnabled();

// Whether fingerprinting values may be PERTURBED.
//
// Two conditions, both required. The randomization flag is the §6.5 gate, and
// collection being enabled is the other: randomization that nothing observes
// would change what sites see while the user is told nothing about it, and
// §2's whole premise is that the browser reports what it did.
bool IsFingerprintRandomizationEnabled();

// Per-surface kill switches (§6.5).
//
// Every surface derives from ONE seed, so without this there is no way to turn
// a single surface off — which meant the only response to a suspected
// regression was disabling the whole feature, and the only way to find WHICH
// surface caused it was a rebuild per hypothesis. Both are unacceptable for
// something whose release gate is a breakage corpus.
//
// Bits are stable and must never be renumbered: they cross to the renderer.
inline constexpr uint32_t kFpSurfaceCanvas = 1u << 0;
inline constexpr uint32_t kFpSurfaceAudio = 1u << 1;
inline constexpr uint32_t kFpSurfaceWebgl = 1u << 2;
inline constexpr uint32_t kFpSurfaceNavigator = 1u << 3;
inline constexpr uint32_t kFpSurfaceScreen = 1u << 4;
// Local font visibility. Unlike the five above it does not perturb a value the
// page reads; it decides which locally-installed families a page is allowed to
// discover. See CSSFontSelector::ShouldHideLocalFontFamily.
inline constexpr uint32_t kFpSurfaceFonts = 1u << 5;
// Every DEFINED bit. Used to reject nonsense values, not as a default -- see
// kFpSurfaceDefault. Keeping the two separate is deliberate: this constant is
// what a new surface must be added to, and if it were also the default, adding
// a bit here would silently switch the new surface on for every user. That is
// exactly what happened when the fonts bit was first added.
inline constexpr uint32_t kFpSurfaceAll = 0x3fu;

// What actually ships on: every surface. Fonts was held back at first because
// it changes which typefaces a page may use rather than a value it measures.
// With the font list left unprotected the browser measured as UNIQUE on
// fingerprinting test pages, and the allowlist design (see
// CSSFontSelector::ShouldHideLocalFontFamily) only withholds fonts that
// arrived with applications, never the ones Windows ships, so it is on.
// Still a separate constant from kFpSurfaceAll -- see above.
inline constexpr uint32_t kFpSurfaceDefault =
    kFpSurfaceCanvas | kFpSurfaceAudio | kFpSurfaceWebgl |
    kFpSurfaceNavigator | kFpSurfaceScreen | kFpSurfaceFonts;

// Which surfaces may perturb. Feature param "surfaces", a decimal bitmask;
// absent means all. Example: --enable-features=ZephyrusPrivacyFingerprint\
// Randomization:surfaces/30 leaves canvas off and everything else on.
uint32_t FingerprintSurfaceMask();

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_FEATURES_H_
