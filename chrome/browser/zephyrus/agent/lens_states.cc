// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The lens mascot's states: six activities and 25 emotions ported from the
// prototype (agent-mascot.html), then the states added for browser functions.
//
// The prototype's states are transcribed, not reinterpreted: the same numbers
// and, just as important, the same ORDER of random draws, because the port is
// checked against the prototype running under node with a seeded generator
// (lens_reference_unittest). Changing the order of two rand() calls here is a
// parity failure, not a refactor.

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "chrome/browser/zephyrus/agent/lens_model.h"
#include "chrome/browser/zephyrus/agent/lens_states.h"

namespace zephyrus::agent {

namespace {

constexpr double kPi = 3.14159265358979323846;

LensRgb Rgb(int r, int g, int b) {
  return {static_cast<double>(r), static_cast<double>(g), static_cast<double>(b)};
}

}  // namespace

// ---- emotion tables ------------------------------------------------------------

namespace {

struct EmoPose {
  std::optional<double> open, open_r, tilt, tilt_r, cheek, cheek_r, sc, sc_r,
      esx, esy, eye_y, rot, gap;
  std::optional<LensHighlight> hl;
  std::optional<double> hsx, hsy, hs, s, k, body_y, scale, rot_b, lean, tremor,
      hz, energy;
  std::optional<std::array<double, 4>> tint;
  std::optional<LensPalette> pal;
  std::optional<LensRgb> eye;
  std::optional<double> sat, bri;
  std::optional<std::array<double, 2>> blink;
  std::optional<double> rate;
};

struct EmoLook {
  double x, y, w, rate_lo, rate_hi;
};

using Osc = std::array<double, 2>;
using BehFn = void (*)(LensModel&, double t, double ph, double I,
                       LensTargets& T, LensMemory& M);
using InitFn = void (*)(LensModel&);

struct EmoDef {
  LensState state;
  EmoPose p;
  std::optional<EmoLook> look;
  std::optional<Osc> bob, sway, wob, breath;
  InitFn init = nullptr;
  BehFn beh = nullptr;
};

}  // namespace

namespace {

using S = LensStates;

// ---- emotion behaviours ------------------------------------------------------------

void JoyBeh(LensModel&, double t, double, double I, LensTargets& T,
            LensMemory&) {
  T.hs = 1 + 0.14 * std::sin(t * 7) * I;
}

void SadnessBeh(LensModel& m, double t, double ph, double I, LensTargets& T,
                LensMemory& M) {
  if (ph > 0.8 && I > 0.45 && t >= M.nt) {
    m.Tear(S::Chance(m, 0.5) ? 0 : 1);
    M.nt = t + S::Rand(m, 2.2, 4);
  }
  const double c = std::fmod(ph, 7.0) / 7;
  if (c < 0.3) {
    const double w = std::sin(c / 0.3 * kPi);
    T.k += 0.07 * w * I;
    T.body_y -= 3 * w * I;
  }
  T.hs = 1 + 0.04 * std::sin(t * 5) * I;
}

void AngerBeh(LensModel&, double t, double ph, double I, LensTargets& T,
              LensMemory&) {
  T.scale += 0.012 * std::sin(t * 5.5) * I;
  T.open -= 0.12 * LensClamp(ph / 2, 0, 1) * I;
  const double c = std::fmod(ph, 2.6);
  if (c < 0.4) {
    const double w = std::sin(c / 0.4 * kPi);
    T.scale += 0.07 * w * I;
    T.tremor += 1.8 * w * I;
    T.open -= 0.08 * w * I;
  }
}

void FearBeh(LensModel&, double, double ph, double I, LensTargets& T,
             LensMemory&) {
  const double c = std::fmod(ph, 2.5);
  if (c < 0.25) {
    T.body_y -= 5 * std::sin(c / 0.25 * kPi) * I;
  }
}

void SurpriseInit(LensModel& m) {
  m.Flash();
  m.AddRipple(125.5, 262);
}

void SurpriseBeh(LensModel&, double, double ph, double I, LensTargets& T,
                 LensMemory&) {
  if (ph < 0.1) {
    T.sx = 1.1;
    T.sy = 0.86;
    T.scale -= 0.1 * I;
    T.open = 0.5;
  } else if (ph < 0.5) {
    T.sx = 0.93;
    T.sy = 1.16;
    T.body_y -= 6 * I;
  }
}

void DisgustBeh(LensModel& m, double t, double, double I, LensTargets& T,
                LensMemory& M) {
  if (t >= M.ng) {
    if (M.away) {
      m.Look(0, 0.05);
      M.ng = t + 0.45;
      M.away = false;
      M.sh = t;
    } else {
      m.Look(-0.85 * I, 0.1);
      M.ng = t + S::Rand(m, 2.2, 3.4);
      M.away = true;
    }
  }
  const double a = t - M.sh;
  if (a < 0.45) {
    T.tremor += 2.4 * I;
    T.sx = 0.92;
    T.sy = 1.04;
  }
}

void LoveBeh(LensModel&, double t, double, double I, LensTargets& T,
             LensMemory&) {
  const double b = LensBeat(t * 1.3);
  T.hs = 1 + 0.3 * b * I;
  T.k += 0.03 * b * I;
  T.scale += 0.015 * b * I;
}

void ExcitedBeh(LensModel&, double t, double, double I, LensTargets& T,
                LensMemory&) {
  T.hs = 1.1 + 0.2 * std::sin(t * 9) * I;
}

void LaughingBeh(LensModel& m, double t, double ph, double I, LensTargets& T,
                 LensMemory& M) {
  const double c = std::fmod(ph, 2.7);
  if (c < 1.8) {
    const double x = c * kLensTau * 1.6;
    const double w = std::abs(std::sin(x / 2));
    T.body_y -= 7 * w * I;
    T.s += 0.8 * std::sin(x / 2);
    T.rot_b += 3 * std::sin(x / 2);
    T.sy = 1 + 0.05 * (1 - w) * I;
  }
  if (I > 0.5 && t >= M.nt) {
    m.Tear(S::Chance(m, 0.5) ? 0 : 1);
    M.nt = t + S::Rand(m, 0.8, 1.6);
  }
}

void PleadingBeh(LensModel& m, double t, double, double I, LensTargets& T,
                 LensMemory& M) {
  T.hs = 1 + 0.05 * std::sin(t * 7);
  if (I > 0.7 && t >= M.nt) {
    m.Tear(S::Chance(m, 0.5) ? 0 : 1);
    M.nt = t + S::Rand(m, 3.5, 5.5);
  }
}

void ShyBeh(LensModel& m, double t, double, double I, LensTargets&,
            LensMemory& M) {
  if (t >= M.ng) {
    if (M.peek) {
      m.Look(-0.7 * I, 0.5 * I);
      M.ng = t + S::Rand(m, 1.6, 2.8);
      M.peek = false;
    } else {
      m.Look(0, -0.05);
      M.ng = t + 0.4;
      M.peek = true;
      m.BlinkNow(0.9);
    }
  }
}

void ConfusedBeh(LensModel& m, double t, double, double I, LensTargets& T,
                 LensMemory& M) {
  if (t >= M.ng) {
    M.side *= -1;
    m.Look(0.6 * M.side * I, -0.15);
    M.ng = t + S::Rand(m, 0.8, 1.3);
  }
  T.s += 0.5 * M.side * I;
}

void ThinkingBeh(LensModel&, double t, double, double, LensTargets& T,
                 LensMemory&) {
  T.energy = 0.6 + 0.25 * std::sin(t * 2.2);
}

void SuspiciousBeh(LensModel& m, double t, double, double I, LensTargets&,
                   LensMemory& M) {
  if (t >= M.ng) {
    M.side *= -1;
    m.Look(0.85 * M.side * I, 0.05);
    M.ng = t + S::Rand(m, 1.6, 2.8);
  }
}

void DeterminedBeh(LensModel&, double t, double, double I, LensTargets& T,
                   LensMemory&) {
  T.scale += 0.035 * (std::sin(t * 5.6) * 0.5 + 0.5) * I;
}

void SmugBeh(LensModel&, double, double ph, double I, LensTargets& T,
             LensMemory&) {
  const double c = std::fmod(ph, 3.4);
  if (c < 0.7) {
    T.glint = std::sin(c / 0.7 * kPi) * I;
  }
}

void MischievousBeh(LensModel& m, double t, double ph, double I,
                    LensTargets& T, LensMemory& M) {
  if (t >= M.ng) {
    if (M.side > 0) {
      m.Look(0.85 * I, 0.15);
      M.ng = t + S::Rand(m, 1.2, 2);
      M.side = -1;
    } else {
      m.Look(-0.25, -0.05);
      M.ng = t + 0.35;
      M.side = 1;
    }
  }
  const double c = std::fmod(ph, 2.4);
  if (c > 1.5 && c < 2.1) {
    T.tremor += 1.4 * I;
    T.body_y -= 1.5 * std::abs(std::sin(t * kLensTau * 5)) * I;
  }
}

void WinkBeh(LensModel&, double, double ph, double I, LensTargets& T,
             LensMemory&) {
  const double c = std::fmod(ph, 3.4);
  const double w = LensEaseInOut(LensClamp((c - 1) / 0.12, 0, 1)) *
                   (1 - LensEaseInOut(LensClamp((c - 2.1) / 0.2, 0, 1)));
  T.cheek_r = w;
  T.sc_r = 1.08 - 0.1 * w;
  T.rot_b += 5 * w * I;
  T.body_y -= 3 * w * I;
  T.glint = w > 0.6 ? w : 0;
}

void BoredBeh(LensModel& m, double t, double ph, double, LensTargets&,
              LensMemory& M) {
  const double c = std::fmod(ph, 6.5);
  if (c > 4.8 && c < 6.2) {
    const double a = (c - 4.8) / 1.4 * kLensTau;
    M.hold = t + 0.1;
    m.Look(0.75 * std::cos(a + 1.6), 0.6 * std::sin(a + 1.6) - 0.1);
  }
}

void SleepyBeh(LensModel& m, double, double ph, double I, LensTargets& T,
               LensMemory& M) {
  const double c = std::fmod(ph, 7.5);
  if (c < 5.6) {
    const double d = LensClamp(c / 5, 0, 1);
    T.open = LensLerp(0.4, 0.05, d * d);
    T.body_y += 7 * d * I;
    T.rot_b += 3 * d * I;
  } else if (c < 6.4) {
    T.open = 1;
    T.sy = 1.08;
    T.sx = 0.96;
    T.body_y -= 3 * I;
    if (!M.jolt) {
      M.jolt = true;
      m.Look(0, -0.2);
    }
  } else {
    M.jolt = false;
  }
}

void CryingBeh(LensModel& m, double t, double ph, double I, LensTargets& T,
               LensMemory& M) {
  if (t >= M.nt) {
    m.Tear(0);
    m.Tear(1);
    M.nt = t + S::Rand(m, 0.35, 0.6);
  }
  const double c = std::fmod(ph, 1.8);
  if (c < 0.7) {
    const double sb = std::max(0.0, std::sin(c / 0.7 * kLensTau));
    T.body_y -= 4 * sb * I;
    T.k -= 0.05 * sb * I;
    T.scale += 0.02 * sb * I;
  }
}

void DizzyBeh(LensModel& m, double t, double, double I, LensTargets& T,
              LensMemory&) {
  T.rot_b = 10 * std::sin(t * 3.1) * I;
  T.lean = 7 * std::sin(t * 3.1 + 1.6) * I;
  T.s = 0.9 * std::sin(t * 2.2) * I;
  m.Look(0.35 * std::cos(t * 5), 0.3 * std::sin(t * 5));
}

// The 25 emotions, in the prototype's order. `.p` keys follow the declaration
// order of EmoPose; each row matches its entry in EMO in agent-mascot.html.
const std::array<EmoDef, 25>& Emotions() {
  using H = LensHighlight;
  using P = LensPalette;
  static const std::array<EmoDef, 25> table = {{
      {LensState::kJoy,
       {.open = .95, .tilt = -.4, .cheek = .7, .sc = 1.06, .hl = H::kStar,
        .s = .6, .k = 1.05, .body_y = -2, .energy = 1,
        .tint = std::array<double, 4>{255, 205, 110, .14}, .pal = P::kWarm,
        .eye = Rgb(32, 170, 255), .bri = 1.08,
        .blink = std::array<double, 2>{3, 6}},
       EmoLook{0, -.15, .25, 1.2, 2.5}, Osc{3, 2.2}, Osc{.5, .9}, std::nullopt,
       std::nullopt, nullptr, &JoyBeh},
      {LensState::kSadness,
       {.open = .62, .tilt = -3.4, .sc = .96, .eye_y = 2.2, .rot = 5,
        .hl = H::kBowl, .hsx = 1.25, .hsy = 1.35, .s = -.85, .k = .9,
        .body_y = 5, .scale = .94, .rot_b = -3, .energy = .05,
        .tint = std::array<double, 4>{52, 72, 125, .42}, .pal = P::kBlue,
        .eye = Rgb(44, 80, 150), .sat = .65, .bri = .82,
        .blink = std::array<double, 2>{4, 8}, .rate = .5},
       EmoLook{0, .62, .1, 2, 4}, std::nullopt, std::nullopt, std::nullopt,
       Osc{.05, .35}, nullptr, &SadnessBeh},
      {LensState::kAnger,
       {.open = .8, .tilt = 4.4, .cheek = .3, .rot = -3, .gap = -2.4,
        .hl = H::kBowl, .hsx = 1.1, .hsy = .3, .k = 1.03, .body_y = 1,
        .scale = 1.06, .tremor = .8, .hz = 15, .energy = .95,
        .tint = std::array<double, 4>{255, 40, 40, .4}, .pal = P::kRed,
        .eye = Rgb(220, 35, 40), .blink = std::array<double, 2>{8, 12},
        .rate = 1.3},
       EmoLook{0, .05, .04, 3, 5}, std::nullopt, std::nullopt, std::nullopt,
       std::nullopt, nullptr, &AngerBeh},
      {LensState::kFear,
       {.open = 1, .tilt = -2, .sc = 1.2, .esy = 1.08, .gap = 1.2,
        .hl = H::kDot, .hs = .5, .k = .85, .body_y = -3, .scale = .9,
        .tremor = 1.1, .hz = 19, .energy = .6,
        .tint = std::array<double, 4>{200, 205, 255, .2}, .pal = P::kViolet,
        .eye = Rgb(120, 110, 220), .sat = .7, .bri = 1.1,
        .blink = std::array<double, 2>{1.8, 3.2}, .rate = 1.5},
       EmoLook{0, 0, .9, .18, .5}, std::nullopt, Osc{.25, 6}, std::nullopt,
       std::nullopt, nullptr, &FearBeh},
      {LensState::kSurprise,
       {.open = 1, .tilt = -.6, .sc = 1.3, .esy = 1.2, .gap = 1.6,
        .hl = H::kDot, .hs = 1.15, .k = 1.13, .body_y = -4, .scale = 1.08,
        .energy = .9, .tint = std::array<double, 4>{255, 255, 255, .1},
        .pal = P::kTeal, .eye = Rgb(60, 200, 255), .bri = 1.1,
        .blink = std::array<double, 2>{5, 8}, .rate = 1.7},
       EmoLook{0, -.1, .05, 3, 5}, std::nullopt, std::nullopt, std::nullopt,
       std::nullopt, &SurpriseInit, &SurpriseBeh},
      {LensState::kDisgust,
       {.open = .64, .open_r = .74, .tilt = 2, .tilt_r = 1.2, .cheek = .5,
        .cheek_r = .35, .sc = .94, .rot = -3, .hsx = .9, .hsy = .7, .s = -.8,
        .k = .9, .scale = .96, .rot_b = 6, .lean = -5, .energy = .15,
        .tint = std::array<double, 4>{120, 150, 30, .32}, .pal = P::kGreen,
        .eye = Rgb(110, 160, 30), .sat = .8,
        .blink = std::array<double, 2>{3, 6}},
       std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
       nullptr, &DisgustBeh},
      {LensState::kContempt,
       {.open = .55, .open_r = .92, .tilt = .8, .tilt_r = -.6, .cheek = .35,
        .hsy = .8, .s = .5, .k = .98, .scale = 1.04, .rot_b = -5, .lean = 3,
        .energy = .2, .tint = std::array<double, 4>{120, 90, 200, .12},
        .pal = P::kPurple, .eye = Rgb(110, 70, 200),
        .blink = std::array<double, 2>{5, 9}},
       EmoLook{.35, .35, .05, 3, 6}, std::nullopt, std::nullopt, std::nullopt,
       std::nullopt, nullptr, nullptr},
      {LensState::kLove,
       {.open = .92, .tilt = -.8, .cheek = .45, .sc = 1.12, .gap = .8,
        .hl = H::kHeart, .s = .4, .k = 1.04, .energy = .7,
        .tint = std::array<double, 4>{255, 90, 150, .26}, .pal = P::kPink,
        .eye = Rgb(235, 70, 125), .blink = std::array<double, 2>{4, 7}},
       EmoLook{0, -.1, .2, 1.5, 3}, std::nullopt, Osc{.35, .5}, std::nullopt,
       std::nullopt, nullptr, &LoveBeh},
      {LensState::kExcited,
       {.open = 1, .tilt = -.5, .cheek = .35, .sc = 1.18, .hl = H::kStar,
        .hs = 1.15, .s = .8, .k = 1.08, .scale = 1.05, .tremor = .5, .hz = 22,
        .energy = 1, .tint = std::array<double, 4>{255, 190, 60, .16},
        .pal = P::kWarm, .eye = Rgb(255, 176, 32), .bri = 1.12,
        .blink = std::array<double, 2>{4, 7}, .rate = 1.4},
       EmoLook{0, -.2, .7, .35, .8}, Osc{5, 3.6}, Osc{.8, 2.2}, std::nullopt,
       std::nullopt, nullptr, &ExcitedBeh},
      {LensState::kLaughing,
       {.open = .98, .tilt = -.2, .cheek = 1, .sc = 1.05, .s = .5, .k = 1.06,
        .scale = 1.04, .energy = 1,
        .tint = std::array<double, 4>{255, 200, 100, .14}, .pal = P::kWarm,
        .eye = Rgb(32, 180, 255), .bri = 1.08,
        .blink = std::array<double, 2>{10, 12}},
       std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
       nullptr, &LaughingBeh},
      {LensState::kPleading,
       {.open = .96, .tilt = -3, .cheek = .1, .sc = 1.16, .eye_y = .5,
        .rot = 4, .gap = .6, .hsx = 1.5, .hsy = 1.6, .s = .2, .k = 1.04,
        .body_y = 1, .scale = .97, .rot_b = 5, .energy = .4,
        .tint = std::array<double, 4>{255, 150, 190, .2}, .pal = P::kPink,
        .eye = Rgb(60, 140, 255), .blink = std::array<double, 2>{3, 5}},
       EmoLook{0, -.45, .04, 2, 3}, Osc{1.2, 2.2}, std::nullopt, std::nullopt,
       std::nullopt, nullptr, &PleadingBeh},
      {LensState::kShy,
       {.open = .7, .tilt = -1.2, .cheek = .45, .sc = .96, .eye_y = 1.2,
        .hsx = 1.15, .hsy = 1.1, .s = -.4, .k = .94, .scale = .93,
        .rot_b = 5, .lean = 3, .energy = .35,
        .tint = std::array<double, 4>{255, 105, 150, .3}, .pal = P::kPink,
        .eye = Rgb(220, 100, 150), .blink = std::array<double, 2>{2, 4}},
       std::nullopt, std::nullopt, Osc{.3, .7}, std::nullopt, std::nullopt,
       nullptr, &ShyBeh},
      {LensState::kCurious,
       {.open = 1, .open_r = 1, .tilt = -.8, .tilt_r = .5, .sc = 1.18,
        .sc_r = 1, .s = .5, .k = 1.04, .scale = 1.02, .rot_b = 8,
        .energy = .5, .tint = std::array<double, 4>{40, 200, 190, .1},
        .pal = P::kTeal, .eye = Rgb(30, 190, 170),
        .blink = std::array<double, 2>{3, 5}},
       EmoLook{.45, -.4, .5, .7, 1.6}, Osc{1.5, 1.4}, std::nullopt,
       std::nullopt, std::nullopt, nullptr, nullptr},
      {LensState::kConfused,
       {.open = 1, .open_r = .62, .tilt = -2.6, .tilt_r = 2.6, .sc = 1.06,
        .sc_r = .96, .k = .98, .scale = .98, .rot_b = -6, .energy = .35,
        .tint = std::array<double, 4>{140, 100, 220, .14}, .pal = P::kViolet,
        .eye = Rgb(140, 92, 214), .blink = std::array<double, 2>{2, 4}},
       std::nullopt, std::nullopt, std::nullopt, Osc{5, .8}, std::nullopt,
       nullptr, &ConfusedBeh},
      {LensState::kThinking,
       {.open = .72, .tilt = .9, .cheek = .15, .hsx = .9, .hsy = .8, .s = .2,
        .rot_b = -4, .energy = .7,
        .tint = std::array<double, 4>{30, 120, 230, .1}, .pal = P::kTeal,
        .eye = Rgb(47, 111, 224), .blink = std::array<double, 2>{3, 6}},
       EmoLook{-.7, -.65, .12, 1.2, 2.4}, std::nullopt, Osc{.25, .4},
       std::nullopt, std::nullopt, nullptr, &ThinkingBeh},
      {LensState::kSuspicious,
       {.open = .5, .tilt = 1.3, .cheek = .5, .sc = .98, .gap = -1,
        .hsy = .5, .s = -.2, .k = .92, .body_y = 2, .scale = .96,
        .rot_b = -2, .energy = .2,
        .tint = std::array<double, 4>{170, 160, 30, .2}, .pal = P::kGreen,
        .eye = Rgb(170, 160, 30), .sat = .85,
        .blink = std::array<double, 2>{6, 10}},
       std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
       nullptr, &SuspiciousBeh},
      {LensState::kDetermined,
       {.open = .85, .tilt = 2.6, .cheek = .2, .sc = 1.04, .gap = -1.2,
        .hsx = 1.05, .hsy = .7, .s = .3, .k = 1.02, .body_y = -1,
        .scale = 1.05, .energy = .9,
        .tint = std::array<double, 4>{255, 106, 26, .2}, .pal = P::kRed,
        .eye = Rgb(255, 106, 26), .blink = std::array<double, 2>{8, 12}},
       EmoLook{.1, -.2, .03, 3, 6}, std::nullopt, std::nullopt, std::nullopt,
       std::nullopt, nullptr, &DeterminedBeh},
      {LensState::kSmug,
       {.open = .58, .open_r = .62, .tilt = .5, .cheek = .6, .cheek_r = .55,
        .sc = 1.02, .hsy = .8, .s = .65, .k = 1.03, .body_y = -2,
        .scale = 1.06, .rot_b = -6, .energy = .35,
        .tint = std::array<double, 4>{130, 90, 210, .12}, .pal = P::kPurple,
        .eye = Rgb(122, 79, 209), .blink = std::array<double, 2>{5, 8}},
       EmoLook{.25, -.1, .05, 3, 5}, std::nullopt, std::nullopt, std::nullopt,
       std::nullopt, nullptr, &SmugBeh},
      {LensState::kMischievous,
       {.open = .72, .tilt = 2.4, .cheek = .65, .rot = -4, .gap = -.6,
        .hsy = .8, .s = .4, .k = 1.02, .scale = 1.02, .rot_b = 3,
        .energy = .6, .tint = std::array<double, 4>{160, 60, 210, .18},
        .pal = P::kPurple, .eye = Rgb(155, 59, 214),
        .blink = std::array<double, 2>{4, 7}},
       std::nullopt, std::nullopt, std::nullopt, Osc{3, 3}, std::nullopt,
       nullptr, &MischievousBeh},
      {LensState::kWink,
       {.open = 1, .tilt = -.2, .sc = 1.08, .hs = 1, .s = .6, .k = 1.04,
        .rot_b = 0, .energy = .6,
        .tint = std::array<double, 4>{255, 205, 110, .1}, .pal = P::kWarm,
        .eye = Rgb(32, 170, 255), .blink = std::array<double, 2>{6, 9}},
       EmoLook{.1, -.05, .05, 2, 4}, std::nullopt, std::nullopt, std::nullopt,
       std::nullopt, nullptr, &WinkBeh},
      {LensState::kBored,
       {.open = .45, .tilt = .4, .sc = .98, .eye_y = 1.6, .hsx = .95,
        .hsy = .6, .s = -.5, .k = .95, .body_y = 3, .scale = .97,
        .rot_b = -3, .energy = .04,
        .tint = std::array<double, 4>{90, 100, 120, .3}, .pal = P::kBlue,
        .eye = Rgb(74, 85, 104), .sat = .6, .bri = .85,
        .blink = std::array<double, 2>{3, 5}, .rate = .6},
       EmoLook{.55, .3, .15, 2.5, 4}, std::nullopt, std::nullopt, std::nullopt,
       std::nullopt, nullptr, &BoredBeh},
      {LensState::kSleepy,
       {.open = .4, .tilt = -.6, .sc = .98, .eye_y = 2, .hsx = .9, .hsy = .5,
        .s = -.2, .body_y = 4, .energy = .02,
        .tint = std::array<double, 4>{40, 56, 100, .32}, .pal = P::kBlue,
        .eye = Rgb(49, 69, 110), .sat = .6, .bri = .8,
        .blink = std::array<double, 2>{40, 60}, .rate = .45},
       std::nullopt, std::nullopt, std::nullopt, std::nullopt, Osc{.06, .25},
       nullptr, &SleepyBeh},
      {LensState::kCrying,
       {.open = .58, .tilt = -3.4, .cheek = .45, .sc = .98, .eye_y = 2.2,
        .rot = 5, .hsx = 1.4, .hsy = 1.5, .s = -.9, .k = .88, .body_y = 4,
        .scale = .93, .rot_b = -2, .tremor = .6, .hz = 9, .energy = .08,
        .tint = std::array<double, 4>{40, 70, 150, .45}, .pal = P::kBlue,
        .eye = Rgb(43, 111, 214), .sat = .6, .bri = .8,
        .blink = std::array<double, 2>{4, 6}, .rate = .8},
       EmoLook{0, .6, .05, 2, 4}, std::nullopt, std::nullopt, std::nullopt,
       std::nullopt, nullptr, &CryingBeh},
      {LensState::kAnxious,
       {.open = 1, .tilt = -2.2, .sc = 1.1, .esy = 1.04, .hl = H::kDot,
        .hs = .75, .k = .9, .body_y = -1, .scale = .94, .tremor = .7,
        .hz = 16, .energy = .55,
        .tint = std::array<double, 4>{170, 185, 255, .16}, .pal = P::kViolet,
        .eye = Rgb(108, 138, 214), .sat = .8,
        .blink = std::array<double, 2>{.8, 1.8}, .rate = 1.3},
       EmoLook{0, .1, .55, .25, .7}, std::nullopt, std::nullopt, std::nullopt,
       Osc{.07, 2.4}, nullptr, nullptr},
      {LensState::kDizzy,
       {.open = 1, .sc = 1.22, .hl = H::kSpiral, .hs = 1.05, .k = 1.02,
        .energy = .8, .tint = std::array<double, 4>{190, 70, 255, .14},
        .pal = P::kViolet, .eye = Rgb(192, 64, 255),
        .blink = std::array<double, 2>{20, 30}},
       std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
       nullptr, &DizzyBeh},
  }};
  return table;
}

const EmoDef& EmotionFor(LensState state) {
  const int index = static_cast<int>(state) - static_cast<int>(LensState::kJoy);
  return Emotions()[index];
}

// emoUpdate(): pose keys present in the table are lerped from neutral by the
// intensity; the rest of the frame is oscillators, gaze wandering and the
// emotion's own behaviour.
void EmoUpdate(LensModel& m, double t, const EmoDef& def, double I,
               LensTargets& T, LensMemory& M) {
  const EmoPose& p = def.p;
  const double ph = t - M.t0;
  const auto lerp_to = [I](double neutral, const std::optional<double>& v,
                           double& out) {
    if (v) {
      out = LensLerp(neutral, *v, I);
    }
  };
  lerp_to(1, p.open, T.open);
  lerp_to(0, p.tilt, T.tilt);
  lerp_to(0, p.cheek, T.cheek);
  lerp_to(1, p.sc, T.sc);
  lerp_to(1, p.esx, T.esx);
  lerp_to(1, p.esy, T.esy);
  lerp_to(0, p.rot, T.rot);
  lerp_to(0, p.eye_y, T.eye_y);
  lerp_to(0, p.gap, T.gap);
  lerp_to(1, p.hsx, T.hsx);
  lerp_to(1, p.hsy, T.hsy);
  lerp_to(1, p.hs, T.hs);
  lerp_to(0, p.s, T.s);
  lerp_to(1, p.k, T.k);
  lerp_to(0, p.body_y, T.body_y);
  lerp_to(1, p.scale, T.scale);
  lerp_to(0, p.rot_b, T.rot_b);
  lerp_to(0, p.lean, T.lean);
  lerp_to(0, p.tremor, T.tremor);
  lerp_to(12, p.hz, T.hz);
  lerp_to(0.3, p.energy, T.energy);
  lerp_to(1, p.sat, T.sat);
  lerp_to(1, p.bri, T.bri);
  if (p.open_r) T.open_r = LensLerp(1, *p.open_r, I);
  if (p.tilt_r) T.tilt_r = LensLerp(0, *p.tilt_r, I);
  if (p.cheek_r) T.cheek_r = LensLerp(0, *p.cheek_r, I);
  if (p.sc_r) T.sc_r = LensLerp(1, *p.sc_r, I);
  T.hl = I >= 0.4 ? p.hl.value_or(LensHighlight::kBowl) : LensHighlight::kBowl;
  if (p.tint) {
    T.tint_color = {(*p.tint)[0], (*p.tint)[1], (*p.tint)[2]};
    T.tint_alpha = (*p.tint)[3] * I;
  }
  T.pal = p.pal.value_or(LensPalette::kNeutral);
  T.pal_mix = I;
  if (p.eye) {
    T.eye_color = *p.eye;
    T.eye_mix = I;
  }
  if (def.look && t >= std::max(M.nl, M.hold)) {
    const EmoLook& L = *def.look;
    const double x = LensClamp(L.x * I + S::Rand(m, -L.w, L.w), -1, 1);
    const double y = LensClamp(L.y * I + S::Rand(m, -L.w, L.w), -1, 1);
    m.Look(x, y);
    M.nl = t + S::Rand(m, L.rate_lo, L.rate_hi);
  }
  const auto osc = [&](const std::optional<Osc>& a, double f) {
    return a ? (*a)[0] * I * std::sin(t * (*a)[1] * kLensTau + f) : 0.0;
  };
  T.body_y += osc(def.bob, 0);
  T.s += osc(def.sway, 1);
  T.rot_b += osc(def.wob, 2);
  T.k += osc(def.breath, 3);
  if (def.beh) {
    def.beh(m, t, ph, I, T, M);
  }
}

}  // namespace

void LensStates::Init(LensModel& m, LensState s) {
  if (s >= LensState::kJoy && s < LensState::kTyping) {
    const EmoDef& def = EmotionFor(s);
    m.blink_range_ = def.p.blink ? *def.p.blink : std::array<double, 2>{2.5, 6};
    m.rate_ = def.p.rate.value_or(1.0);
    LensMemory& M = m.mem_;
    M.nl = m.t_ + 0.05;
    M.ng = m.t_;
    M.nt = m.t_ + 1.2;
    M.side = 1;
    M.away = false;
    M.hold = 0;
    M.peek = false;
    M.sh = -9;
    M.jolt = false;
    if (def.init) {
      def.init(m);
    }
    return;
  }
  InitActivity(m, s);
}

void LensStates::Update(LensModel& m, LensState s, double t) {
  if (s >= LensState::kJoy && s < LensState::kTyping) {
    EmoUpdate(m, t, EmotionFor(s), m.intensity_, m.targets_, m.mem_);
    return;
  }
  UpdateActivity(m, s, t);
}

}  // namespace zephyrus::agent
