// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_features.h"

#include <atomic>

#include "base/metrics/field_trial_params.h"

namespace zephyrus_privacy {

BASE_FEATURE(kZephyrusPrivacyIntelligence,
             "ZephyrusPrivacyIntelligence",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kZephyrusPrivacyFingerprinting,
             "ZephyrusPrivacyFingerprinting",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kZephyrusPrivacyFingerprintRandomization,
             "ZephyrusPrivacyFingerprintRandomization",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kZephyrusPrivacyCnameUncloaking,
             "ZephyrusPrivacyCnameUncloaking",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kZephyrusPrivacyDashboard,
             "ZephyrusPrivacyDashboard",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kZephyrusPrivacyInternals,
             "ZephyrusPrivacyInternals",
             base::FEATURE_DISABLED_BY_DEFAULT);

namespace {

constexpr base::FeatureParam<Mode>::Option kModeOptions[] = {
    {Mode::kDisabled, "disabled"},
    {Mode::kCollectOnly, "collect-only"},
    {Mode::kEnabled, "enabled"},
};

// Defaults to collect-only rather than enabled: turning the feature on should
// start the pipeline, not the UI. Reaching kEnabled is a deliberate second
// step.
constexpr base::FeatureParam<Mode> kMode{&kZephyrusPrivacyIntelligence, "mode",
                                         Mode::kCollectOnly, &kModeOptions};

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
      static_cast<int>(kFpSurfaceAll)};
  return static_cast<uint32_t>(kSurfaces.Get()) & kFpSurfaceAll;
}

bool IsFingerprintRandomizationEnabled() {
  return IsCollectionEnabled() &&
         base::FeatureList::IsEnabled(kZephyrusPrivacyFingerprintRandomization);
}

}  // namespace zephyrus_privacy
