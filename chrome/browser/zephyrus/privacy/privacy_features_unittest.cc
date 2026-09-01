// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// What SHIPS, asserted rather than assumed.
//
// Until 151.0.7922.171 every one of these defaulted off, and the mode defaulted
// to collect-only, so an installed browser ran none of this: no collection, no
// randomization, no dashboard. That was correct while the gates were open, and
// it is exactly the kind of thing that gets flipped back by an unrelated change
// and noticed months later by nobody — the feature does not crash when it is
// off, it just quietly does nothing.
//
// These tests are deliberately about DEFAULTS, with no ScopedFeatureList. A
// test that enables a flag and then checks the flag is enabled proves nothing
// about what a user gets.

#include "chrome/browser/zephyrus/privacy/privacy_features.h"

#include "base/feature_list.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

TEST(PrivacyFeaturesDefaultsTest, CollectionIsOnByDefault) {
  EXPECT_TRUE(base::FeatureList::IsEnabled(kZephyrusPrivacyIntelligence));
  EXPECT_TRUE(IsCollectionEnabled())
      << "with collection off the whole feature is inert: no service, no "
         "dataset load, and the dashboard has nothing to show";
}

TEST(PrivacyFeaturesDefaultsTest, ModeIsEnabledNotCollectOnly) {
  EXPECT_EQ(Mode::kEnabled, GetMode())
      << "collect-only runs the pipeline with no UI surfaces, so a user sees "
         "nothing; §13.1's staged rollout has been completed";
}

TEST(PrivacyFeaturesDefaultsTest, SurfacesAreOnByDefault) {
  EXPECT_TRUE(base::FeatureList::IsEnabled(kZephyrusPrivacyFingerprinting));
  EXPECT_TRUE(base::FeatureList::IsEnabled(kZephyrusPrivacyCnameUncloaking));
  EXPECT_TRUE(base::FeatureList::IsEnabled(kZephyrusPrivacyDashboard));
  EXPECT_TRUE(base::FeatureList::IsEnabled(kZephyrusPrivacyInternals));
}

// The one that can actually change what a page sees. Kept as its own test so a
// deliberate decision to ship with randomization off fails HERE, loudly, rather
// than silently disabling protection users were told they had.
TEST(PrivacyFeaturesDefaultsTest, RandomizationIsOnByDefault) {
  EXPECT_TRUE(IsFingerprintRandomizationEnabled());
  EXPECT_EQ(kFpSurfaceDefault, FingerprintSurfaceMask())
      << "five of the six §6.5 surfaces ship on: canvas, audio, WebGL, "
         "navigator, screen. FONTS is defined but deliberately NOT in the "
         "default -- it changes which typefaces a page may use, not just a "
         "value it reads. If this now equals kFpSurfaceAll, someone enabled "
         "fonts for every user, which is a product decision and not a "
         "refactor. If WebGL is later cleared "
         "for the KHR_parallel_shader_compile cost, expect mask 27 here — that "
         "configuration was verified clean over the full breakage corpus.";
}

}  // namespace
}  // namespace zephyrus_privacy
