// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/lens_quality.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

// Runs `seconds` of frames at a fixed interval.
void Advance(LensQualityGovernor& g, double interval, double seconds, bool heavy = true) {
  for (double t = 0; t < seconds; t += interval) {
    g.OnFrame(interval, heavy);
  }
}

TEST(LensQualityTest, StaysFullQualityOnAFastMachine) {
  LensQualityGovernor g;
  Advance(g, 1.0 / 60, 30);
  EXPECT_FALSE(g.lite());
  EXPECT_NEAR(g.average_interval(), 1.0 / 60, 1e-3);
}

TEST(LensQualityTest, DropsToLiteOnlyAfterSlowFramesHaveLastedAWhile) {
  LensQualityGovernor g;
  Advance(g, 0.035, 1.5);
  EXPECT_FALSE(g.lite()) << "a slow second is not yet a slow machine";
  Advance(g, 0.035, 6);
  EXPECT_TRUE(g.lite());
}

TEST(LensQualityTest, ComesBackWhenTheMachineRecoversAndNotBefore) {
  LensQualityGovernor g;
  Advance(g, 0.04, 10);
  ASSERT_TRUE(g.lite());
  // Merely fast is not enough for a few seconds: the prototype's switch was
  // one-way for life, so this is the new part.
  Advance(g, 0.012, 2);
  EXPECT_TRUE(g.lite());
  Advance(g, 0.012, 12);
  EXPECT_FALSE(g.lite());
}

TEST(LensQualityTest, TheGapBetweenTheTwoThresholdsDoesNotFlap) {
  LensQualityGovernor g;
  Advance(g, 0.04, 10);
  ASSERT_TRUE(g.lite());
  // 22 ms frames: faster than the drop-down threshold but not fast enough to
  // return. It must hold, not oscillate.
  int flips = 0;
  bool was = g.lite();
  for (double t = 0; t < 120; t += 0.022) {
    g.OnFrame(0.022, true);
    if (g.lite() != was) {
      ++flips;
      was = g.lite();
    }
  }
  EXPECT_EQ(flips, 0);
  EXPECT_TRUE(g.lite());
}

TEST(LensQualityTest, NeverFlipsTwiceInAQuickSuccession) {
  LensQualityGovernor g;
  // A machine that alternates between slow and fast every few seconds.
  int flips = 0;
  bool was = g.lite();
  for (int cycle = 0; cycle < 10; ++cycle) {
    for (double t = 0; t < 4; t += 0.04) {
      g.OnFrame(0.04, true);
      if (g.lite() != was) {
        ++flips;
        was = g.lite();
      }
    }
    for (double t = 0; t < 4; t += 0.01) {
      g.OnFrame(0.01, true);
      if (g.lite() != was) {
        ++flips;
        was = g.lite();
      }
    }
  }
  // 80 seconds; flips are at least kMinHold apart, so there cannot be many.
  EXPECT_LE(flips, 80 / static_cast<int>(LensQualityGovernor::kMinHold));
}

TEST(LensQualityTest, StallsAreNotEvidenceOfASlowMachine) {
  LensQualityGovernor g;
  for (int i = 0; i < 10; ++i) {
    Advance(g, 1.0 / 60, 2);
    g.OnFrame(5.0, true);  // the window was hidden, then shown
    g.OnFrame(0.3, true);
    g.OnFrame(-1, true);
    g.OnFrame(0, true);
  }
  EXPECT_FALSE(g.lite());
}

TEST(LensQualityTest, SlowFramesWhileStaticIsShowingChangeNothing) {
  LensQualityGovernor g;
  Advance(g, 0.05, 20, /*heavy=*/false);
  EXPECT_FALSE(g.lite()) << "Static has no blurs to drop";
  // Switching to a heavy fill starts the count from zero.
  Advance(g, 0.05, 1.0, /*heavy=*/true);
  EXPECT_FALSE(g.lite());
}

TEST(LensQualityTest, ResetForgetsEverything) {
  LensQualityGovernor g;
  Advance(g, 0.05, 10);
  ASSERT_TRUE(g.lite());
  g.Reset();
  EXPECT_FALSE(g.lite());
  EXPECT_NEAR(g.average_interval(), 1.0 / 60, 1e-9);
}

}  // namespace
}  // namespace zephyrus::agent
