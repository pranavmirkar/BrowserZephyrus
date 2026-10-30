// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Milestone 2: what the model does that the prototype comparison cannot say.
// The six activities and 25 emotions are held to the prototype by
// lens_reference_unittest; these cover every state (including the ones added
// for browser functions, which have no prototype to compare with), switching
// between states, intensity, reduced motion, and the model's contract.

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "chrome/browser/zephyrus/agent/lens_model.h"
#include "chrome/browser/zephyrus/agent/lens_random.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

constexpr double kDt = 1.0 / 60;

// Counts how many numbers were drawn, to prove a call draws none.
class CountingRandom : public LensRandom {
 public:
  explicit CountingRandom(uint32_t seed) : inner_(seed) {}
  double Next() override {
    ++draws;
    return inner_.Next();
  }
  int draws = 0;

 private:
  SeededLensRandom inner_;
};

void Advance(LensModel& m, double seconds) {
  const int n = static_cast<int>(std::lround(seconds / kDt));
  for (int i = 0; i < n; ++i) {
    m.Update(kDt);
  }
}

bool Finite(double v) { return std::isfinite(v); }

// Every number the painter is given must be finite and sane.
::testing::AssertionResult FrameIsSane(const LensFrame& f) {
  const double all[] = {f.s,           f.u,           f.k,
                        f.body_tx,     f.body_ty,     f.body_rot_deg,
                        f.body_sx,     f.body_sy,     f.face_rot_deg,
                        f.tint_opacity, f.beam_opacity, f.flash_opacity};
  for (double v : all) {
    if (!Finite(v)) {
      return ::testing::AssertionFailure() << "non-finite body value " << v;
    }
  }
  if (f.s < -1.1 || f.s > 1.1) {
    return ::testing::AssertionFailure() << "slant " << f.s;
  }
  if (f.k < 0.5 || f.k > 1.5) {
    return ::testing::AssertionFailure() << "breath k " << f.k;
  }
  if (f.body_sx < 0.5 || f.body_sx > 1.6 || f.body_sy < 0.5 || f.body_sy > 1.6) {
    return ::testing::AssertionFailure()
           << "squash " << f.body_sx << "," << f.body_sy;
  }
  if (std::abs(f.body_tx - 125.5) > 40 || std::abs(f.body_ty - 330) > 40) {
    return ::testing::AssertionFailure()
           << "body offset " << f.body_tx << "," << f.body_ty;
  }
  for (const LensEyeFrame& e : f.eyes) {
    const double v[] = {e.lid.a, e.lid.b, e.lid.yb, e.lid.yc, e.tx, e.ty,
                        e.rot_deg, e.sx, e.sy, e.hl_tx, e.hl_ty};
    for (double x : v) {
      if (!Finite(x)) {
        return ::testing::AssertionFailure() << "non-finite eye value " << x;
      }
    }
    if (e.sx < 0.3 || e.sx > 2 || e.sy < 0.3 || e.sy > 2) {
      return ::testing::AssertionFailure() << "eye scale " << e.sx << "," << e.sy;
    }
    if (e.lid.CenterGap() < 0.89) {
      return ::testing::AssertionFailure() << "lid gap " << e.lid.CenterGap();
    }
    if (std::abs(e.tx - 125.5) > 80 || e.ty < 240 || e.ty > 280) {
      return ::testing::AssertionFailure()
             << "eye position " << e.tx << "," << e.ty;
    }
  }
  if (f.tears.size() > 12) {
    return ::testing::AssertionFailure() << "tears " << f.tears.size();
  }
  for (const LensTearFrame& t : f.tears) {
    if (!Finite(t.x) || !Finite(t.y) || t.opacity < 0 || t.opacity > 1) {
      return ::testing::AssertionFailure() << "tear";
    }
  }
  return ::testing::AssertionSuccess();
}

// ---- the table of states --------------------------------------------------------

TEST(LensStatesTableTest, EveryStateHasAUniqueKeyAndALabelAndRoundTrips) {
  std::set<std::string> keys;
  int emotions = 0;
  for (int i = 0; i < kLensStateCount; ++i) {
    const LensState s = static_cast<LensState>(i);
    const std::string key = LensStateKey(s);
    EXPECT_FALSE(key.empty());
    EXPECT_FALSE(std::string(LensStateLabel(s)).empty());
    EXPECT_TRUE(keys.insert(key).second) << "duplicate key " << key;
    ASSERT_TRUE(LensStateFromKey(key).has_value()) << key;
    EXPECT_EQ(*LensStateFromKey(key), s);
    emotions += LensStateIsEmotion(s) ? 1 : 0;
  }
  EXPECT_EQ(emotions, 25) << "the prototype has 25 emotions";
  EXPECT_EQ(kLensPrototypeStateCount, 31) << "6 activities + 25 emotions";
  EXPECT_EQ(kLensStateCount - kLensPrototypeStateCount, 14)
      << "states added for browser functions";
  EXPECT_FALSE(LensStateFromKey("nonsense").has_value());
}

// ---- running every state ------------------------------------------------------------

TEST(LensModelTest, EveryStateRunsForTenSecondsInEveryFillWithSaneNumbers) {
  for (LensFill fill : {LensFill::kStatic, LensFill::kFluid}) {
    for (int i = 0; i < kLensStateCount; ++i) {
      const LensState s = static_cast<LensState>(i);
      SeededLensRandom rng(100 + i);
      LensModel m(&rng);
      m.SetFill(fill);
      m.SetState(s, true);
      for (int step = 0; step < 600; ++step) {
        m.Update(kDt);
        // Checked every 5th frame to keep the test quick.
        if (step % 5 == 0) {
          ASSERT_TRUE(FrameIsSane(m.ComputeFrame()))
              << LensStateKey(s) << " step " << step << " fill "
              << static_cast<int>(fill);
        }
      }
    }
  }
}

TEST(LensModelTest, EveryStateStaysBoundedForAMinuteAtLowAndFullIntensity) {
  // Drift, a runaway spring or a leak shows up over a long run.
  for (double intensity : {0.3, 1.0}) {
    for (int i = 0; i < kLensStateCount; ++i) {
      const LensState s = static_cast<LensState>(i);
      SeededLensRandom rng(7 * i + 1);
      LensModel m(&rng);
      m.SetIntensity(intensity);
      m.SetState(s, true);
      for (int step = 0; step < 3600; ++step) {
        m.Update(kDt);
      }
      ASSERT_TRUE(FrameIsSane(m.ComputeFrame()))
          << LensStateKey(s) << " intensity " << intensity;
      EXPECT_LE(m.tears().size(), 12u);
      EXPECT_LE(m.blinks().size(), 4u) << LensStateKey(s);
    }
  }
}

TEST(LensModelTest, ASeedMakesARunRepeatableAndADifferentSeedDiffers) {
  const auto trace = [](uint32_t seed, LensState state) {
    SeededLensRandom rng(seed);
    LensModel m(&rng);
    m.SetState(state, true);
    std::vector<double> out;
    for (int i = 0; i < 300; ++i) {
      m.Update(kDt);
      out.push_back(m.GazeX());
      out.push_back(m.PoseValue(LensPose::kS));
    }
    return out;
  };
  for (LensState s : {LensState::kIdle, LensState::kSearching, LensState::kConfused,
                      LensState::kHeld, LensState::kTyping}) {
    EXPECT_EQ(trace(5, s), trace(5, s)) << LensStateKey(s);
    EXPECT_NE(trace(5, s), trace(6, s)) << LensStateKey(s);
  }
}

TEST(LensModelTest, SettingTheStateItAlreadyHasDrawsNothingAndForceReplaysIt) {
  CountingRandom rng(3);
  LensModel m(&rng);
  m.SetState(LensState::kSearching);
  const int after_first = rng.draws;
  m.SetState(LensState::kSearching);
  EXPECT_EQ(rng.draws, after_first) << "not forced: a no-op";
  m.SetState(LensState::kSearching, /*force=*/true);
  EXPECT_GT(rng.draws, after_first) << "forced: replays and shuffles again";
}

TEST(LensModelTest, ResetDrawsNothingAndReturnsToANeutralPose) {
  CountingRandom rng(9);
  LensModel m(&rng);
  m.SetState(LensState::kAnger, true);
  Advance(m, 3);
  EXPECT_LT(m.PoseValue(LensPose::kOpen), 0.95);
  const int before = rng.draws;
  m.Reset();
  EXPECT_EQ(rng.draws, before);
  EXPECT_EQ(m.time(), 0.0);
  EXPECT_DOUBLE_EQ(m.PoseValue(LensPose::kOpen), 1.0);
  EXPECT_DOUBLE_EQ(m.PoseValue(LensPose::kS), 0.0);
  EXPECT_TRUE(m.blinks().empty());
  EXPECT_TRUE(m.tears().empty());
}

// ---- switching ------------------------------------------------------------------------

TEST(LensModelTest, SwitchingBetweenAnyTwoStatesNeverCutsTheBody) {
  // A state only moves targets, so a switch is a glide. Measure, over every
  // ordered pair of states, the largest per-frame jump of the visible body in
  // the second after the switch.
  struct Worst {
    double v = 0;
    std::string pair;
  } worst_body, worst_eye;
  for (int a = 0; a < kLensStateCount; ++a) {
    for (int b = 0; b < kLensStateCount; ++b) {
      if (a == b) {
        continue;
      }
      SeededLensRandom rng(a * 100 + b);
      LensModel m(&rng);
      m.SetState(static_cast<LensState>(a), true);
      Advance(m, 2.5);
      LensFrame prev = m.ComputeFrame();
      double prev_open = m.PoseValue(LensPose::kOpen);
      double prev_tilt = m.PoseValue(LensPose::kTilt);
      double prev_cheek = m.PoseValue(LensPose::kCheek);
      m.SetState(static_cast<LensState>(b), true);
      for (int i = 0; i < 60; ++i) {
        m.Update(kDt);
        const LensFrame f = m.ComputeFrame();
        const double body = std::abs(f.body_ty - prev.body_ty) +
                            std::abs(f.body_tx - prev.body_tx) +
                            std::abs(f.body_rot_deg - prev.body_rot_deg) +
                            std::abs(f.face_rot_deg - prev.face_rot_deg);
        // The pose springs are what must glide. Eye position follows the
        // gaze, which is deliberately stiff (a saccade is fast).
        if (body > worst_body.v) {
          worst_body = {body, std::string(LensStateKey(static_cast<LensState>(a))) +
                                  "->" + LensStateKey(static_cast<LensState>(b))};
        }
        // The eyelid POSE glides; a blink is a separate, fast, deliberate
        // movement on top (its first frame alone moves the lid about 8 units).
        const double eye = std::abs(m.PoseValue(LensPose::kOpen) - prev_open) +
                           std::abs(m.PoseValue(LensPose::kTilt) - prev_tilt) +
                           std::abs(m.PoseValue(LensPose::kCheek) - prev_cheek);
        prev_open = m.PoseValue(LensPose::kOpen);
        prev_tilt = m.PoseValue(LensPose::kTilt);
        prev_cheek = m.PoseValue(LensPose::kCheek);
        if (eye > worst_eye.v) {
          worst_eye = {eye, std::string(LensStateKey(static_cast<LensState>(a))) +
                                "->" + LensStateKey(static_cast<LensState>(b))};
        }
        prev = f;
      }
    }
  }
  // A frame is 1/60 s. These bounds are far below a "cut" (a cut would move the
  // whole stretch of the pose in one frame, tens of units) and above what the
  // fastest legitimate snap (surprise, fear) does.
  EXPECT_LT(worst_body.v, 14.0) << worst_body.pair;
  // Lid tilt is in lid units (a frown is about +4, a worried brow about -3.4),
  // so a full swing is about 8 and the critically damped spring takes it at no
  // more than about 0.7 a frame; a cut would be the whole 8 in one frame.
  EXPECT_LT(worst_eye.v, 1.5) << worst_eye.pair;
}

// ---- intensity, motion scale, gaze, cursor ---------------------------------------------

TEST(LensModelTest, IntensityScalesHowFarAnEmotionMovesFromNeutral) {
  const auto cheek_at = [](double intensity) {
    SeededLensRandom rng(1);
    LensModel m(&rng);
    m.SetIntensity(intensity);
    m.SetState(LensState::kLaughing, true);
    Advance(m, 3);
    return m.PoseValue(LensPose::kCheek);
  };
  const double low = cheek_at(0.3);
  const double mid = cheek_at(0.6);
  const double full = cheek_at(1.0);
  EXPECT_LT(low, mid);
  EXPECT_LT(mid, full);
  EXPECT_NEAR(full, 1.0, 0.05);
  EXPECT_NEAR(low, 0.3, 0.05);
}

TEST(LensModelTest, IntensityIsClampedToTheDocumentedRange) {
  SeededLensRandom rng(1);
  LensModel m(&rng);
  m.SetIntensity(0.0);
  EXPECT_DOUBLE_EQ(m.intensity(), 0.3);
  m.SetIntensity(5.0);
  EXPECT_DOUBLE_EQ(m.intensity(), 1.0);
}

TEST(LensModelTest, ReducedMotionShrinksTheBodysTravel) {
  const auto travel = [](double amp) {
    SeededLensRandom rng(4);
    LensModel m(&rng);
    m.SetMotionScale(amp);
    m.SetState(LensState::kExcited, true);
    double lo = 1e9, hi = -1e9;
    for (int i = 0; i < 600; ++i) {
      m.Update(kDt);
      const LensFrame f = m.ComputeFrame();
      lo = std::min(lo, f.body_ty);
      hi = std::max(hi, f.body_ty);
    }
    return hi - lo;
  };
  EXPECT_LT(travel(0.35), 0.5 * travel(1.0));
}

TEST(LensModelTest, IdleFollowsTheCursorOnlyWhenAskedAndOnlyWhileItIsRecent) {
  SeededLensRandom rng(2);
  LensModel m(&rng);
  m.SetState(LensState::kIdle, true);
  m.SetFollowCursor(true);
  m.SetCursor(0.8, -0.5);
  Advance(m, 0.5);
  EXPECT_NEAR(m.GazeX(), 0.8 * 0.95, 0.05);
  EXPECT_NEAR(m.GazeY(), -0.5 * 0.8, 0.05);
  // Not followed once it has gone stale (3 s without a move).
  Advance(m, 4.0);
  EXPECT_GT(m.time(), 3.0);
  SeededLensRandom rng2(2);
  LensModel off(&rng2);
  off.SetState(LensState::kIdle, true);
  off.SetCursor(0.8, -0.5);  // follow not enabled
  Advance(off, 0.5);
  EXPECT_LT(off.GazeX(), 0.8 * 0.95 - 0.05) << "follow_cursor is off by default";
}

TEST(LensModelTest, ATearPoolHoldsAtMostTwelve) {
  SeededLensRandom rng(1);
  LensModel m(&rng);
  for (int i = 0; i < 40; ++i) {
    m.Tear(i % 2);
  }
  EXPECT_EQ(m.tears().size(), 12u);
  Advance(m, 2.0);
  EXPECT_TRUE(m.tears().empty()) << "tears fall away after 1.5 s";
}

TEST(LensModelTest, PressDrawsARippleThatFadesAndBlinks) {
  SeededLensRandom rng(1);
  LensModel m(&rng);
  m.Press(100, 250);
  EXPECT_FALSE(m.blinks().empty());
  m.Update(kDt * 10);
  LensFrame f = m.ComputeFrame();
  EXPECT_TRUE(f.ripples[0].active);
  EXPECT_NEAR(f.ripples[0].cx, 100, 1e-9);
  Advance(m, 1.2);
  f = m.ComputeFrame();
  EXPECT_FALSE(f.ripples[0].active) << "gone after 0.95 s";
}

TEST(LensModelTest, ASquashFlinchesTheBodyAndEasesBackByItself) {
  SeededLensRandom a(5);
  SeededLensRandom b(5);
  LensModel plain(&a);
  LensModel squashed(&b);
  plain.SetState(LensState::kIdle, true);
  squashed.SetState(LensState::kIdle, true);
  Advance(plain, 1.0);
  Advance(squashed, 1.0);
  squashed.Squash();
  double deepest = 0;
  for (int i = 0; i < 30; ++i) {
    plain.Update(kDt);
    squashed.Update(kDt);
    deepest = std::max(deepest, plain.PoseValue(LensPose::kSy) -
                                    squashed.PoseValue(LensPose::kSy));
  }
  EXPECT_GT(deepest, 0.08) << "the body squashes";
  Advance(plain, 1.5);
  Advance(squashed, 1.5);
  EXPECT_NEAR(plain.PoseValue(LensPose::kSy), squashed.PoseValue(LensPose::kSy), 0.01)
      << "and recovers on its own";
  EXPECT_NEAR(plain.PoseValue(LensPose::kSx), squashed.PoseValue(LensPose::kSx), 0.01);
}
TEST(LensModelTest, FluidCrossfadesInAndOutWithoutAFlashOfTheOtherLayer) {
  SeededLensRandom rng(1);
  LensModel m(&rng);
  EXPECT_FALSE(m.ComputeFrame().layers.show_fluid);
  EXPECT_FALSE(m.ComputeFrame().fluid.computed) << "hidden layers do no work";
  m.SetFill(LensFill::kFluid);
  bool saw_both = false;
  for (int i = 0; i < 300; ++i) {
    m.Update(kDt);
    const LensFrame f = m.ComputeFrame();
    // Composited weights must always add up to one.
    const double fluid = (1 - f.layers.space) * (1 - f.layers.opal) * f.layers.fluid;
    const double stat = (1 - f.layers.space) * (1 - f.layers.opal) * (1 - f.layers.fluid);
    EXPECT_NEAR(fluid + stat, 1.0, 1e-9);
    if (f.layers.show_fluid && f.layers.show_static) {
      saw_both = true;
    }
  }
  EXPECT_TRUE(saw_both) << "a crossfade shows both layers on the way";
  const LensFrame done = m.ComputeFrame();
  EXPECT_TRUE(done.layers.show_fluid);
  EXPECT_FALSE(done.layers.show_static) << "fully fluid has no static layer";
  EXPECT_TRUE(done.fluid.computed);
  m.SetFill(LensFill::kStatic);
  Advance(m, 5);
  const LensFrame back = m.ComputeFrame();
  EXPECT_FALSE(back.layers.show_fluid);
  EXPECT_FALSE(back.fluid.computed);
}

// ---- the states added for browser functions -----------------------------------------------

LensModel* NewModel(LensRandom* rng, LensState s) {
  auto* m = new LensModel(rng);
  m->SetState(s, true);
  return m;
}

TEST(LensNewStatesTest, SleepingKeepsTheLidsShutAndNeverBlinks) {
  SeededLensRandom rng(1);
  std::unique_ptr<LensModel> m(NewModel(&rng, LensState::kSleeping));
  Advance(*m, 4);
  EXPECT_LT(m->PoseValue(LensPose::kOpen), 0.1);
  EXPECT_GT(m->next_blink_time(), 300) << "no blink is scheduled";
  const LensFrame f = m->ComputeFrame();
  // The dome sits above its base line (y < 0), so what shows is the strip
  // between the upper lid and the base, not the whole lid gap.
  const LensLid& lid = f.eyes[0].lid;
  const double lower_mid = (lid.yb + lid.yc) / 2;
  const double visible = std::min(0.0, lower_mid) - (lid.a + lid.b) / 2;
  EXPECT_LT(visible, 2.0) << "lids are almost closed (a full eye shows 12)";
  // And it breathes.
  double lo = 9, hi = -9;
  for (int i = 0; i < 600; ++i) {
    m->Update(kDt);
    lo = std::min(lo, m->PoseValue(LensPose::kK));
    hi = std::max(hi, m->PoseValue(LensPose::kK));
  }
  EXPECT_GT(hi - lo, 0.05);
}

TEST(LensNewStatesTest, TypingWatchesTheLineBelowAndSquashesOnEnter) {
  SeededLensRandom rng(1);
  std::unique_ptr<LensModel> m(NewModel(&rng, LensState::kTyping));
  double min_sy = 9, ysum = 0;
  int n = 0;
  for (int i = 0; i < 900; ++i) {
    m->Update(kDt);
    min_sy = std::min(min_sy, m->PoseValue(LensPose::kSy));
    ysum += m->GazeTargetY();
    ++n;
  }
  EXPECT_GT(ysum / n, 0.05) << "looks down at what is being written";
  EXPECT_LT(min_sy, 0.99) << "an Enter squashes the body";
  EXPECT_LT(m->PoseValue(LensPose::kOpen), 0.85) << "focused lids";
}

TEST(LensNewStatesTest, ReadingSweepsAlongALineAndStepsDown) {
  SeededLensRandom rng(1);
  std::unique_ptr<LensModel> m(NewModel(&rng, LensState::kReading));
  double min_x = 9, max_x = -9;
  std::set<int> lines;
  for (int i = 0; i < 60 * 9; ++i) {
    m->Update(kDt);
    min_x = std::min(min_x, m->GazeTargetX());
    max_x = std::max(max_x, m->GazeTargetX());
    lines.insert(static_cast<int>(std::lround(m->GazeTargetY() * 100)));
  }
  EXPECT_LT(min_x, -0.5);
  EXPECT_GT(max_x, 0.5);
  EXPECT_GE(lines.size(), 4u) << "several lines are read";
}

TEST(LensNewStatesTest, ScrollingRidesTheGazeDownAndSnapsBack) {
  SeededLensRandom rng(1);
  std::unique_ptr<LensModel> m(NewModel(&rng, LensState::kScrolling));
  double prev = m->GazeTargetY();
  int rises = 0, snaps = 0;
  for (int i = 0; i < 60 * 8; ++i) {
    m->Update(kDt);
    const double y = m->GazeTargetY();
    if (y > prev + 1e-9) {
      ++rises;
    }
    if (y < prev - 0.5) {
      ++snaps;
    }
    prev = y;
  }
  EXPECT_GT(rises, 200);
  EXPECT_GE(snaps, 3) << "returns to the top for the next screenful";
}

TEST(LensNewStatesTest, ListeningTiltsItsHeadAndSpeakingTalksWithItsBody) {
  SeededLensRandom rng(1);
  std::unique_ptr<LensModel> l(NewModel(&rng, LensState::kListening));
  Advance(*l, 3);
  EXPECT_NEAR(l->PoseValue(LensPose::kRotB), 6, 0.5);
  EXPECT_GT(l->PoseValue(LensPose::kSc), 1.05);

  SeededLensRandom rng2(1);
  std::unique_ptr<LensModel> s(NewModel(&rng2, LensState::kSpeaking));
  double lo = 9, hi = -9;
  for (int i = 0; i < 600; ++i) {
    s->Update(kDt);
    lo = std::min(lo, s->PoseValue(LensPose::kScale));
    hi = std::max(hi, s->PoseValue(LensPose::kScale));
  }
  EXPECT_GT(hi - lo, 0.01) << "pulses with the speech rhythm";
}

TEST(LensNewStatesTest, AskingAlternatesBetweenThePersonAndTheQuestion) {
  SeededLensRandom rng(1);
  std::unique_ptr<LensModel> m(NewModel(&rng, LensState::kAsking));
  std::set<int> targets;
  for (int i = 0; i < 60 * 8; ++i) {
    m->Update(kDt);
    targets.insert(m->GazeTargetX() > 0.3 ? 1 : 0);
  }
  EXPECT_EQ(targets.size(), 2u) << "looks at the person and at the question";
  EXPECT_LT(m->PoseValue(LensPose::kOpenR), m->PoseValue(LensPose::kOpen))
      << "one eye a little bigger";
}

TEST(LensNewStatesTest, HandoffLooksPolitelyAwayAndGlancesBack) {
  SeededLensRandom rng(1);
  std::unique_ptr<LensModel> m(NewModel(&rng, LensState::kHandoff));
  int away = 0, back = 0;
  for (int i = 0; i < 60 * 12; ++i) {
    m->Update(kDt);
    (m->GazeTargetX() < -0.5 ? away : back)++;
  }
  EXPECT_GT(away, back) << "mostly away";
  EXPECT_GT(back, 20) << "but it checks back";
  EXPECT_LT(m->PoseValue(LensPose::kLean), -2) << "out of the way";
}

TEST(LensNewStatesTest, DownloadingLooksDownAndLoadingCirclesItsGaze) {
  SeededLensRandom rng(1);
  std::unique_ptr<LensModel> d(NewModel(&rng, LensState::kDownloading));
  Advance(*d, 2);
  EXPECT_GT(d->GazeTargetY(), 0.5);

  SeededLensRandom rng2(1);
  std::unique_ptr<LensModel> l(NewModel(&rng2, LensState::kLoading));
  std::set<int> quadrants;
  for (int i = 0; i < 60 * 4; ++i) {
    l->Update(kDt);
    quadrants.insert((l->GazeTargetX() > 0 ? 1 : 0) + (l->GazeTargetY() > 0 ? 2 : 0));
  }
  EXPECT_EQ(quadrants.size(), 4u) << "visits every side, like a spinner";
}

TEST(LensNewStatesTest, GreetingWavesThenSettlesIntoASmile) {
  SeededLensRandom rng(1);
  std::unique_ptr<LensModel> m(NewModel(&rng, LensState::kGreeting));
  double early = 0, late = 0;
  for (int i = 0; i < 60 * 6; ++i) {
    m->Update(kDt);
    const double r = std::abs(m->PoseValue(LensPose::kRotB));
    if (m->time() < 1.4) {
      early = std::max(early, r);
    }
    if (m->time() > 3.5) {
      late = std::max(late, r);
    }
  }
  EXPECT_GT(early, 4) << "the wave";
  EXPECT_LT(late, 1.0) << "then it stops";
  EXPECT_GT(m->PoseValue(LensPose::kCheek), 0.4) << "and smiles";
}

TEST(LensNewStatesTest, HeldHangsOnWideEyedAndSwinging) {
  SeededLensRandom rng(1);
  std::unique_ptr<LensModel> m(NewModel(&rng, LensState::kHeld));
  double lo = 9, hi = -9;
  for (int i = 0; i < 60 * 5; ++i) {
    m->Update(kDt);
    lo = std::min(lo, m->PoseValue(LensPose::kRotB));
    hi = std::max(hi, m->PoseValue(LensPose::kRotB));
  }
  EXPECT_GT(hi - lo, 8) << "swings like a pendulum";
  EXPECT_GT(m->PoseValue(LensPose::kSc), 1.1) << "wide eyes";
  EXPECT_GT(m->PoseValue(LensPose::kTremor), 0.3) << "trembling";
}

TEST(LensNewStatesTest, RelievedClosesItsEyesSlowlyThenOpensThemAgain) {
  SeededLensRandom rng(1);
  std::unique_ptr<LensModel> m(NewModel(&rng, LensState::kRelieved));
  double early_open = 9;
  for (int i = 0; i < 60 * 1; ++i) {
    m->Update(kDt);
    early_open = std::min(early_open, m->PoseValue(LensPose::kOpen));
  }
  Advance(*m, 4);
  EXPECT_LT(early_open, 0.75) << "eyes close in the first second";
  EXPECT_GT(m->PoseValue(LensPose::kOpen), 0.9) << "then open again";
}

TEST(LensNewStatesTest, SwitchingGlancesLeftThenRightThenSettles) {
  SeededLensRandom rng(1);
  std::unique_ptr<LensModel> m(NewModel(&rng, LensState::kSwitching));
  bool saw_left = false, saw_right = false;
  double order_left = -1, order_right = -1;
  for (int i = 0; i < 60 * 1; ++i) {
    m->Update(kDt);
    if (m->GazeTargetX() < -0.7 && !saw_left) {
      saw_left = true;
      order_left = m->time();
    }
    if (m->GazeTargetX() > 0.7 && !saw_right) {
      saw_right = true;
      order_right = m->time();
    }
  }
  EXPECT_TRUE(saw_left && saw_right);
  EXPECT_LT(order_left, order_right) << "left first";
  EXPECT_NEAR(m->GazeTargetX(), 0, 0.1) << "then settles on the new page";
}

}  // namespace
}  // namespace zephyrus::agent
