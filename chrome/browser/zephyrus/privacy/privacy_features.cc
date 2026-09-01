// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_features.h"

#include <atomic>

#include "base/metrics/field_trial_params.h"

namespace zephyrus_privacy {

BASE_FEATURE(kZephyrusPrivacyIntelligence,
             "ZephyrusPrivacyIntelligence",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kZephyrusPrivacyFingerprinting,
             "ZephyrusPrivacyFingerprinting",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kZephyrusPrivacyFingerprintRandomization,
             "ZephyrusPrivacyFingerprintRandomization",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kZephyrusPrivacyCnameUncloaking,
             "ZephyrusPrivacyCnameUncloaking",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kZephyrusPrivacyDashboard,
             "ZephyrusPrivacyDashboard",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kZephyrusPrivacyInternals,
             "ZephyrusPrivacyInternals",
             base::FEATURE_ENABLED_BY_DEFAULT);

namespace {

constexpr base::FeatureParam<Mode>::Option kModeOptions[] = {
    {Mode::kDisabled, "disabled"},
    {Mode::kCollectOnly, "collect-only"},
    {Mode::kEnabled, "enabled"},
};

// kEnabled as of 151.0.7922.171. The staged COLLECT_ONLY -> ENABLED path in
// §13.1 exists so performance and stability can be validated before any UI
// depends on the data, and that validation has now happened: the §12.6 breakage
// corpus is clean (64 sites, both arms, zero findings), the manual pass covered
// the load-bearing canvas cases, and ASAN/LSAN/TSAN are clean on this base.
//
// The four things that kept this off are all closed: the entity dataset now
// actually ships, the §13.3 kill switch exists, §13.4 is decided and guarded,
// and the panel can no longer claim protection for surfaces that were only
// detected.
constexpr base::FeatureParam<Mode> kMode{&kZephyrusPrivacyIntelligence, "mode",
                                         Mode::kEnabled, &kModeOptions};

}  // namespace

Mode GetMode() {
  if (!base::FeatureList::IsEnabled(kZephyrusPrivacyIntelligence)) {
    return Mode::kDisabled;
  }
  return kMode.Get();
}

namespace {
// -1 means "not yet resolved". Written at most once per process in practice;
// the atomic is for visibility across threads, not for contention.
std::atomic<int> g_cached_mode{-1};
}  // namespace

Mode GetModeForHotPath() {
  const int cached = g_cached_mode.load(std::memory_order_relaxed);
  if (cached >= 0) {
    return static_cast<Mode>(cached);
  }
  const Mode mode = GetMode();
  g_cached_mode.store(static_cast<int>(mode), std::memory_order_relaxed);
  return mode;
}

void ResetHotPathModeCacheForTesting() {
  g_cached_mode.store(-1, std::memory_order_relaxed);
}

bool IsCollectionEnabled() {
  return GetMode() != Mode::kDisabled;
}

uint32_t FingerprintSurfaceMask() {
  if (!IsFingerprintRandomizationEnabled()) {
    return 0;
  }
  static const base::FeatureParam<int> kSurfaces{
      &kZephyrusPrivacyFingerprintRandomization, "surfaces",
      static_cast<int>(kFpSurfaceDefault)};
  return static_cast<uint32_t>(kSurfaces.Get()) & kFpSurfaceAll;
}

bool IsFingerprintRandomizationEnabled() {
  return IsCollectionEnabled() &&
         base::FeatureList::IsEnabled(kZephyrusPrivacyFingerprintRandomization);
}

}  // namespace zephyrus_privacy
