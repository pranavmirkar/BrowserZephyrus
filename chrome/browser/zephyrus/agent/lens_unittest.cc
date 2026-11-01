// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>
#include <vector>

#include "chrome/browser/zephyrus/agent/lens_eyes.h"
#include "chrome/browser/zephyrus/agent/lens_geometry.h"
#include "chrome/browser/zephyrus/agent/lens_math.h"
#include "chrome/browser/zephyrus/agent/lens_random.h"
#include "chrome/browser/zephyrus/agent/lens_spring.h"
#include "testing/gtest/include/gtest/gtest.h"

// Milestone 1 of the lens mascot (docs/mascot-lens-plan.md): the pure model.
//
// Two kinds of check. The ones named in the handoff spec (section 10) are here
// as written. The "Reference" ones compare against numbers produced by running
// the PROTOTYPE'S OWN functions (agent-mascot.html, under node), so a drift
// from the prototype shows up as a number and not as "looks about right".

namespace zephyrus::agent {
namespace {

// ---- geometry ---------------------------------------------------------------

TEST(LensGeometryTest, PivotsMatchTheSpecAtThreeSlants) {
  struct Case {
    double u;
    LensPoint left;
    LensPoint right;
  };
  // Spec section 4.2 table.
  const Case cases[] = {
      {0.0, {25.0, 195.7}, {226.0, 324.3}},
      {0.5, {26.8, 260.0}, {224.2, 260.0}},
      {1.0, {25.0, 324.3}, {226.0, 195.7}},
  };
  for (const Case& c : cases) {
    const std::optional<LensPivots> p = FindLensPivots(c.u, 1.0);
    ASSERT_TRUE(p.has_value()) << "u=" << c.u;
    EXPECT_NEAR(p->left.x, c.left.x, 0.2) << "u=" << c.u;
    EXPECT_NEAR(p->left.y, c.left.y, 0.2) << "u=" << c.u;
    EXPECT_NEAR(p->right.x, c.right.x, 0.2) << "u=" << c.u;
    EXPECT_NEAR(p->right.y, c.right.y, 0.2) << "u=" << c.u;
  }
}

TEST(LensGeometryTest, ControlPointsReferenceTheOriginalAtTheEnds) {
  // u = 0 and u = 1 are the original artwork's two shapes, verbatim.
  const LensBodyControls zero = ComputeBodyControls(0.0, 1.0);
  EXPECT_NEAR(zero.a[1].x, 47.6865, 1e-6);
  EXPECT_NEAR(zero.a[1].y, 282.23, 1e-6);
  EXPECT_NEAR(zero.a[2].x, 251.0, 1e-6);
  EXPECT_NEAR(zero.a[2].y, 569.607, 1e-6);
  EXPECT_NEAR(zero.b[1].x, 203.313, 1e-6);
  EXPECT_NEAR(zero.b[1].y, 237.783, 1e-6);
  EXPECT_NEAR(zero.b[2].x, 0.0, 1e-6);
  EXPECT_NEAR(zero.b[2].y, -49.594, 1e-6);
  const LensBodyControls one = ComputeBodyControls(1.0, 1.0);
  EXPECT_NEAR(one.a[1].x, 0.0, 1e-6);
  EXPECT_NEAR(one.a[1].y, 569.607, 1e-6);
  EXPECT_NEAR(one.a[2].x, 203.313, 1e-6);
  EXPECT_NEAR(one.b[1].x, 251.0, 1e-6);
  EXPECT_NEAR(one.b[2].x, 47.6865, 1e-6);
}

TEST(LensGeometryTest, ReferenceControlPointsAndPivotsMatchThePrototype) {
  struct Case {
    double u, k;
    double a1x, a1y, a2x, a2y, b1x, b1y, b2x, b2y;
    double pl_x, pl_y, pr_x, pr_y;
  };
  // Produced by running controls() and pivots() from agent-mascot.html.
  const Case cases[] = {
      {0.3, 1.1, 33.38055, 379.28671, 236.6939, 505.73259, 217.6191, 140.72619,
       14.30595, 14.28031, 23.997656, 231.731955, 227.002243, 288.281055},
      {0.85, 0.9, 7.152975, 499.851105, 210.46605, 318.803595, 243.84695,
       20.161995, 40.533525, 201.209505, 29.404446, 301.965604, 221.59544,
       218.04737},
      {-0.2, 1.2, 50.54769, 265.983456, 251.0, 652.218144, 200.45178,
       254.029344, 0.0, -132.205344, 19.389759, 180.472286, 231.610157,
       339.54076},
      {1.4, 0.8, 0.0, 521.481096, 200.45178, 263.991304, 251.0, -1.467896,
       50.54769, 256.021896, 33.703651, 317.899335, 217.296201, 202.113694},
  };
  for (const Case& c : cases) {
    const LensBodyControls got = ComputeBodyControls(c.u, c.k);
    EXPECT_NEAR(got.a[1].x, c.a1x, 1e-4) << c.u << "," << c.k;
    EXPECT_NEAR(got.a[1].y, c.a1y, 1e-4) << c.u << "," << c.k;
    EXPECT_NEAR(got.a[2].x, c.a2x, 1e-4) << c.u << "," << c.k;
    EXPECT_NEAR(got.a[2].y, c.a2y, 1e-4) << c.u << "," << c.k;
    EXPECT_NEAR(got.b[1].x, c.b1x, 1e-4) << c.u << "," << c.k;
    EXPECT_NEAR(got.b[1].y, c.b1y, 1e-4) << c.u << "," << c.k;
    EXPECT_NEAR(got.b[2].x, c.b2x, 1e-4) << c.u << "," << c.k;
    EXPECT_NEAR(got.b[2].y, c.b2y, 1e-4) << c.u << "," << c.k;
    const std::optional<LensPivots> p = FindLensPivots(c.u, c.k);
    ASSERT_TRUE(p.has_value()) << c.u << "," << c.k;
    EXPECT_NEAR(p->left.x, c.pl_x, 1e-3) << c.u << "," << c.k;
    EXPECT_NEAR(p->left.y, c.pl_y, 1e-3) << c.u << "," << c.k;
    EXPECT_NEAR(p->right.x, c.pr_x, 1e-3) << c.u << "," << c.k;
    EXPECT_NEAR(p->right.y, c.pr_y, 1e-3) << c.u << "," << c.k;
  }
}

TEST(LensGeometryTest, BodyIsClosedFiniteAndInsideTheSidesForEverySlantAndBreath) {
  // Spec: finite for s in [-1.1, 1.1] and k in [0.8, 1.2]. s maps to u as
  // (s + 1) / 2.
  for (double s = -1.1; s <= 1.1001; s += 0.05) {
    for (double k = 0.8; k <= 1.2001; k += 0.05) {
      const LensBodyControls c = ComputeBodyControls((s + 1.0) / 2.0, k);
      // The four fixed corners never move.
      EXPECT_EQ(c.a[0].x, 0.0);
      EXPECT_EQ(c.a[0].y, kLensBodyTop);
      EXPECT_EQ(c.a[3].x, 251.0);
      EXPECT_EQ(c.b[0].y, kLensBodyBottom);
      EXPECT_EQ(c.b[3].x, 0.0);
      for (const LensCubic* cubic : {&c.a, &c.b}) {
        for (const LensPoint& p : *cubic) {
          ASSERT_TRUE(std::isfinite(p.x) && std::isfinite(p.y))
              << "s=" << s << " k=" << k;
          // The curves never poke outside the side edges.
          EXPECT_GE(p.x, 0.0);
          EXPECT_LE(p.x, 251.0);
        }
      }
    }
  }
}

TEST(LensGeometryTest, SlantBeyondTheClampStopsMoving) {
  // u is clamped to [-0.06, 1.06]; a spring that overshoots further than that
  // must not keep deforming the body.
  const LensBodyControls at_limit = ComputeBodyControls(1.06, 1.0);
  const LensBodyControls beyond = ComputeBodyControls(5.0, 1.0);
  EXPECT_EQ(at_limit.a[1].y, beyond.a[1].y);
  EXPECT_EQ(at_limit.b[2].x, beyond.b[2].x);
}

// ---- layer weights ------------------------------------------------------------

TEST(LensLayerTest, EachLayerContributesExactlyItsWeight) {
  // Composite bottom to top (static opaque, then fluid, opal, space at their
  // opacities) and check what each contributes equals its weight.
  for (double wF = 0; wF <= 1.0001; wF += 0.125) {
    for (double wO = 0; wO <= 1.0001 - wF; wO += 0.125) {
      for (double wP = 0; wP <= 1.0001 - wF - wO; wP += 0.125) {
        const double wS = std::max(0.0, 1.0 - wF - wO - wP);
        const LensLayerOpacities o = ComputeLayerOpacities(wF, wO, wP);
        const double space = o.space;
        const double opal = (1 - o.space) * o.opal;
        const double fluid = (1 - o.space) * (1 - o.opal) * o.fluid;
        const double stat =
            (1 - o.space) * (1 - o.opal) * (1 - o.fluid);
        EXPECT_NEAR(space, wP, 1e-9) << wF << "," << wO << "," << wP;
        EXPECT_NEAR(opal, wO, 1e-9) << wF << "," << wO << "," << wP;
        EXPECT_NEAR(fluid, wF, 1e-9) << wF << "," << wO << "," << wP;
        EXPECT_NEAR(stat, wS, 1e-9) << wF << "," << wO << "," << wP;
        EXPECT_NEAR(space + opal + fluid + stat, 1.0, 1e-9);
      }
    }
  }
}

TEST(LensLayerTest, ALayerBelowOneThousandthIsHiddenSoItDoesNoWork) {
  const LensLayerOpacities o = ComputeLayerOpacities(0.0005, 0.0, 0.0);
  EXPECT_FALSE(o.show_fluid);
  EXPECT_TRUE(o.show_static);
  const LensLayerOpacities full = ComputeLayerOpacities(1.0, 0.0, 0.0);
  EXPECT_TRUE(full.show_fluid);
  EXPECT_FALSE(full.show_static) << "a fully fluid body has no static layer";
}

// ---- springs ------------------------------------------------------------------

TEST(LensSpringTest, StableAndConvergentAtEveryFrameLengthTheLoopCanProduce) {
  // Spec: stable at dt from 1/240 to 0.05 (the loop caps a frame at 50 ms). The
  // stiffest spring in the prototype is omega 46 (gaze).
  const double dts[] = {1.0 / 240, 1.0 / 120, 1.0 / 60, 1.0 / 30, 0.05};
  const double omegas[] = {3.5, 14.0, 30.0, 46.0};
  const double zetas[] = {0.5, 0.7, 1.0};
  for (double dt : dts) {
    for (double w : omegas) {
      for (double z : zetas) {
        LensSpring s(0.0, w, z);
        s.t = 1.0;
        double peak = 0;
        for (int i = 0; i < static_cast<int>(10.0 / dt); ++i) {
          s.Step(dt);
          ASSERT_TRUE(std::isfinite(s.x)) << "dt=" << dt << " w=" << w;
          peak = std::max(peak, std::abs(s.x));
        }
        EXPECT_LT(peak, 1.5) << "dt=" << dt << " w=" << w << " z=" << z;
        EXPECT_NEAR(s.x, 1.0, 1e-3) << "dt=" << dt << " w=" << w << " z=" << z;
      }
    }
  }
}

TEST(LensSpringTest, CriticalDampingNeverOvershoots) {
  for (double w : {3.5, 14.0, 46.0}) {
    LensSpring s(0.0, w, 1.0);
    s.t = 1.0;
    for (int i = 0; i < 2000; ++i) {
      s.Step(1.0 / 60);
      ASSERT_LE(s.x, 1.0 + 1e-3) << "w=" << w << " step " << i;
    }
  }
}

TEST(LensSpringTest, UnderdampedOvershootsALittleSoPosesPop) {
  LensSpring s(0.0, 16.0, 0.7);
  s.t = 1.0;
  double peak = 0;
  for (int i = 0; i < 600; ++i) {
    s.Step(1.0 / 60);
    peak = std::max(peak, s.x);
  }
  EXPECT_GT(peak, 1.0);
  EXPECT_LT(peak, 1.2);
}

TEST(LensSpringTest, NonPositiveStepsDoNothingInsteadOfBlowingUp) {
  LensSpring s(0.5, 14.0, 1.0);
  s.t = 2.0;
  s.Step(0.0);
  s.Step(-1.0);
  EXPECT_EQ(s.x, 0.5);
  EXPECT_EQ(s.v, 0.0);
}

// ---- eyes -----------------------------------------------------------------------

TEST(LensEyesTest, ReferenceLidsMatchThePrototype) {
  struct Case {
    double open, tilt, cheek, blink;
    bool left;
    double a, b, yb, yc;
  };
  // From the prototype's own lid code.
  const Case cases[] = {
      {1, 0, 0, 0, true, -12.6, -12.6, 6, 6},
      {0.62, -3.4, 0.45, 0, true, -4.716, -11.516, 2.31, -3.81},
      {0.62, -3.4, 0.45, 0, false, -11.516, -4.716, 2.31, -3.81},
      {1, 2.6, 1.1, 0.5, false, -4.1, -9.3, -3.02, -8.58},
      {0, 0, 1.1, 0, true, -0.8, -0.8, -3.02, 3.22},
  };
  for (const Case& c : cases) {
    const LensLid lid = ComputeLid(c.open, c.tilt, c.cheek, c.blink, c.left);
    EXPECT_NEAR(lid.a, c.a, 1e-6);
    EXPECT_NEAR(lid.b, c.b, 1e-6);
    EXPECT_NEAR(lid.yb, c.yb, 1e-6);
    EXPECT_NEAR(lid.yc, c.yc, 1e-6);
  }
}

TEST(LensEyesTest, AClosedEyeKeepsNonNegativeThicknessForEveryCheck) {
  // Spec: closed eye (open 0) has non-negative thickness for every cheek 0..1.1.
  // The prototype keeps the lower lid at least 0.9 below the upper lid at the
  // middle; this holds for any openness too.
  for (double open : {0.0, 0.25, 0.5, 1.0, 1.05}) {
    for (double cheek = 0.0; cheek <= 1.1001; cheek += 0.05) {
      for (bool left : {true, false}) {
        const LensLid lid = ComputeLid(open, 0.0, cheek, 0.0, left);
        EXPECT_GE(lid.CenterGap(), 0.9 - 1e-9)
            << "open=" << open << " cheek=" << cheek;
      }
    }
  }
}

TEST(LensEyesTest, TheInnerEndIsTheLeftEndOfTheLeftEyeAndTheOtherWayRound) {
  const LensLid left = ComputeLid(1.0, 2.0, 0.0, 0.0, true);
  const LensLid right = ComputeLid(1.0, 2.0, 0.0, 0.0, false);
  EXPECT_DOUBLE_EQ(left.a, right.b);
  EXPECT_DOUBLE_EQ(left.b, right.a);
  EXPECT_NE(left.a, left.b);
}

TEST(LensEyesTest, BlinkClosesHoldsAndOpensOnTheDocumentedSchedule) {
  const LensBlink b{.t0 = 10.0, .d = 1.0};
  EXPECT_EQ(LensBlinkValue(b, 9.9), 0.0);
  EXPECT_NEAR(LensBlinkValue(b, 10.0), 0.0, 1e-12);
  EXPECT_NEAR(LensBlinkValue(b, 10.05), 0.962963, 1e-6);
  EXPECT_EQ(LensBlinkValue(b, 10.09), 1.0);
  EXPECT_EQ(LensBlinkValue(b, 10.11), 1.0);
  EXPECT_NEAR(LensBlinkValue(b, 10.18), 0.5, 1e-6);
  EXPECT_EQ(LensBlinkValue(b, 10.25), 0.0);
  EXPECT_EQ(LensBlinkValue(b, 12.0), 0.0);
  // A slow blink takes proportionally longer.
  const LensBlink slow{.t0 = 0.0, .d = 2.2};
  EXPECT_EQ(LensBlinkValue(slow, 0.2), 1.0);
  EXPECT_GT(LensBlinkValue(slow, 0.45), 0.0);
}

TEST(LensEyesTest, SeveralBlinksTakeTheClosestEye) {
  const std::vector<LensBlink> blinks = {{0.0, 1.0}, {0.28, 1.0}};
  EXPECT_EQ(LensBlinksValue(blinks, 0.09), 1.0);
  EXPECT_EQ(LensBlinksValue(blinks, 0.37), 1.0);
  EXPECT_EQ(LensBlinksValue({}, 1.0), 0.0);
}

// ---- math and randomness ----------------------------------------------------------

TEST(LensMathTest, EasingsMatchThePrototype) {
  EXPECT_DOUBLE_EQ(LensEaseOut(0.0), 0.0);
  EXPECT_DOUBLE_EQ(LensEaseOut(1.0), 1.0);
  EXPECT_NEAR(LensEaseInOut(0.5), 0.5, 1e-12);
  EXPECT_NEAR(LensEaseInOut(0.25), 0.125, 1e-12);
  // beat(): from the prototype under node.
  EXPECT_NEAR(LensBeat(0.0), 0.367879, 1e-6);
  EXPECT_NEAR(LensBeat(0.27), 0.65, 1e-3);
  EXPECT_NEAR(LensBeat(1.3), 0.506221, 1e-6);
}

TEST(LensRandomTest, SeededSequenceMatchesThePrototypeAndRepeats) {
  // mulberry32(42) under node: 0.6011037519 0.4482905590 0.8524657935.
  SeededLensRandom r(42);
  EXPECT_NEAR(r.Next(), 0.6011037519, 1e-9);
  EXPECT_NEAR(r.Next(), 0.4482905590, 1e-9);
  EXPECT_NEAR(r.Next(), 0.8524657935, 1e-9);
  SeededLensRandom again(42);
  SeededLensRandom other(43);
  EXPECT_NEAR(again.Next(), 0.6011037519, 1e-9);
  EXPECT_NE(other.Next(), 0.6011037519);
}

TEST(LensRandomTest, RangeStaysInsideItsBounds) {
  SeededLensRandom r(7);
  for (int i = 0; i < 1000; ++i) {
    const double v = r.Range(-0.4, 0.4);
    EXPECT_GE(v, -0.4);
    EXPECT_LT(v, 0.4);
  }
}

}  // namespace
}  // namespace zephyrus::agent
