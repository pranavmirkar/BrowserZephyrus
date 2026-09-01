// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/installer/zephyrus_setup/mascot.h"

#include <math.h>

#include <algorithm>

namespace zephyrus_setup {

namespace {

constexpr double kPi = 3.14159265358979323846;

// --- The static artwork, transcribed from the source SVG's <defs>. ----------
//
// Transcribed rather than parsed on purpose: the geometry is fixed art, and a
// table the compiler can see beats shipping a parser and a copy of the markup.

// Body: the stepped "Z" silhouette.
constexpr Rect kTorso[] = {
    {44, 15, 24, 12, kBlue},  {92, 15, 24, 12, kBlue},
    {32, 27, 36, 12, kBlue},  {92, 27, 36, 12, kBlue},
    {32, 39, 96, 12, kBlue},  {20, 51, 120, 12, kBlue},
    {20, 63, 120, 12, kBlue}, {20, 75, 120, 12, kBlue},
    {20, 87, 120, 12, kBlue}, {20, 99, 120, 12, kBlue},
    {20, 111, 120, 8, kBlue},
};

constexpr Rect kLegs[] = {
    {28, 119, 14, 22, kBlue},
    {56, 119, 14, 22, kBlue},
    {90, 119, 14, 22, kBlue},
    {118, 119, 14, 22, kBlue},
};

constexpr Rect kEyeWhites[] = {
    {27, 65, 18, 18, kWhite},
    {95, 65, 18, 18, kWhite},
};

constexpr Rect kPupils[] = {
    {33, 70, 8, 8, kNavy},   {101, 70, 8, 8, kNavy},
    {34, 71, 3, 3, kWhite},  {102, 71, 3, 3, kWhite},
};

// Closed, upward-curving "happy" eyes: three stepped blocks each.
constexpr Rect kEyesHappy[] = {
    {27, 74, 5, 5, kWhite},  {32, 69, 5, 5, kWhite},  {37, 74, 5, 5, kWhite},
    {95, 74, 5, 5, kWhite},  {100, 69, 5, 5, kWhite}, {105, 74, 5, 5, kWhite},
};

// Takes the array by reference rather than as a pointer + length: Chromium
// builds with -Wunsafe-buffer-usage, which rejects raw pointer indexing, and
// the reference form keeps the bound in the type where the compiler can see it.
template <size_t N>
void Append(std::vector<Rect>& out, const Rect (&src)[N], float dx = 0,
            float dy = 0) {
  for (const Rect& entry : src) {
    Rect r = entry;
    r.x += dx;
    r.y += dy;
    out.push_back(r);
  }
}

// Triangle wave in [0,1] with period `period`, so a motion can ping-pong
// without accumulating drift the way a modulo-and-branch would.
double PingPong(double t, double period) {
  const double phase = fmod(t, period) / period;
  return phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0;
}

double EaseInOut(double x) {
  return x < 0.5 ? 2 * x * x : 1 - pow(-2 * x + 2, 2) / 2;
}

// The blink used by every open-eyed state. Open almost always, shut briefly --
// a blink that is too slow reads as a character falling asleep.
bool IsBlinking(double t) {
  const double phase = fmod(t, 3.5);
  return phase > 3.36 && phase < 3.46;
}

}  // namespace

std::vector<Rect> BuildMascot(State state, double t) {
  std::vector<Rect> out;
  out.reserve(48);

  switch (state) {
    case State::kIdle: {
      // The state before anything has been asked of it: a slow breath and an
      // occasional blink. Deliberately almost still -- a character that fidgets
      // while waiting reads as impatient, and this one is waiting on the user.
      const float breath = static_cast<float>(-2.0 * PingPong(t, 2.2));
      Append(out, kTorso, 0, breath);
      if (IsBlinking(t)) {
        out.push_back({27, 72 + breath, 18, 4, kWhite});
        out.push_back({95, 72 + breath, 18, 4, kWhite});
      } else {
        Append(out, kEyeWhites, 0, breath);
        Append(out, kPupils, 0, breath);
      }
      Append(out, kLegs, 0, 0);
      break;
    }

    case State::kWorking: {
      // Body bob, and hands hammering in opposition so there is always one up.
      const float bob = static_cast<float>(-2.0 * PingPong(t, 0.8));
      Append(out, kTorso, 0, bob);
      if (IsBlinking(t)) {
        out.push_back({27, 72, 18, 4, kWhite});
        out.push_back({95, 72, 18, 4, kWhite});
      } else {
        Append(out, kEyeWhites, 0, bob);
        Append(out, kPupils, 0, bob);
      }
      Append(out, kLegs, 0, bob);

      const double swing = PingPong(t, 0.8);
      const float left_y = static_cast<float>(-12.0 + 14.0 * swing);
      const float right_y = static_cast<float>(2.0 - 14.0 * swing);
      out.push_back({6, 94 + left_y, 11, 15, kBlue});
      out.push_back({143, 94 + right_y, 11, 15, kBlue});

      // Impact sparks, visible only at the bottom of each swing.
      if (swing > 0.82) {
        out.push_back({0, 118, 5, 5, kBlueLight});
        out.push_back({10, 124, 4, 4, kBlueLight});
      }
      if (swing < 0.18) {
        out.push_back({155, 118, 5, 5, kBlueLight});
        out.push_back({146, 124, 4, 4, kBlueLight});
      }
      break;
    }

    case State::kCarrying: {
      // Walk cycle: legs alternate, body bobs at twice the leg frequency.
      const double cycle = fmod(t, 0.7) / 0.7;
      const float bob = (cycle < 0.25 || cycle > 0.75) ? 0.0f : -3.0f;
      const float file_bob = static_cast<float>(-3.0 * PingPong(t, 0.7));

      // The payload, held overhead.
      out.push_back({70, 2 + file_bob, 20, 26, kWhite});
      out.push_back({84, 2 + file_bob, 6, 6, kBackground});
      out.push_back({84, 8 + file_bob, 6, 6, kBlueLight});
      out.push_back({74, 12 + file_bob, 12, 3, kBlue});
      out.push_back({74, 18 + file_bob, 12, 3, kBlue});
      out.push_back({64, 22 + file_bob, 8, 12, kBlue});
      out.push_back({88, 22 + file_bob, 8, 12, kBlue});

      const float dy = 28.0f + bob;
      Append(out, kTorso, 0, dy);
      if (IsBlinking(t)) {
        out.push_back({27, 72 + dy, 18, 4, kWhite});
        out.push_back({95, 72 + dy, 18, 4, kWhite});
      } else {
        Append(out, kEyeWhites, 0, dy);
        Append(out, kPupils, 0, dy);
      }

      // Legs step out of phase with each other.
      const float step = static_cast<float>(6.0 * (PingPong(t, 0.7) * 2 - 1));
      out.push_back({28 + step, 147, 14, 22, kBlue});
      out.push_back({90 + step, 147, 14, 22, kBlue});
      out.push_back({56 - step, 147, 14, 22, kBlue});
      out.push_back({118 - step, 147, 14, 22, kBlue});
      break;
    }

    case State::kThinking: {
      // Slow rock plus wandering pupils; the dots climb away from the head.
      const float lean = static_cast<float>(2.0 * (PingPong(t, 2.6) * 2 - 1));
      Append(out, kTorso, lean, 0);
      Append(out, kEyeWhites, lean, 0);
      Append(out, kPupils, lean + 2.0f, -4.0f);
      Append(out, kLegs, lean, 0);

      const double pulse = fmod(t, 1.6) / 1.6;
      auto dot = [&](float x, float y, float s, double offset) {
        const double p = fmod(pulse + offset, 1.0);
        if (p > 0.25 && p < 0.85) {
          out.push_back({x, y, s, s, kBlue});
        }
      };
      dot(114, 16, 6, 0.0);
      dot(126, 8, 8, 0.15);
      dot(140, 0, 10, 0.3);
      break;
    }

    case State::kHappy: {
      // A hop with squash on landing and stretch at the top -- the squash is
      // what stops it reading as a rigid sprite sliding up and down.
      const double cycle = fmod(t, 1.1) / 1.1;
      float hop = 0.0f;
      float squash = 1.0f;
      if (cycle < 0.15) {
        hop = 2.0f;
        squash = 0.92f;
      } else if (cycle < 0.45) {
        const double p = (cycle - 0.15) / 0.30;
        hop = static_cast<float>(2.0 - 16.0 * EaseInOut(p));
        squash = 1.06f;
      } else if (cycle < 0.70) {
        const double p = (cycle - 0.45) / 0.25;
        hop = static_cast<float>(-14.0 + 14.0 * EaseInOut(p));
        squash = 1.0f;
      } else if (cycle < 0.82) {
        hop = 2.0f;
        squash = 0.95f;
      }

      // Squash about the feet, so the character compresses onto the ground
      // rather than shrinking toward its own middle.
      const float pivot = 141.0f;
      auto squashed = [&](Rect r) {
        r.y = pivot + (r.y - pivot) * squash + hop;
        r.h *= squash;
        return r;
      };

      for (const Rect& r : kTorso) {
        out.push_back(squashed(r));
      }
      for (const Rect& r : kEyesHappy) {
        out.push_back(squashed(r));
      }
      // Open smile.
      out.push_back(squashed({66, 90, 6, 5, kWhite}));
      out.push_back(squashed({72, 93, 16, 5, kWhite}));
      out.push_back(squashed({88, 90, 6, 5, kWhite}));
      for (const Rect& r : kLegs) {
        out.push_back(squashed(r));
      }

      // Sparkles, offset in time so they do not blink in unison.
      auto sparkle = [&](float cx, float cy, double offset) {
        const double p = fmod(t + offset, 1.1) / 1.1;
        if (p > 0.35 && p < 0.65) {
          out.push_back({cx, cy, 6, 6, kBlueLight});
          out.push_back({cx - 6, cy + 6, 6, 6, kBlueLight});
          out.push_back({cx + 6, cy + 6, 6, 6, kBlueLight});
          out.push_back({cx, cy + 12, 6, 6, kBlueLight});
        }
      };
      sparkle(10, 30, 0.0);
      sparkle(142, 18, 0.25);
      break;
    }

    case State::kAlert: {
      // A short shake that settles, not a permanent jitter: the browser is
      // reporting a problem, not panicking about it.
      const double shake_phase = fmod(t, 1.8);
      float dx = 0.0f;
      if (shake_phase < 0.4) {
        dx = static_cast<float>(3.0 * sin(shake_phase * 8 * kPi));
      }
      Append(out, kTorso, dx, 0);
      // Furrowed brow above each eye -- the whole difference between "alert"
      // and "idle" at this size.
      out.push_back({24 + dx, 61, 11, 3, kNavy});
      out.push_back({93 + dx, 61, 11, 3, kNavy});
      Append(out, kEyeWhites, dx, 0);
      Append(out, kPupils, dx, 0);
      Append(out, kLegs, dx, 0);

      // Pulsing badge.
      const float scale =
          static_cast<float>(1.0 + 0.12 * PingPong(t, 1.0));
      const float bw = 16.0f * scale;
      const float bx = 132.0f - bw / 2.0f;
      const float by = 26.0f - bw / 2.0f;
      out.push_back({bx, by, bw, bw, kRed});
      out.push_back({bx + bw * 0.375f, by + bw * 0.19f, bw * 0.25f,
                     bw * 0.44f, kWhite});
      out.push_back({bx + bw * 0.375f, by + bw * 0.75f, bw * 0.25f,
                     bw * 0.19f, kWhite});
      break;
    }
  }

  return out;
}

}  // namespace zephyrus_setup
