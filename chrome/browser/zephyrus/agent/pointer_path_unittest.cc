// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/pointer_path.h"

#include <algorithm>
#include <cmath>

#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

// How far `p` is from the straight line through `a` and `b`.
float DistanceFromLine(gfx::PointF a, gfx::PointF b, gfx::PointF p) {
  const float dx = b.x() - a.x();
  const float dy = b.y() - a.y();
  const float length = std::hypot(dx, dy);
  return std::abs(dy * (p.x() - a.x()) - dx * (p.y() - a.y())) / length;
}

TEST(PointerPathTest, StartsAndEndsExactlyWhereItWasAimed) {
  // Whatever the curve does between, the click lands where it was aimed.
  for (uint32_t seed = 1; seed < 40; ++seed) {
    const PointerPath path(gfx::PointF(40, 700), gfx::PointF(900, 120), seed);
    EXPECT_EQ(path.At(base::TimeDelta()), gfx::PointF(40, 700));
    EXPECT_EQ(path.At(path.duration()), gfx::PointF(900, 120));
    EXPECT_EQ(path.At(path.duration() + base::Seconds(5)),
              gfx::PointF(900, 120));
  }
}

TEST(PointerPathTest, ALongReachTakesLongerButNeverInProportion) {
  const PointerPath hop(gfx::PointF(100, 100), gfx::PointF(120, 100), 1);
  const PointerPath reach(gfx::PointF(100, 100), gfx::PointF(400, 100), 1);
  const PointerPath far(gfx::PointF(100, 100), gfx::PointF(1900, 100), 1);
  EXPECT_LT(hop.duration(), reach.duration());
  EXPECT_LT(reach.duration(), far.duration());
  EXPECT_LE(hop.duration(), base::Milliseconds(220));
  EXPECT_LE(far.duration(), base::Milliseconds(340));
  // Three times the distance is nowhere near three times the time.
  EXPECT_LT(far.duration(), 2 * reach.duration());
}

TEST(PointerPathTest, AcceleratesAndEasesInsteadOfMovingAtAConstantSpeed) {
  const PointerPath path(gfx::PointF(0, 0), gfx::PointF(800, 0), 3);
  const base::TimeDelta total = path.duration();
  const auto speed_at = [&](double fraction) {
    const base::TimeDelta t = total * fraction;
    const base::TimeDelta dt = base::Milliseconds(4);
    return (path.At(t + dt) - path.At(t)).Length() / 0.004f;
  };
  // Nearly stopped at both ends, fastest in the middle.
  EXPECT_LT(speed_at(0.02), 0.25f * speed_at(0.5));
  EXPECT_LT(speed_at(0.97), 0.25f * speed_at(0.5));
}

TEST(PointerPathTest, ALongReachArcsAndAShortOneDoesNot) {
  const gfx::PointF from(100, 100);
  const gfx::PointF to(900, 500);
  float widest = 0;
  for (uint32_t seed = 1; seed < 30; ++seed) {
    const PointerPath path(from, to, seed);
    for (int i = 1; i < 20; ++i) {
      widest = std::max(
          widest,
          DistanceFromLine(from, to, path.At(path.duration() * (i / 20.0))));
    }
  }
  EXPECT_GT(widest, 8.0f) << "a hand does not travel a ruler-straight line";
  // ...and never wanders: a few percent of the distance at most.
  EXPECT_LT(widest, 0.12f * std::hypot(800.0f, 400.0f));

  const PointerPath tiny(gfx::PointF(100, 100), gfx::PointF(108, 104), 5);
  for (int i = 1; i < 10; ++i) {
    EXPECT_LT(DistanceFromLine(gfx::PointF(100, 100), gfx::PointF(108, 104),
                               tiny.At(tiny.duration() * (i / 10.0))),
              0.5f);
  }
}

TEST(PointerPathTest, TheSameSeedIsTheSameMoveAndDifferentSeedsDiffer) {
  const gfx::PointF from(0, 0);
  const gfx::PointF to(600, 300);
  const PointerPath a(from, to, 7);
  const PointerPath b(from, to, 7);
  const base::TimeDelta t = a.duration() / 2;
  EXPECT_EQ(a.At(t), b.At(t));
  bool differs = false;
  for (uint32_t seed = 8; seed < 30; ++seed) {
    differs |= PointerPath(from, to, seed).At(t) != a.At(t);
  }
  EXPECT_TRUE(differs);
}

TEST(PointerPathTest, AStandingStartIsHandled) {
  // From and to the same point: no division by zero, no wandering.
  const PointerPath path(gfx::PointF(50, 50), gfx::PointF(50, 50), 2);
  EXPECT_EQ(path.At(path.duration() / 2), gfx::PointF(50, 50));
}

TEST(PointerPathTest, ScrollsGlideForLongerWhenTheyGoFarther) {
  EXPECT_LT(ScrollDuration(100), ScrollDuration(700));
  EXPECT_GE(ScrollDuration(1), base::Milliseconds(120));
  EXPECT_LE(ScrollDuration(100000), base::Milliseconds(380));
}

TEST(PointerPathTest, AWalkTakesLongerTheFartherItGoesAndNeverNothing) {
  EXPECT_GT(EstimateWalkTime(0), base::Milliseconds(100));
  EXPECT_LT(EstimateWalkTime(100), EstimateWalkTime(800));
  // Across a whole window is about a second, not a blink.
  EXPECT_GT(EstimateWalkTime(1200), base::Milliseconds(1200));
  EXPECT_LT(EstimateWalkTime(1200), base::Milliseconds(2000));
}

TEST(PointerPathTest, MinimumJerkIsFlatAtBothEnds) {
  EXPECT_DOUBLE_EQ(MinimumJerk(0), 0);
  EXPECT_DOUBLE_EQ(MinimumJerk(1), 1);
  EXPECT_NEAR(MinimumJerk(0.5), 0.5, 1e-9);
  EXPECT_LT(MinimumJerk(0.05), 0.01);
  EXPECT_GT(MinimumJerk(0.95), 0.99);
}

}  // namespace
}  // namespace zephyrus::agent
