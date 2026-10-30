// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Activities: the prototype's six (idle, working, searching, clicking, done,
// error) transcribed from agent-mascot.html with the same random-draw order,
// then the states added for what the browser does that the prototype had no
// state for. The new ones use only the existing pose keys, so any painter that
// can draw the prototype's states can draw these.

#include <algorithm>
#include <cmath>

#include "chrome/browser/zephyrus/agent/lens_model.h"
#include "chrome/browser/zephyrus/agent/lens_states.h"

namespace zephyrus::agent {

namespace {

using S = LensStates;

LensRgb Rgb(int r, int g, int b) {
  return {static_cast<double>(r), static_cast<double>(g), static_cast<double>(b)};
}

double Sign(double x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); }

// The prototype's shuffle(): Fisher-Yates from the end, one draw per swap.
template <typename T>
void Shuffle(LensModel& m, std::vector<T>& a) {
  for (int i = static_cast<int>(a.size()) - 1; i > 0; --i) {
    const int j = static_cast<int>(std::floor(S::Rand(m, 0, 1) * (i + 1)));
    std::swap(a[i], a[j]);
  }
}

}  // namespace

void LensStates::InitActivity(LensModel& m, LensState s) {
  LensMemory& M = m.mem_;
  switch (s) {
    // ---- prototype ----------------------------------------------------------
    case LensState::kIdle:
      m.blink_range_ = {2.2, 5.8};
      M.next = m.t_ + 0.6;
      break;
    case LensState::kWorking:
      m.blink_range_ = {4, 8};
      M.col = 0;
      M.line = 0;
      M.next = m.t_ + 0.1;
      break;
    case LensState::kSearching:
      m.blink_range_ = {3, 7};
      M.pts = {{-.92, -.1}, {.9, -.25}, {.05, -.88}, {-.65, .55},
               {.88, .45}, {0, .15},   {-.4, -.6},  {.5, .6}};
      Shuffle(m, M.pts);
      M.i = 0;
      M.next = m.t_ + 0.05;
      M.back.reset();
      break;
    case LensState::kClicking: {
      m.blink_range_ = {6, 10};
      // Order matters: the prototype evaluates tx = rand * (random < .5 ? -1 : 1).
      const double a = S::Rand(m, 0.4, 0.8);
      const double sign = S::Chance(m, 0.5) ? -1 : 1;
      M.t0 = m.t_;
      M.ph0 = m.t_;
      M.stage = 0;
      M.tx = a * sign;
      M.ty = S::Rand(m, -0.5, 0.4);
      M.gap = S::Rand(m, 0.2, 0.9);
      break;
    }
    case LensState::kDone:
      m.blink_range_ = {3, 6};
      M.t0 = m.t_;
      M.flashed = false;
      M.slow = false;
      M.looked = false;
      break;
    case LensState::kError:
      m.blink_range_ = {3.5, 7};
      M.t0 = m.t_;
      M.down = false;
      break;

    // ---- added for browser functions --------------------------------------------
    case LensState::kTyping:
      m.blink_range_ = {5, 9};
      M.next = m.t_ + 0.1;
      M.col = 0;
      M.a = m.t_ + S::Rand(m, 1.4, 2.6);  // when the next "Enter" lands
      break;
    case LensState::kReading:
      m.blink_range_ = {3, 6};
      M.t0 = m.t_;
      M.line = 0;
      M.a = 0;
      break;
    case LensState::kScrolling:
      m.blink_range_ = {4, 8};
      M.t0 = m.t_;
      M.a = 0;
      break;
    case LensState::kListening:
      m.blink_range_ = {4, 8};
      M.next = m.t_ + 0.2;
      break;
    case LensState::kSpeaking:
      m.blink_range_ = {3, 6};
      M.next = m.t_ + 0.2;
      break;
    case LensState::kAsking:
      m.blink_range_ = {3, 6};
      M.t0 = m.t_;
      M.next = m.t_ + 0.1;
      M.a = 0;
      break;
    case LensState::kHandoff:
      m.blink_range_ = {4, 8};
      M.t0 = m.t_;
      M.next = m.t_ + 0.2;
      M.a = 0;
      break;
    case LensState::kDownloading:
      m.blink_range_ = {4, 8};
      M.t0 = m.t_;
      break;
    case LensState::kLoading:
      m.blink_range_ = {5, 9};
      M.t0 = m.t_;
      break;
    case LensState::kSleeping:
      // Never blinks: the lids are already shut.
      m.blink_range_ = {400, 600};
      m.rate_ = 0.4;
      M.t0 = m.t_;
      break;
    case LensState::kGreeting:
      m.blink_range_ = {3, 6};
      M.t0 = m.t_;
      M.looked = false;
      M.flashed = false;
      break;
    case LensState::kHeld:
      m.blink_range_ = {1.2, 2.4};
      m.rate_ = 1.3;
      M.t0 = m.t_;
      M.next = m.t_;
      break;
    case LensState::kRelieved:
      m.blink_range_ = {3, 6};
      m.rate_ = 0.7;
      M.t0 = m.t_;
      M.slow = false;
      break;
    case LensState::kSwitching:
      m.blink_range_ = {5, 9};
      M.t0 = m.t_;
      M.stage = 0;
      break;
    default:
      break;
  }
}

void LensStates::UpdateActivity(LensModel& m, LensState s, double t) {
  LensTargets& T = m.targets_;
  LensMemory& M = m.mem_;
  switch (s) {
    // ---- prototype ------------------------------------------------------------------
    case LensState::kIdle: {
      T.energy = 0.3;
      T.k = 1 + 0.035 * std::sin(t * 1.3);
      T.body_y = std::sin(t * 1.05) * 2.4;
      if (m.follow_cursor_ && m.cursor_ && t - m.cursor_t_ < 3) {
        m.Look((*m.cursor_)[0] * 0.95, (*m.cursor_)[1] * 0.8);
      } else if (t >= M.next) {
        const double r = S::Rand(m, 0, 1);
        double x, y;
        if (r < 0.22) {
          x = 0;
          y = 0;
        } else if (r < 0.7) {
          x = S::Rand(m, -0.4, 0.4);
          y = S::Rand(m, -0.3, 0.25);
        } else {
          x = S::Rand(m, -0.9, 0.9);
          y = S::Rand(m, -0.6, 0.5);
        }
        m.Look(x, y);
        M.next = t + S::Rand(m, 1, 3.6);
      }
      T.s = 0.5 * std::sin(t * 0.62) + 0.25 * std::sin(t * 0.27 + 1.7) +
            m.g_.x * 0.4;
      break;
    }
    case LensState::kWorking: {
      T.energy = 1;
      T.open = 0.72;
      T.tilt = 1.5;
      if (t >= M.next) {
        M.col++;
        double gap;
        if (M.col > 4) {
          M.col = 0;
          M.line = (M.line + 1) % 4;
          gap = 0.34;
        } else {
          gap = S::Rand(m, 0.16, 0.3);
        }
        m.Look(-0.62 + M.col * 0.31, -0.3 + M.line * 0.22);
        M.next = t + gap;
      }
      const double w = t * 3.7;
      T.s = std::sin(w) * 0.85;
      T.k = 1 + 0.05 * std::sin(w + 1.2);
      T.body_y = std::sin(w * 2) * 0.9;
      T.sy = 1 + 0.012 * std::sin(w * 2 + 0.5);
      break;
    }
    case LensState::kSearching: {
      T.energy = 0.75;
      T.sc = 1.08;
      T.tilt = -0.9;
      if (t >= M.next) {
        if (M.back) {
          const auto [x, y] = *M.back;
          M.back.reset();
          m.Look(x, y);
          M.next = t + S::Rand(m, 0.45, 0.8);
        } else {
          const auto p = M.pts[static_cast<size_t>(M.i++) % M.pts.size()];
          const double x = p[0] + S::Rand(m, -0.08, 0.08);
          const double y = p[1] + S::Rand(m, -0.08, 0.08);
          m.Look(x, y);
          if (S::Chance(m, 0.2)) {
            M.back = std::array<double, 2>{-x * 0.5, y * 0.3};
            M.next = t + 0.22;
          } else {
            M.next = t + S::Rand(m, 0.55, 1.05);
          }
        }
      }
      T.s = m.g_.x + std::sin(t * 2.2) * 0.06;
      T.k = 1 + 0.02 * std::sin(t * 2);
      T.body_y = std::sin(t * 1.4) * 1.5;
      T.beam_alpha = 0.4;
      m.beam_x_.t = 125.5 + m.g_.x * 95;
      break;
    }
    case LensState::kClicking: {
      const double ph = t - M.ph0;
      T.energy = 0.55;
      if (M.stage == 0 && ph >= 0.05) {
        m.Look(M.tx, M.ty);
        M.stage = 1;
      }
      if (ph < 0.5) {
        T.sc = 1.06;
        T.s = M.tx * 0.9;
      } else if (ph < 0.98) {
        T.open = 0.58;
        T.tilt = 2.2;
        T.sy = 0.985;
        T.energy = 0.7;
        T.s = M.tx * 0.9;
      } else if (ph < 1.1) {
        if (M.stage == 1) {
          M.stage = 2;
          m.Press(125.5 + m.g_.x * 95, 260 + m.g_.y * 60);
        }
        T.open = 0.58;
        T.tilt = 2.2;
        T.sy = 0.85;
        T.sx = 1.08;
        T.s = -M.tx * 0.9;
        T.energy = 1;
      } else if (ph < 1.7) {
        T.sc = 1.18;
        T.s = -M.tx * 0.35;
        T.energy = 1;
      } else {
        T.s = 0;
      }
      if (ph >= 2.4 + M.gap) {
        const double tx = -Sign(M.tx) * S::Rand(m, 0.35, 0.8);
        const double ty = S::Rand(m, -0.5, 0.4);
        const double gap = S::Rand(m, 0.2, 0.9);
        M.ph0 = t;
        M.stage = 0;
        M.tx = tx;
        M.ty = ty;
        M.gap = gap;
      }
      break;
    }
    case LensState::kDone: {
      const double ph = t - M.t0;
      T.energy = ph < 1.4 ? 1 : 0.5;
      T.open = 0.92;
      T.sc = 1.12;
      T.tilt = -0.5;
      T.pal = LensPalette::kWarm;
      T.pal_mix = ph < 2 ? 0.7 : 0.3;
      if (!M.looked) {
        M.looked = true;
        m.Look(0, -0.25);
      }
      T.s = ph < 0.9 ? -1 + 2 * LensEaseInOut(LensClamp(ph / 0.9, 0, 1))
                     : 0.5 * std::cos((ph - 0.9) * 1.0);
      T.sy = ph < 0.12 ? 1 : ph < 0.24 ? 0.88 : ph < 0.42 ? 1.1 : ph < 0.62 ? 0.96 : 1;
      T.sx = 2 - T.sy;
      if (!M.flashed && ph >= 0.24) {
        M.flashed = true;
        m.flash_v_ = 1;
      }
      if (!M.slow && ph >= 1.3) {
        M.slow = true;
        m.BlinkNow(2.2);
      }
      break;
    }
    case LensState::kError: {
      const double ph = t - M.t0;
      T.energy = 0.12;
      T.k = 0.93;
      T.open = 0.72;
      T.tilt = -2.6;
      T.sc = 0.96;
      T.sy = 0.975;
      T.body_y = 2;
      T.tint_color = Rgb(255, 61, 61);
      T.tint_alpha = 0.34 + 0.05 * std::sin(t * 3.2);
      T.pal = LensPalette::kRed;
      T.pal_mix = 0.55;
      T.eye_color = Rgb(220, 50, 50);
      T.eye_mix = 1;
      if (ph < 0.9) {
        const double a = 1 - ph / 0.9;
        const double w = std::sin(ph * kLensTau * 3.4);
        T.s = w * a;
        m.Look(w * 0.55 * a, 0.1);
      } else {
        T.s = 0;
        if (!M.down) {
          M.down = true;
          m.Look(0, 0.55);
        }
      }
      break;
    }

    // ---- added for browser functions -------------------------------------------------
    case LensState::kTyping: {
      // Eyes on the line being written, hands (body) busy: small quick moves,
      // a squash for each Enter.
      T.energy = 0.85;
      T.open = 0.74;
      T.tilt = 1.2;
      if (t >= M.next) {
        M.col = (M.col + 1) % 9;
        const double x = -0.45 + M.col * 0.11;
        const double y = 0.12 + (M.col == 0 ? 0.08 : 0.0);
        m.Look(x, y);
        M.next = t + S::Rand(m, 0.1, 0.28);
      }
      T.s = std::sin(t * 5.5) * 0.35;
      T.k = 1 + 0.03 * std::sin(t * 9);
      T.body_y = std::sin(t * 11) * 0.8;
      if (t >= M.a) {
        M.b = t;  // an Enter press
        M.a = t + S::Rand(m, 1.4, 2.6);
      }
      const double e = t - M.b;
      if (M.b > 0 && e < 0.16) {
        T.sy = 0.96;
        T.sx = 1.03;
      }
      break;
    }
    case LensState::kReading: {
      // Calmer than working: a slow sweep along each line, a small step down,
      // a long return.
      const double ph = t - M.t0;
      T.energy = 0.45;
      T.open = 0.8;
      T.tilt = 0.4;
      T.cheek = 0.1;
      const double period = 1.5;
      const int line = static_cast<int>(std::floor(ph / period)) % 5;
      const double along = std::fmod(ph, period) / period;
      m.Look(-0.6 + 1.2 * LensEaseInOut(along), -0.35 + line * 0.15);
      T.s = 0.15 * std::sin(t * 0.5);
      T.k = 1 + 0.02 * std::sin(t * 1.1);
      T.body_y = std::sin(t * 0.9) * 1.2;
      break;
    }
    case LensState::kScrolling: {
      // The gaze rides down a page and snaps back up for the next screenful.
      const double ph = t - M.t0;
      T.energy = 0.6;
      T.sc = 1.06;
      T.tilt = -0.4;
      T.open = 1;
      const double period = 1.7;
      const double along = std::fmod(ph, period) / period;
      m.Look(0.05 * std::sin(t * 1.3), -0.55 + 1.05 * along);
      T.s = 0.1 * std::sin(t * 0.8);
      T.k = 1 + 0.02 * std::sin(t * 1.6);
      T.body_y = 1.5 * along;
      T.lean = 1.2 * std::sin(t * 1.2);
      break;
    }
    case LensState::kListening: {
      // Attentive: wide, a little head tilt, eyes on the person, breathing with
      // an irregular pulse that stands in for a voice level.
      T.open = 1;
      T.sc = 1.1;
      T.tilt = -0.9;
      T.rot_b = 6;
      T.scale = 1.02;
      T.energy = 0.45 + 0.2 * std::sin(t * 3.1) * std::sin(t * 1.7 + 0.4);
      T.k = 1 + 0.025 * std::sin(t * 2.4);
      T.pal = LensPalette::kTeal;
      T.pal_mix = 0.6;
      T.tint_color = Rgb(40, 200, 190);
      T.tint_alpha = 0.1;
      T.eye_color = Rgb(30, 190, 170);
      T.eye_mix = 0.7;
      if (t >= M.next) {
        m.Look(S::Rand(m, -0.08, 0.08), -0.12 + S::Rand(m, -0.06, 0.06));
        M.next = t + S::Rand(m, 0.8, 1.8);
      }
      break;
    }
    case LensState::kSpeaking: {
      // No mouth: the whole body talks, in a rhythm, with the eyes warm.
      const double syll = std::abs(std::sin(t * 7.3)) *
                          (0.55 + 0.45 * std::abs(std::sin(t * 2.9 + 0.8)));
      T.open = 0.95;
      T.cheek = 0.15;
      T.sc = 1.04;
      T.energy = 0.8;
      T.scale = 1 + 0.025 * syll;
      T.k = 1 + 0.04 * syll;
      T.body_y = -2.2 * syll;
      T.s = 0.3 * std::sin(t * 1.9);
      T.pal = LensPalette::kWarm;
      T.pal_mix = 0.5;
      T.tint_color = Rgb(255, 205, 110);
      T.tint_alpha = 0.08;
      if (t >= M.next) {
        m.Look(S::Rand(m, -0.1, 0.1), -0.05 + S::Rand(m, -0.05, 0.05));
        M.next = t + S::Rand(m, 1, 2.2);
      }
      break;
    }
    case LensState::kAsking: {
      // Needs a decision: eyes on the person, then a glance toward where the
      // question is, then back; one eye a little bigger, a patient tilt.
      const double ph = t - M.t0;
      T.open = 1;
      T.open_r = 0.92;
      T.tilt = -1.4;
      T.tilt_r = -0.6;
      T.sc = 1.14;
      T.rot_b = 6;
      T.energy = 0.4;
      T.body_y = std::sin(t * 1.4) * 1.4;
      T.pal = LensPalette::kViolet;
      T.pal_mix = 0.5;
      T.tint_color = Rgb(140, 100, 220);
      T.tint_alpha = 0.1;
      if (t >= M.next) {
        if (M.a < 0.5) {
          m.Look(0.05, -0.15);
          M.next = t + 2.0;
          M.a = 1;
        } else {
          m.Look(0.6, 0.3);
          M.next = t + 0.8;
          M.a = 0;
        }
      }
      T.s = 0.25 * std::sin(ph * 0.9);
      break;
    }
    case LensState::kHandoff: {
      // "Your turn": looks politely away, leans out of the way, glances back now
      // and then to check it is still wanted.
      T.open = 0.9;
      T.sc = 1.02;
      T.tilt = -0.4;
      T.scale = 0.97;
      T.lean = -4;
      T.body_y = 2 + std::sin(t * 1.1) * 0.8;
      T.energy = 0.2;
      T.s = -0.35;
      T.pal = LensPalette::kTeal;
      T.pal_mix = 0.4;
      T.tint_color = Rgb(40, 200, 190);
      T.tint_alpha = 0.08;
      if (t >= M.next) {
        if (M.a < 0.5) {
          m.Look(-0.8, 0.1);
          M.next = t + S::Rand(m, 2.4, 3.6);
          M.a = 1;
        } else {
          m.Look(0.05, -0.05);
          M.next = t + 0.55;
          M.a = 0;
        }
      }
      break;
    }
    case LensState::kDownloading: {
      // Looking down at something arriving: a slow nod, a swelling pulse.
      const double ph = t - M.t0;
      T.open = 0.85;
      T.tilt = 0.3;
      T.energy = 0.4 + 0.3 * std::sin(ph * 2);
      T.k = 1 + 0.03 * std::sin(ph * 2.4);
      T.body_y = 3 * std::sin(ph * 1.6) * 0.5 + 1.5;
      T.s = 0.1 * std::sin(ph * 0.7);
      m.Look(0.05 * std::sin(ph * 0.9), 0.6);
      break;
    }
    case LensState::kLoading: {
      // Waiting on a page: the gaze circles slowly, like a spinner.
      const double ph = t - M.t0;
      T.open = 1;
      T.sc = 1.08;
      T.esy = 1.04;
      T.energy = 0.7;
      T.k = 1 + 0.02 * std::sin(t * 2.4);
      T.scale = 1 + 0.01 * std::sin(t * 2.4);
      T.pal = LensPalette::kTeal;
      T.pal_mix = 0.5;
      T.tint_color = Rgb(30, 120, 230);
      T.tint_alpha = 0.08;
      m.Look(0.5 * std::cos(ph * 2.4), 0.35 * std::sin(ph * 2.4));
      T.s = 0.35 * std::cos(ph * 2.4);
      break;
    }
    case LensState::kSleeping: {
      // Lids shut, a slow deep breath, everything low and dim.
      T.open = 0.03;
      T.tilt = -0.4;
      T.cheek = 0.15;
      T.eye_y = 2;
      T.s = -0.15 + 0.05 * std::sin(t * 0.5);
      T.k = 1 + 0.045 * std::sin(t * 0.9);
      T.body_y = 5;
      T.scale = 0.96;
      T.rot_b = -3;
      T.energy = 0.02;
      T.sat = 0.6;
      T.bri = 0.8;
      T.pal = LensPalette::kBlue;
      T.pal_mix = 0.7;
      T.tint_color = Rgb(40, 56, 100);
      T.tint_alpha = 0.32;
      T.eye_color = Rgb(49, 69, 110);
      T.eye_mix = 1;
      break;
    }
    case LensState::kGreeting: {
      // A wave hello, in a body with no arm: the whole body sways side to side
      // and bounces, then settles into a smile with a glint.
      const double ph = t - M.t0;
      T.open = 0.98;
      T.tilt = -0.5;
      T.cheek = 0.5;
      T.sc = 1.08;
      T.hl = LensHighlight::kStar;
      T.energy = 1;
      T.pal = LensPalette::kWarm;
      T.pal_mix = 0.7;
      T.tint_color = Rgb(255, 205, 110);
      T.tint_alpha = 0.12;
      T.eye_color = Rgb(32, 170, 255);
      T.eye_mix = 1;
      if (!M.looked) {
        M.looked = true;
        m.Look(0, -0.1);
      }
      if (ph < 2.4) {
        const double decay = 1 - LensEaseInOut(LensClamp((ph - 1.4) / 1.0, 0, 1));
        const double w = std::sin(ph * kLensTau * 1.6);
        T.rot_b = 9 * w * decay;
        T.s = 0.5 * w * decay;
        T.body_y = -3 * std::abs(w) * decay;
        T.k = 1.04;
      } else {
        T.s = 0.4;
        T.k = 1.03;
        if (!M.flashed) {
          M.flashed = true;
          m.BlinkNow(1.4);
        }
      }
      break;
    }
    case LensState::kHeld: {
      // Picked up by the person's cursor, hanging on: wide eyes, a pendulum
      // swing, a tremble.
      T.open = 1;
      T.tilt = -2;
      T.sc = 1.2;
      T.esy = 1.08;
      T.gap = 1.2;
      T.hl = LensHighlight::kDot;
      T.hs = 0.6;
      T.k = 0.88;
      T.scale = 0.94;
      T.body_y = -2;
      T.rot_b = 7 * std::sin(t * 2.6);
      T.lean = 3 * std::sin(t * 2.6 + 0.8);
      T.tremor = 0.6;
      T.hz = 17;
      T.energy = 0.6;
      T.sat = 0.8;
      T.pal = LensPalette::kViolet;
      T.pal_mix = 0.4;
      T.tint_color = Rgb(200, 205, 255);
      T.tint_alpha = 0.12;
      if (t >= M.next) {
        m.Look(S::Rand(m, -0.45, 0.45), S::Rand(m, -0.45, 0.15));
        M.next = t + S::Rand(m, 0.3, 0.7);
      }
      break;
    }
    case LensState::kRelieved: {
      // A long breath out: the eyes close slowly, the body sinks and settles,
      // then it looks up again.
      const double ph = t - M.t0;
      if (!M.slow) {
        M.slow = true;
        m.BlinkNow(2.4);
      }
      const double settle = LensEaseInOut(LensClamp(ph / 2.6, 0, 1));
      T.open = 0.5 + 0.5 * settle;
      T.cheek = 0.35 * (1 - settle) + 0.1;
      T.tilt = -0.6 * (1 - settle);
      T.k = 1 - 0.07 * std::sin(LensClamp(ph / 1.6, 0, 1) * 3.14159265);
      T.body_y = 4 * (1 - settle);
      T.s = 0.3;
      T.energy = 0.25;
      T.pal = LensPalette::kPink;
      T.pal_mix = 0.3;
      T.tint_color = Rgb(255, 150, 190);
      T.tint_alpha = 0.08 * (1 - settle);
      if (ph > 2.0 && ph < 2.1) {
        m.Look(0, -0.15);
      }
      break;
    }
    case LensState::kSwitching: {
      // A tab change: look one way, then the other, then settle on the new page.
      const double ph = std::fmod(t - M.t0, 3.2);
      T.open = 1;
      T.sc = 1.05;
      T.energy = 0.7;
      T.k = 1.02;
      if (ph < 0.25) {
        if (M.stage != 1) {
          M.stage = 1;
          m.Look(-0.8, 0);
        }
        T.s = -0.5;
      } else if (ph < 0.55) {
        if (M.stage != 2) {
          M.stage = 2;
          m.Look(0.8, 0);
        }
        T.s = 0.5;
      } else {
        if (M.stage != 3) {
          M.stage = 3;
          m.Look(0, -0.05);
        }
        T.s = 0.1;
        if (ph > 2.9) {
          M.stage = 0;
        }
      }
      break;
    }
    default:
      break;
  }
}

}  // namespace zephyrus::agent
