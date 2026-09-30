// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_MASCOT_RIG_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_MASCOT_RIG_H_

#include <array>
#include <stdint.h>
#include <vector>

#include "base/time/time.h"
#include "chrome/installer/zephyrus_setup/mascot.h"
#include "ui/gfx/geometry/point_f.h"

namespace zephyrus::agent {

// What the character is doing.
enum class MascotMood {
  kIdle,
  kSleeping,
  kYawning,    // on its way to sleep; settles into kSleeping by itself
  kWaving,     // a greeting; settles into kIdle by itself
  kListening,
  kThinking,
  kReading,
  kSearching,
  kTyping,
  kClicking,   // the arm out, pointing at what it is about to press
  kScrolling,
  kAsking,
  kHappy,
  kConfused,
  kAlert,
  kWorking,
};

// A one-off jolt on top of whatever the mood is doing.
enum class MascotKick {
  kPress,    // a click: the fist dips, the body flinches
  kLand,     // arriving somewhere: squash, then settle
  kStartle,  // something unexpected: a small jump
};

// The mascot as a skeleton rather than a set of poses.
//
// The installer draws each state as its own list of rectangles, which is right
// for an installer and wrong for a character that is meant to seem alive: going
// from one list to another is a cut. Here the character is a handful of
// continuous quantities -- how high the body sits, how far it leans, where each
// hand is, how open the eyes are, how big each prop is -- and a mood only sets
// where each of them WANTS to be. Every quantity chases its target as a spring,
// so a change of mood is never a cut: the old pose glides into the new one, the
// body overshoots and settles, and a prop pops in and out with a little bounce.
// That glide is most of what reads as alive.
//
// The parts themselves come from the installer's art (zephyrus_setup::*Art), so
// this is the same character, not a copy of it.
//
// No views, no clock, no drawing: `Update` takes a time step and `Draw` returns
// rectangles. That is what lets its motion be tested.
class MascotRig {
 public:
  MascotRig();

  void SetMood(MascotMood mood);
  MascotMood mood() const { return mood_; }

  // Seconds in the current mood.
  double MoodTime() const { return mood_time_; }

  // Where the character is looking, each in [-1, 1] relative to straight ahead.
  // Overrides the idle glances, which resume when it is cleared.
  void LookAt(float x, float y);
  void StopLooking();

  // Which way the character faces. Mirrored at draw time, so a pose is
  // authored once.
  void SetFacingLeft(bool left) { facing_left_ = left; }
  bool facing_left() const { return facing_left_; }

  // How fast the character is travelling, in art units a second, signed by
  // direction. Drives the walk cycle; zero stands still and the legs settle.
  void SetTravel(float velocity);

  // Which way a scroll is going, +1 down and -1 up, for the arrows and the arm.
  void SetScrollDirection(int direction) { scroll_direction_ = direction; }

  void Kick(MascotKick kick);

  void Update(base::TimeDelta dt);

  // Rectangles in the art's own coordinate space, painter's order. The frame
  // they fit in is zephyrus_setup::kFrame*.
  std::vector<zephyrus_setup::Rect> Draw() const;

  // Where the tip of the pointing fist is, in the same space, for a character
  // facing right; a caller aiming it at something places the body so this lands
  // there. Mirrored automatically when facing left.
  gfx::PointF PointerTip() const;

 private:
  enum Param {
    kBodyDx,
    kBodyDy,
    kSquash,
    kLean,
    kLeg0,   // four legs' vertical lift
    kLeg1,
    kLeg2,
    kLeg3,
    kLegSwing,  // fore-aft swing of the walking pair
    kPupilX,
    kPupilY,
    kPupilSize,
    kPupilSplit,  // pupils drifting apart, for confusion
    kLid,
    kBrow,        // -1 sad .. 0 .. +1 furrowed
    kBrowRaise,   // 0 .. 1, both brows lifted: surprise, a question
    kBlush,       // 0 .. 1, pleased
    kMouth,
    kSmile,
    kHappyEyes,
    kHandLOn,
    kHandLX,
    kHandLY,
    kHandROn,
    kHandRX,
    kHandRY,
    kPropKeyboard,
    kPropGlass,
    kPropQuestion,
    kPropZ,
    kPropSparkle,
    kPropBadge,
    kPropDots,
    kPropWaves,
    kPropPage,
    kPropArrow,
    kPropDust,
    kPropSweat,
    kGlassX,
    kParamCount,
  };

  struct Spring {
    float x = 0;
    float v = 0;
    float target = 0;
    float frequency = 5;  // Hz
    float damping = 1;    // 1 is critical, below 1 overshoots
  };

  void Set(Param p, float target) { springs_[p].target = target; }
  void Snap(Param p, float value);
  float Get(Param p) const { return springs_[p].x; }
  void ApplyMood();
  void ApplyGait();
  float Rand();  // [0, 1), deterministic

  std::array<Spring, kParamCount> springs_;

  MascotMood mood_ = MascotMood::kIdle;
  double mood_time_ = 0;
  double time_ = 0;

  bool facing_left_ = false;
  bool looking_ = false;
  float look_x_ = 0;
  float look_y_ = 0;
  float travel_ = 0;
  double gait_ = 0;
  int scroll_direction_ = 1;

  // Small things it does when left alone: a stretch, a hop, a look round.
  // Only while idle, on a slow schedule, so waiting reads as being alive and
  // not as fidgeting.
  int micro_ = 0;
  double micro_time_ = 0;
  double next_micro_ = 9.0;

  // Idle life: blinks and glances, on their own schedule.
  double next_blink_ = 2.0;
  double blink_left_ = 0;
  double next_glance_ = 3.0;
  float glance_x_ = 0;
  float glance_y_ = 0;

  uint32_t rng_ = 0x9E3779B9u;
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_MASCOT_RIG_H_
