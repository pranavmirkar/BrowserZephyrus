// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/mascot_rig.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace zephyrus::agent {

namespace {

using zephyrus_setup::kBlue;
using zephyrus_setup::kBlueLight;
using zephyrus_setup::kNavy;
using zephyrus_setup::kRed;
using zephyrus_setup::kWhite;
using zephyrus_setup::Rect;

constexpr float kPi = 3.14159265358979f;
constexpr float kFeet = 141.0f;
constexpr float kCenterX = 80.0f;
constexpr float kBodyBottom = 119.0f;
constexpr float kFrameWidth = 160.0f;

// Triangle wave in [0, 1].
float PingPong(double t, double period) {
  const double phase = std::fmod(t, period) / period;
  return static_cast<float>(phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0);
}

float Ease(float x) {
  x = std::clamp(x, 0.0f, 1.0f);
  return x < 0.5f ? 2 * x * x : 1 - std::pow(-2 * x + 2, 2.0f) / 2;
}

// Whether `t` (wrapped to `period`) falls in [from, to) of the period.
bool InPhase(double t, double period, double from, double to) {
  const double phase = std::fmod(t, period) / period;
  return phase >= from && phase < to;
}

// The body's transform: squash-and-stretch about the feet, a lean that shifts
// the top more than the bottom (rectangles cannot rotate, and a shear reads as
// a tilt at this size), and a translation. Everything attached to the body goes
// through it, so a hop moves the eyes, the mouth and the hands with the torso.
struct BodyTransform {
  float sx = 1;
  float sy = 1;
  float lean = 0;
  float dx = 0;
  float dy = 0;

  Rect Apply(Rect r) const {
    const float cx = r.x + r.w / 2;
    const float cy = r.y + r.h / 2;
    const float shear = lean * std::clamp((kBodyBottom - cy) / 104.0f, 0.0f, 1.0f);
    const float ncx = kCenterX + (cx - kCenterX) * sx + shear + dx;
    const float ncy = kFeet - (kFeet - cy) * sy + dy;
    r.w *= sx;
    r.h *= sy;
    r.x = ncx - r.w / 2;
    r.y = ncy - r.h / 2;
    return r;
  }
};

// A prop made of several rectangles, scaled about one shared point so the
// pieces stay together as it pops.
void AppendProp(std::vector<Rect>& out,
                std::span<const Rect> parts,
                float scale,
                float pivot_x,
                float pivot_y,
                float dx = 0,
                float dy = 0) {
  if (scale < 0.03f) {
    return;
  }
  for (const Rect& part : parts) {
    Rect r = part;
    r.x = pivot_x + (r.x - pivot_x) * scale + dx;
    r.y = pivot_y + (r.y - pivot_y) * scale + dy;
    r.w *= scale;
    r.h *= scale;
    out.push_back(r);
  }
}

}  // namespace

MascotRig::MascotRig() {
  // Every quantity's spring. Fast and critically damped where a value should
  // arrive without wobble (the eyes, the lid); slower and underdamped where
  // overshoot is the point (the body, the squash, props popping in).
  const auto tune = [&](Param p, float frequency, float damping) {
    springs_[p].frequency = frequency;
    springs_[p].damping = damping;
  };
  tune(kBodyDx, 4, 0.8f);
  tune(kBodyDy, 6, 0.55f);
  tune(kSquash, 7, 0.4f);
  tune(kLean, 3.5f, 0.6f);
  for (Param leg : {kLeg0, kLeg1, kLeg2, kLeg3}) {
    tune(leg, 10, 0.8f);
  }
  tune(kLegSwing, 10, 0.8f);
  tune(kPupilX, 8, 0.9f);
  tune(kPupilY, 8, 0.9f);
  tune(kPupilSize, 6, 0.7f);
  tune(kPupilSplit, 6, 0.8f);
  tune(kLid, 16, 1.0f);
  tune(kBrow, 8, 0.8f);
  tune(kBrowRaise, 9, 0.6f);
  tune(kBlush, 5, 1.0f);
  tune(kMouth, 9, 0.8f);
  tune(kSmile, 9, 1.0f);
  tune(kHappyEyes, 9, 1.0f);
  for (Param hand : {kHandLOn, kHandROn}) {
    tune(hand, 7, 0.5f);
  }
  for (Param hand : {kHandLX, kHandLY, kHandRX, kHandRY}) {
    tune(hand, 6, 0.6f);
  }
  for (Param prop :
       {kPropKeyboard, kPropGlass, kPropQuestion, kPropZ, kPropSparkle,
        kPropBadge, kPropDots, kPropWaves, kPropPage, kPropArrow, kPropDust,
        kPropSweat}) {
    tune(prop, 5, 0.45f);
  }
  tune(kGlassX, 6, 0.9f);

  Snap(kSquash, 1);
  Snap(kPupilSize, 8);
  Snap(kHandLX, 13);
  Snap(kHandLY, 101);
  Snap(kHandRX, 147);
  Snap(kHandRY, 101);
  ApplyMood();
}

void MascotRig::Snap(Param p, float value) {
  springs_[p].x = value;
  springs_[p].v = 0;
  springs_[p].target = value;
}

float MascotRig::Rand() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return static_cast<float>((rng_ >> 8) & 0xFFFF) / 65536.0f;
}

void MascotRig::SetMood(MascotMood mood) {
  if (mood == mood_) {
    return;
  }
  mood_ = mood;
  mood_time_ = 0;
  micro_ = 0;
  // A change of mood is announced by the body before the face: a small dip, as
  // if drawing breath. Cheap, and it is what stops the transition reading as a
  // swap.
  springs_[kSquash].v -= 2.5f;
  // Some moods have an entrance of their own.
  switch (mood) {
    case MascotMood::kAlert:
      // Caught off guard.
      Kick(MascotKick::kStartle);
      break;
    case MascotMood::kAsking:
      // A small rise, as if straightening to ask.
      springs_[kBodyDy].v -= 45;
      break;
    case MascotMood::kHappy:
      springs_[kBodyDy].v += 30;  // crouches, then the hop takes over
      break;
    default:
      break;
  }
}

void MascotRig::LookAt(float x, float y) {
  looking_ = true;
  look_x_ = std::clamp(x, -1.0f, 1.0f);
  look_y_ = std::clamp(y, -1.0f, 1.0f);
}

void MascotRig::StopLooking() {
  looking_ = false;
}

void MascotRig::SetTravel(float velocity) {
  travel_ = velocity;
}

void MascotRig::Kick(MascotKick kick) {
  switch (kick) {
    case MascotKick::kPress:
      springs_[kHandRY].v += 140;
      springs_[kSquash].v -= 3.0f;
      break;
    case MascotKick::kLand:
      springs_[kSquash].v -= 7.0f;
      springs_[kBodyDy].v += 150;
      break;
    case MascotKick::kStartle:
      springs_[kBodyDy].v -= 220;
      springs_[kSquash].v += 4.0f;
      springs_[kPupilSize].v += 60;
      break;
  }
}

void MascotRig::Update(base::TimeDelta dt) {
  const double step = std::clamp(dt.InSecondsF(), 0.0, 0.1);
  time_ += step;
  mood_time_ += step;

  // The idle life: a blink every few seconds, sometimes two, and a glance
  // somewhere else now and then. Running whatever the mood, so a character that
  // is working still blinks.
  if (blink_left_ > 0) {
    blink_left_ -= step;
  } else if (time_ >= next_blink_) {
    blink_left_ = 0.13;
    next_blink_ = time_ + (Rand() < 0.15f ? 0.35 : 2.2 + 3.0 * Rand());
  }
  if (time_ >= next_glance_) {
    const bool recentre = Rand() < 0.4f;
    glance_x_ = recentre ? 0.0f : (Rand() - 0.5f) * 7.0f;
    glance_y_ = recentre ? 0.0f : (Rand() - 0.5f) * 3.0f;
    next_glance_ = time_ + 1.4 + 3.0 * Rand();
  }

  // What it does when left alone. Only while idle: any other mood has its own
  // business, and a task starting ends whatever it was doing.
  if (mood_ == MascotMood::kIdle) {
    if (micro_ == 0) {
      if (time_ >= next_micro_) {
        micro_ = 1 + static_cast<int>(Rand() * 4.0f) % 4;
        micro_time_ = 0;
      }
    } else {
      micro_time_ += step;
      const double length = micro_ == 1 ? 1.4 : micro_ == 2 ? 0.75
                            : micro_ == 3 ? 1.8 : 1.6;
      if (micro_time_ > length) {
        micro_ = 0;
        next_micro_ = time_ + 8.0 + 8.0 * Rand();
      }
    }
  } else {
    micro_ = 0;
  }

  // Moods that finish by themselves.
  if (mood_ == MascotMood::kYawning && mood_time_ > 2.4) {
    SetMood(MascotMood::kSleeping);
  } else if (mood_ == MascotMood::kWaving && mood_time_ > 1.8) {
    SetMood(MascotMood::kIdle);
  }

  gait_ += std::abs(travel_) * step * 0.055;
  ApplyMood();
  ApplyGait();

  // Integrated in small fixed steps, so a long frame is many short ones.
  const int steps = std::max(1, static_cast<int>(std::ceil(step / (1.0 / 120))));
  const float h = static_cast<float>(step / steps);
  for (int i = 0; i < steps; ++i) {
    for (Spring& s : springs_) {
      // Implicit Euler: stable at any frequency and time step. The plain
      // (semi-implicit) form blows up once the frequency times the step passes
      // about 0.8 -- the 16Hz lid did, at 120 steps a second, and a sleeping
      // character's eyes flew open and shut at random. A spring here is a
      // smoothing filter, and one that can diverge is not.
      const float w = 2 * kPi * s.frequency;
      s.v = (s.v - w * w * h * (s.x - s.target)) /
            (1 + 2 * s.damping * w * h + w * w * h * h);
      s.x += s.v * h;
    }
  }
}

void MascotRig::ApplyGait() {
  if (std::abs(travel_) < 10.0f) {
    return;
  }
  // The walk: the legs alternate in pairs -- 0 and 2 against 1 and 3, as in the
  // installer's walk -- each pair lifting and swinging forward in turn, the body
  // bobbing twice a stride and leaning into the direction of travel.
  const float phase = static_cast<float>(gait_);
  const float a = std::max(0.0f, std::sin(phase));
  const float b = std::max(0.0f, std::sin(phase + kPi));
  Set(kLeg0, -7 * a);
  Set(kLeg2, -7 * a);
  Set(kLeg1, -7 * b);
  Set(kLeg3, -7 * b);
  Set(kLegSwing, 5 * std::sin(phase));
  springs_[kBodyDy].target -= 2.5f * std::abs(std::sin(phase));
  springs_[kLean].target += std::min(std::abs(travel_) * 0.012f, 4.0f);
}

void MascotRig::ApplyMood() {
  const double t = time_;
  const double m = mood_time_;

  // Where everything wants to be when the mood has no opinion.
  Set(kBodyDx, 0);
  Set(kBodyDy, 0);
  Set(kSquash, 1);
  Set(kLean, 0);
  for (Param leg : {kLeg0, kLeg1, kLeg2, kLeg3, kLegSwing}) {
    Set(leg, 0);
  }
  Set(kPupilX, looking_ ? look_x_ * 4 : glance_x_);
  Set(kPupilY, looking_ ? look_y_ * 3 : glance_y_);
  Set(kPupilSize, 8);
  Set(kPupilSplit, 0);
  Set(kLid, blink_left_ > 0 ? 1.0f : 0.0f);
  Set(kBrow, 0);
  Set(kBrowRaise, 0);
  Set(kBlush, 0);
  Set(kMouth, 0);
  Set(kSmile, 0);
  Set(kHappyEyes, 0);
  Set(kHandLOn, 0);
  Set(kHandLX, 13);
  Set(kHandLY, 101);
  Set(kHandROn, 0);
  Set(kHandRX, 147);
  Set(kHandRY, 101);
  for (Param prop :
       {kPropKeyboard, kPropGlass, kPropQuestion, kPropZ, kPropSparkle,
        kPropBadge, kPropDots, kPropWaves, kPropPage, kPropArrow, kPropDust,
        kPropSweat}) {
    Set(prop, 0);
  }
  Set(kGlassX, 0);

  switch (mood_) {
    case MascotMood::kIdle:
      // Almost still: a slow breath. A character that fidgets while it waits
      // reads as impatient, and this one is waiting on a person.
      Set(kBodyDy, -1.5f * PingPong(t, 2.2));
      if (micro_ != 0) {
        const float p = static_cast<float>(micro_time_);
        switch (micro_) {
          case 1: {
            // A stretch: both arms up and out, eyes squeezed, a little rise.
            const float k = std::max(0.0f, std::sin(kPi * p / 1.4f));
            Set(kHandLOn, k > 0.05f ? 1.0f : 0.0f);
            Set(kHandROn, k > 0.05f ? 1.0f : 0.0f);
            Set(kHandLX, 14);
            Set(kHandRX, 146);
            Set(kHandLY, 101 - 32 * k);
            Set(kHandRY, 101 - 32 * k);
            Set(kLid, 0.7f * k);
            Set(kBodyDy, -1.5f * PingPong(t, 2.2) - 2.5f * k);
            break;
          }
          case 2: {
            // A little hop, with the squash before it.
            const float q = p / 0.75f;
            Set(kSquash, q < 0.2f ? 0.93f : 1.0f);
            Set(kBodyDy, q < 0.2f ? 1.5f : -9.0f * std::sin(kPi * (q - 0.2f) / 0.8f));
            break;
          }
          case 3:
            // A look round the room.
            Set(kPupilX, 4.0f * std::sin(p * 2 * kPi / 1.8f));
            Set(kPupilY, -1.0f * std::sin(p * 4 * kPi / 1.8f));
            break;
          default: {
            // A tilt of the head at something.
            const float k = std::sin(kPi * p / 1.6f);
            Set(kLean, 2.5f * k);
            Set(kBrowRaise, k);
            Set(kPupilX, 2.5f * k);
            break;
          }
        }
      }
      break;

    case MascotMood::kSleeping:
      Set(kBodyDy, 5.0f - 5.0f * PingPong(t, 3.0));
      Set(kSquash, 0.97f);
      Set(kLid, 1);
      Set(kPupilX, 0);
      Set(kPupilY, 0);
      Set(kPropZ, 1);
      break;

    case MascotMood::kYawning: {
      const bool stretching = m > 0.5 && m < 1.9;
      const float open = (m > 0.4 && m < 2.0)
                             ? std::max(0.0f, std::sin(kPi * static_cast<float>(m - 0.4) / 1.6f))
                             : 0.0f;
      Set(kMouth, open);
      Set(kLid, stretching ? 0.85f : (blink_left_ > 0 ? 1.0f : 0.0f));
      Set(kBodyDy, stretching ? -2.0f : 0.0f);
      Set(kHandLOn, stretching ? 1.0f : 0.0f);
      Set(kHandROn, stretching ? 1.0f : 0.0f);
      Set(kHandLX, 14);
      Set(kHandRX, 146);
      Set(kHandLY, stretching ? 70.0f : 101.0f);
      Set(kHandRY, stretching ? 70.0f : 101.0f);
      break;
    }

    case MascotMood::kWaving:
      Set(kBrowRaise, 0.5f);
      Set(kBlush, 0.6f);
      Set(kHandROn, 1);
      Set(kHandRX, 154 + 5 * std::sin(static_cast<float>(m) * 14));
      Set(kHandRY, 60);
      Set(kSmile, 1);
      Set(kBodyDy, -1.0f * PingPong(t, 0.5));
      break;

    case MascotMood::kListening:
      // Hands cupped to the sides of the head, leaning toward the sound.
      Set(kBrowRaise, 0.35f);
      Set(kLean, -3);
      Set(kHandLOn, 1);
      Set(kHandROn, 1);
      Set(kHandLX, 14);
      Set(kHandRX, 146);
      Set(kHandLY, 62);
      Set(kHandRY, 62);
      Set(kPupilX, -3 + 2 * std::sin(static_cast<float>(t) * 2.6f));
      Set(kPropWaves, 1);
      break;

    case MascotMood::kThinking:
      Set(kLean, 2 * std::sin(static_cast<float>(t) * 2 * kPi / 2.6f));
      Set(kPupilX, 2 + 2 * std::sin(static_cast<float>(t) * 1.3f));
      Set(kPupilY, -4);
      Set(kPropDots, 1);
      // Brows that go up and down as the thought goes round, and a bead of
      // sweat once it has been at it a while.
      Set(kBrowRaise, 0.5f * PingPong(t, 2.6));
      Set(kPropSweat, m > 7.0 ? 1.0f : 0.0f);
      break;

    case MascotMood::kReading: {
      // Eyes travel along a line of text, snap back, and step down to the next.
      const float line = static_cast<float>(std::fmod(t * 1.4, 1.0));
      Set(kPupilX, -4 + 8 * line);
      Set(kPupilY, -3 + 3 * static_cast<float>(std::fmod(t * 0.45, 1.0)));
      Set(kPropPage, 1);
      Set(kBrowRaise, 0.25f);
      Set(kHandROn, 1);
      Set(kHandRX, 150);
      Set(kHandRY, 88);
      Set(kBodyDy, -0.8f * PingPong(t, 0.9));
      break;
    }

    case MascotMood::kSearching:
      Set(kPropGlass, 1);
      Set(kBrow, 0.4f);
      Set(kGlassX, 64 * Ease(PingPong(t, 1.6)));
      Set(kPupilX, -2 + 5 * PingPong(t, 1.6));
      break;

    case MascotMood::kTyping: {
      const float hand = 8 * PingPong(t, 0.32);
      Set(kHandLOn, 1);
      Set(kHandROn, 1);
      Set(kHandLY, 98 - hand);
      Set(kHandRY, 90 + hand);
      Set(kPropKeyboard, 1);
      Set(kBrow, 0.35f);
      Set(kPupilY, 3);
      Set(kBodyDy, -2.0f * PingPong(t, 0.6));
      break;
    }

    case MascotMood::kClicking:
      // The arm out toward the target; the fist dips on a press (Kick).
      Set(kHandROn, 1);
      Set(kHandRX, 160);
      Set(kHandRY, 79.5f);
      Set(kLean, 1.5f);
      Set(kBrow, 0.5f);  // determined
      Set(kPupilX, 3);
      break;

    case MascotMood::kScrolling:
      Set(kHandROn, 1);
      Set(kHandRX, 160);
      Set(kHandRY, 79.5f + 3 * std::sin(static_cast<float>(t) * 10));
      Set(kLean, 1.0f);
      Set(kPupilX, 3);
      Set(kPupilY, 3.0f * scroll_direction_);
      Set(kPropArrow, 1);
      break;

    case MascotMood::kAsking:
      // Wide eyes, palm up, a patient question mark.
      Set(kPupilSize, 11);
      Set(kBrowRaise, 1);
      Set(kPupilX, 0);
      Set(kPupilY, 0);
      Set(kHandROn, 1);
      Set(kHandRX, 150);
      Set(kHandRY, 86 - 4 * PingPong(t, 2.0));
      Set(kPropQuestion, 1);
      Set(kLean, 1.5f * PingPong(t, 2.4));
      break;

    case MascotMood::kHappy: {
      // A hop with a squash before it and a stretch at the top; the squash is
      // what stops it reading as a rigid sprite sliding up and down.
      const float p = static_cast<float>(std::fmod(m, 1.1) / 1.1);
      float hop = 0;
      float squash = 1;
      if (p < 0.15f) {
        hop = 2;
        squash = 0.92f;
      } else if (p < 0.45f) {
        hop = 2 - 16 * Ease((p - 0.15f) / 0.30f);
        squash = 1.06f;
      } else if (p < 0.70f) {
        hop = -14 + 14 * Ease((p - 0.45f) / 0.25f);
      } else if (p < 0.82f) {
        hop = 2;
        squash = 0.95f;
      }
      Set(kBodyDy, hop);
      Set(kSquash, squash);
      Set(kHappyEyes, 1);
      Set(kSmile, 1);
      Set(kBlush, 1);
      Set(kPropSparkle, 1);
      break;
    }

    case MascotMood::kConfused: {
      Set(kLean, 2 * std::sin(static_cast<float>(t) * 12.57f));
      Set(kBrow, -0.8f);  // sad, brows up at the inside
      Set(kPropSweat, m > 0.6 ? 1.0f : 0.0f);
      Set(kPupilSplit, 3 * PingPong(t, 1.6));
      const bool shrug = InPhase(t, 1.6, 0.62, 0.88);
      Set(kHandLOn, 1);
      Set(kHandROn, 1);
      Set(kHandLY, shrug ? 92.0f : 101.0f);
      Set(kHandRY, shrug ? 92.0f : 101.0f);
      Set(kPropDust, 1);
      break;
    }

    case MascotMood::kAlert:
      // A shake that settles, not a permanent jitter: the browser is reporting
      // a problem, not panicking about it.
      Set(kBrow, 1);
      Set(kLean, 3 * std::sin(static_cast<float>(m) * 8 * kPi) *
                     std::exp(-static_cast<float>(m) * 2.5f));
      Set(kPropBadge, 1);
      break;

    case MascotMood::kWorking: {
      const float swing = PingPong(t, 0.8);
      Set(kHandLOn, 1);
      Set(kHandROn, 1);
      Set(kHandLY, 89 + 14 * swing);
      Set(kHandRY, 103 - 14 * swing);
      Set(kBodyDy, -2.0f * PingPong(t, 0.8));
      Set(kBrow, 0.6f);
      Set(kPropSweat, m > 8.0 ? 1.0f : 0.0f);
      Set(kPropDust, 1);
      break;
    }
  }
}

std::vector<Rect> MascotRig::Draw() const {
  std::vector<Rect> out;
  out.reserve(80);

  BodyTransform body;
  body.sx = 1.0f / std::sqrt(std::max(0.5f, Get(kSquash)));
  body.sy = std::max(0.5f, Get(kSquash));
  body.lean = Get(kLean);
  body.dx = Get(kBodyDx);
  body.dy = Get(kBodyDy);

  const float time = static_cast<float>(time_);

  // ---- Behind the body ------------------------------------------------------
  if (Get(kPropZ) > 0.03f) {
    const struct {
      float x, y, w, bar, delay;
    } zs[] = {{106, 46, 10, 3, 0.0f}, {120, 26, 12, 4, 0.5f}, {136, 2, 14, 5, 1.0f}};
    for (const auto& z : zs) {
      const float p = static_cast<float>(std::fmod(time + 3.0f - z.delay, 3.0f) / 3.0f);
      if (p < 0.1f || p > 0.9f) {
        continue;
      }
      const float rise = 8 - 20 * p;
      const Rect parts[] = {
          {z.x, z.y + rise, z.w, z.bar, kBlueLight},
          {z.x + z.w / 2 - z.bar / 2 + 1, z.y + z.bar + rise, z.bar + 1, z.bar, kBlueLight},
          {z.x, z.y + 2 * z.bar + rise, z.w, z.bar, kBlueLight},
      };
      AppendProp(out, parts, Get(kPropZ), z.x + z.w / 2, z.y + rise + z.bar * 1.5f);
    }
  }
  if (Get(kPropWaves) > 0.03f) {
    const struct {
      float delay, x, top;
      int blocks;
    } waves[] = {{0.0f, 12, 60, 5}, {0.35f, 4, 52, 7}};
    for (const auto& w : waves) {
      const float p = static_cast<float>(std::fmod(time + 1.4f - w.delay, 1.4f) / 1.4f);
      if (p < 0.08f || p > 0.85f) {
        continue;
      }
      const float dx = 26 * p - 34;
      for (int i = 0; i < w.blocks; ++i) {
        const int from_middle = std::abs(i - w.blocks / 2);
        const Rect r{w.x + dx + 5 - from_middle * 2.5f, w.top + i * 6.0f, 5, 5,
                     kBlue};
        AppendProp(out, std::span<const Rect>(&r, 1), Get(kPropWaves), r.x + 2.5f, r.y + 2.5f);
      }
    }
  }
  if (Get(kPropDots) > 0.03f) {
    const Rect dots[] = {{114, 16, 6, 6, kBlue}, {126, 8, 8, 8, kBlue},
                         {140, 0, 10, 10, kBlue}};
    float phase = 0;
    for (const Rect& dot : dots) {
      const float pulse = 0.55f + 0.45f * PingPong(time + phase, 1.6);
      AppendProp(out, std::span<const Rect>(&dot, 1), Get(kPropDots) * pulse,
                 dot.x + dot.w / 2, dot.y + dot.h / 2);
      phase += 0.2f;
    }
  }
  if (Get(kPropQuestion) > 0.03f) {
    const Rect q[] = {
        {148, 10, 6, 6, kBlueLight}, {154, 10, 6, 6, kBlueLight},
        {142, 16, 6, 6, kBlueLight}, {160, 16, 6, 6, kBlueLight},
        {160, 22, 6, 6, kBlueLight}, {154, 28, 6, 6, kBlueLight},
        {154, 34, 6, 6, kBlueLight}, {154, 44, 6, 6, kBlueLight},
    };
    AppendProp(out, q, Get(kPropQuestion), 154, 30, 0, -5 * PingPong(time, 2.0));
  }
  if (Get(kPropSparkle) > 0.03f) {
    const struct {
      float cx, cy, offset;
    } stars[] = {{10, 30, 0.0f}, {142, 18, 0.25f}};
    for (const auto& s : stars) {
      const float p = static_cast<float>(std::fmod(time + s.offset, 1.1f) / 1.1f);
      const float pulse = (p > 0.3f && p < 0.7f)
                              ? std::sin(kPi * (p - 0.3f) / 0.4f)
                              : 0.0f;
      const Rect parts[] = {
          {s.cx, s.cy, 6, 6, kBlueLight},
          {s.cx - 6, s.cy + 6, 6, 6, kBlueLight},
          {s.cx + 6, s.cy + 6, 6, 6, kBlueLight},
          {s.cx, s.cy + 12, 6, 6, kBlueLight},
      };
      AppendProp(out, parts, Get(kPropSparkle) * pulse, s.cx + 3, s.cy + 9);
    }
  }

  // ---- Body -------------------------------------------------------------------
  for (const Rect& r : zephyrus_setup::TorsoArt()) {
    out.push_back(body.Apply(r));
  }

  // Eyes: the lid closes them from the middle, and happy eyes replace them
  // outright once the blend passes halfway.
  if (Get(kHappyEyes) > 0.5f) {
    for (const Rect& r : zephyrus_setup::EyesHappyArt()) {
      out.push_back(body.Apply(r));
    }
  } else {
    const float lid = std::clamp(Get(kLid), 0.0f, 1.0f);
    const float height = std::max(4.0f, 18.0f * (1.0f - lid));
    const float size = Get(kPupilSize);
    for (int side = 0; side < 2; ++side) {
      const float left = side == 0 ? 27.0f : 95.0f;
      out.push_back(body.Apply({left, 74 - height / 2, 18, height, kWhite}));
      if (lid < 0.85f) {
        const float split = side == 0 ? -Get(kPupilSplit) : Get(kPupilSplit);
        const float reach = (18.0f - size) / 2.0f;
        const float px = std::clamp(Get(kPupilX) + split, -reach, reach);
        const float py = std::clamp(Get(kPupilY), -reach, reach);
        const float ph = std::min(size, height);
        out.push_back(body.Apply(
            {left + 9 + px - size / 2, 74 + py - ph / 2, size, ph, kNavy}));
        const float glint = 3.0f * size / 8.0f;
        out.push_back(body.Apply({left + 9 + px - size / 2 + 1, 74 + py - ph / 2 + 1,
                                  glint, std::min(glint, ph - 1), kWhite}));
      }
    }
  }
  // Brows: two blocks each, the inner one stepped down (furrowed) or up (sad),
  // the whole pair lifted for surprise. Navy on the blue, over the eyes.
  if (std::abs(Get(kBrow)) > 0.12f || Get(kBrowRaise) > 0.12f) {
    const float step = std::clamp(Get(kBrow), -1.0f, 1.0f) * 2.6f;
    const float y = 60.0f - std::clamp(Get(kBrowRaise), 0.0f, 1.3f) * 6.0f;
    out.push_back(body.Apply({24, y - step * 0.3f, 6, 3.6f, kNavy}));
    out.push_back(body.Apply({30, y + step, 6, 3.6f, kNavy}));
    out.push_back(body.Apply({92, y + step, 6, 3.6f, kNavy}));
    out.push_back(body.Apply({98, y - step * 0.3f, 6, 3.6f, kNavy}));
  }
  if (Get(kBlush) > 0.2f) {
    const float b = std::clamp(Get(kBlush), 0.0f, 1.2f);
    out.push_back(body.Apply({28, 85, 6 * b, 2.5f * b, kBlueLight}));
    out.push_back(body.Apply({106 - 6 * (b - 1.0f), 85, 6 * b, 2.5f * b, kBlueLight}));
  }
  if (Get(kSmile) > 0.5f) {
    out.push_back(body.Apply({66, 90, 6, 5, kWhite}));
    out.push_back(body.Apply({72, 93, 16, 5, kWhite}));
    out.push_back(body.Apply({88, 90, 6, 5, kWhite}));
  } else if (Get(kMouth) > 0.06f) {
    out.push_back(body.Apply({70, 94, 20, 14 * Get(kMouth), kWhite}));
  }

  // ---- Legs -------------------------------------------------------------------
  const std::array<Param, 4> legs = {kLeg0, kLeg1, kLeg2, kLeg3};
  size_t index = 0;
  for (const Rect& art : zephyrus_setup::LegArt()) {
    Rect leg = art;
    leg.y += Get(legs.at(index));
    leg.x += (index % 2 == 0 ? 1.0f : -1.0f) * Get(kLegSwing);
    out.push_back(body.Apply(leg));
    ++index;
  }

  // ---- Hands and arms ---------------------------------------------------------
  for (int side = 0; side < 2; ++side) {
    const float on = std::clamp(Get(side == 0 ? kHandLOn : kHandROn), 0.0f, 1.3f);
    if (on < 0.03f) {
      continue;
    }
    const float x = Get(side == 0 ? kHandLX : kHandRX);
    const float y = Get(side == 0 ? kHandLY : kHandRY);
    // An arm joins the fist to the body whenever it is held out.
    if (side == 1 && x > 148) {
      out.push_back(body.Apply({140, y - 4 * on, x - 140, 8 * on, kBlue}));
    } else if (side == 0 && x < 12) {
      out.push_back(body.Apply({x, y - 4 * on, 20 - x, 8 * on, kBlue}));
    }
    out.push_back(body.Apply({x - 5 * on, y - 5 * on, 10 * on, 10 * on, kBlue}));
  }

  // ---- In front ---------------------------------------------------------------
  if (Get(kPropKeyboard) > 0.03f) {
    std::vector<Rect> keyboard;
    keyboard.push_back({40, 150, 80, 1.5f, kBlue});
    keyboard.push_back({40, 162.5f, 80, 1.5f, kBlue});
    keyboard.push_back({40, 150, 1.5f, 14, kBlue});
    keyboard.push_back({118.5f, 150, 1.5f, 14, kBlue});
    for (int i = 0; i < 3; ++i) {
      const bool lit = InPhase(time + i * 0.18, 1.1, 0.1, 0.3);
      keyboard.push_back({50.0f + 27 * i, 155, 6, 4, lit ? kBlueLight : kBlue});
    }
    AppendProp(out, keyboard, Get(kPropKeyboard), 80, 157);
  }
  if (Get(kPropPage) > 0.03f) {
    const Rect page[] = {
        {150, 74, 16, 22, kWhite}, {153, 79, 10, 2, kBlue},
        {153, 84, 10, 2, kBlue},   {153, 89, 7, 2, kBlue},
    };
    std::vector<Rect> attached;
    for (const Rect& r : page) {
      attached.push_back(body.Apply(r));
    }
    AppendProp(out, attached, Get(kPropPage), 158, 85);
  }
  if (Get(kPropGlass) > 0.03f) {
    const float gx = Get(kGlassX);
    const Rect glass[] = {
        {22 + gx, 58, 26, 3.5f, kBlueLight},    {22 + gx, 80.5f, 26, 3.5f, kBlueLight},
        {22 + gx, 58, 3.5f, 26, kBlueLight},    {44.5f + gx, 58, 3.5f, 26, kBlueLight},
        {47 + gx, 84, 5, 5, kBlueLight},        {51 + gx, 88, 5, 5, kBlueLight},
        {55 + gx, 92, 5, 5, kBlueLight},
    };
    AppendProp(out, glass, Get(kPropGlass), 35 + gx, 71);
  }
  if (Get(kPropArrow) > 0.03f) {
    const float dir = static_cast<float>(scroll_direction_);
    const float slide = dir * 4 * PingPong(time, 0.6);
    const float top = dir > 0 ? 64.0f : 69.0f;
    const float mid = dir > 0 ? 69.0f : 64.0f;
    const Rect chevron[] = {
        {168, top, 5, 5, kBlueLight},
        {173, mid, 5, 5, kBlueLight},
        {178, top, 5, 5, kBlueLight},
    };
    AppendProp(out, chevron, Get(kPropArrow), 175, 68, 0, slide);
  }
  if (Get(kPropDust) > 0.03f) {
    if (mood_ == MascotMood::kWorking) {
      // Sparks at the bottom of each hammer swing.
      const float swing = PingPong(time, 0.8);
      if (swing > 0.8f) {
        const Rect sparks[] = {{0, 118, 5, 5, kBlueLight}, {10, 124, 4, 4, kBlueLight}};
        AppendProp(out, sparks, Get(kPropDust), 5, 121);
      }
      if (swing < 0.2f) {
        const Rect sparks[] = {{155, 118, 5, 5, kBlueLight}, {146, 124, 4, 4, kBlueLight}};
        AppendProp(out, sparks, Get(kPropDust), 152, 121);
      }
    } else {
      const struct {
        float x, y, delay;
      } bits[] = {{28, 6, 0.0f}, {78, 0, 0.3f}, {126, 8, 0.6f}};
      for (const auto& bit : bits) {
        if (InPhase(time + 1.6 - bit.delay, 1.6, 0.4, 0.6)) {
          const Rect r{bit.x, bit.y, 5, 5, kBlueLight};
          AppendProp(out, std::span<const Rect>(&r, 1), Get(kPropDust), bit.x + 2.5f, bit.y + 2.5f);
        }
      }
    }
  }
  if (Get(kPropSweat) > 0.03f) {
    // A bead of sweat sliding down the side of the head.
    const float drip = static_cast<float>(std::fmod(time * 9.0f, 14.0f));
    const Rect bead[] = {
        {113, 44 + drip, 4, 4, kBlueLight},
        {114, 41 + drip, 2, 3, kBlueLight},
    };
    std::vector<Rect> attached;
    for (const Rect& r : bead) {
      attached.push_back(body.Apply(r));
    }
    AppendProp(out, attached, Get(kPropSweat) * (1.0f - drip / 18.0f), 115, 48 + drip);
  }
  if (Get(kPropBadge) > 0.03f) {
    const float pulse = 1.0f + 0.12f * PingPong(time, 1.0);
    const Rect badge[] = {
        {124, 18, 16, 16, kRed},
        {130, 21, 4, 7, kWhite},
        {130, 30, 4, 3, kWhite},
    };
    AppendProp(out, badge, Get(kPropBadge) * pulse, 132, 26);
  }

  if (facing_left_) {
    // Mirrored about the vertical centre, so a pose is authored once.
    for (Rect& r : out) {
      r.x = kFrameWidth - (r.x + r.w);
    }
  }
  return out;
}

gfx::PointF MascotRig::PointerTip() const {
  // The fist's far edge when it is out, as the clicking pose puts it.
  const float x = 165.0f;
  return gfx::PointF(facing_left_ ? kFrameWidth - x : x, 79.5f);
}

}  // namespace zephyrus::agent
