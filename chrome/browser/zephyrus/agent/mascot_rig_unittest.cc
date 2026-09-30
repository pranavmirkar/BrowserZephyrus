// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/mascot_rig.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

using zephyrus_setup::Rect;

constexpr base::TimeDelta kFrame = base::Seconds(1.0 / 60);

constexpr MascotMood kAllMoods[] = {
    MascotMood::kIdle,     MascotMood::kSleeping,  MascotMood::kYawning,
    MascotMood::kWaving,   MascotMood::kListening, MascotMood::kThinking,
    MascotMood::kReading,  MascotMood::kSearching, MascotMood::kTyping,
    MascotMood::kClicking, MascotMood::kScrolling, MascotMood::kAsking,
    MascotMood::kHappy,    MascotMood::kConfused,  MascotMood::kAlert,
    MascotMood::kWorking,
};

void Advance(MascotRig& rig, double seconds) {
  for (double t = 0; t < seconds; t += kFrame.InSecondsF()) {
    rig.Update(kFrame);
  }
}

// The widest rectangle is a torso row, whatever else is on screen, so it is a
// stand-in for where the body is.
Rect Torso(const MascotRig& rig) {
  const std::vector<Rect> rects = rig.Draw();
  return *std::max_element(
      rects.begin(), rects.end(),
      [](const Rect& a, const Rect& b) { return a.w < b.w; });
}

TEST(MascotRigTest, EveryMoodDrawsSomethingSaneAtEveryMoment) {
  for (MascotMood mood : kAllMoods) {
    MascotRig rig;
    rig.SetMood(mood);
    for (int frame = 0; frame < 600; ++frame) {
      rig.Update(kFrame);
      const std::vector<Rect> rects = rig.Draw();
      ASSERT_GT(rects.size(), 10u) << static_cast<int>(mood);
      for (const Rect& r : rects) {
        ASSERT_TRUE(std::isfinite(r.x) && std::isfinite(r.y) &&
                    std::isfinite(r.w) && std::isfinite(r.h))
            << static_cast<int>(mood) << " frame " << frame;
        ASSERT_GE(r.w, 0) << static_cast<int>(mood);
        ASSERT_GE(r.h, 0) << static_cast<int>(mood);
        // Nothing flies off the frame it is meant to fit in.
        ASSERT_GT(r.x, -80) << static_cast<int>(mood);
        ASSERT_LT(r.x, 260) << static_cast<int>(mood);
        ASSERT_GT(r.y, -60) << static_cast<int>(mood);
        ASSERT_LT(r.y, 230) << static_cast<int>(mood);
      }
    }
  }
}

TEST(MascotRigTest, ChangingMoodNeverCutsTheBodyToTheNewPose) {
  // The whole point of a rig. Going from one mood to another must move the body
  // continuously -- a cut is a jump of the body between two frames, and the
  // biggest honest motion (the happy hop) is a few units a frame.
  for (MascotMood from : kAllMoods) {
    for (MascotMood to : kAllMoods) {
      MascotRig rig;
      rig.SetMood(from);
      Advance(rig, 2.0);
      Rect before = Torso(rig);
      rig.SetMood(to);
      for (int frame = 0; frame < 120; ++frame) {
        rig.Update(kFrame);
        const Rect now = Torso(rig);
        ASSERT_LT(std::abs(now.y - before.y), 6.0f)
            << static_cast<int>(from) << " -> " << static_cast<int>(to)
            << " frame " << frame;
        ASSERT_LT(std::abs(now.x - before.x), 6.0f)
            << static_cast<int>(from) << " -> " << static_cast<int>(to);
        before = now;
      }
    }
  }
}

TEST(MascotRigTest, APoseOvershootsAndSettlesInsteadOfArrivingDead) {
  // Squash-and-stretch: a landing squashes past rest and comes back.
  MascotRig rig;
  Advance(rig, 1.0);
  const float rest = Torso(rig).h;
  rig.Kick(MascotKick::kLand);
  float smallest = rest;
  float largest = rest;
  for (int frame = 0; frame < 60; ++frame) {
    rig.Update(kFrame);
    smallest = std::min(smallest, Torso(rig).h);
    largest = std::max(largest, Torso(rig).h);
  }
  EXPECT_LT(smallest, rest - 0.5f) << "no squash";
  EXPECT_GT(largest, smallest + 1.0f);
  Advance(rig, 2.0);
  EXPECT_NEAR(Torso(rig).h, rest, 1.0f) << "did not settle";
}

TEST(MascotRigTest, ItBlinksOnItsOwn) {
  MascotRig rig;
  float shortest = 100;
  for (int frame = 0; frame < 60 * 12; ++frame) {
    rig.Update(kFrame);
    for (const Rect& r : rig.Draw()) {
      if (r.w > 17.0f && r.w < 19.0f && r.h < 19.0f && r.y > 60 && r.y < 90) {
        shortest = std::min(shortest, r.h);
      }
    }
  }
  EXPECT_LE(shortest, 6.0f) << "no blink in twelve seconds";
}

TEST(MascotRigTest, YawningFallsAsleepAndWavingReturnsToIdle) {
  MascotRig yawn;
  yawn.SetMood(MascotMood::kYawning);
  Advance(yawn, 3.0);
  EXPECT_EQ(yawn.mood(), MascotMood::kSleeping);

  MascotRig wave;
  wave.SetMood(MascotMood::kWaving);
  Advance(wave, 2.5);
  EXPECT_EQ(wave.mood(), MascotMood::kIdle);
}

TEST(MascotRigTest, TravellingWalksAndStandingStillDoesNot) {
  const auto highest_foot = [](MascotRig& rig) {
    float highest_bottom = 200;
    for (int frame = 0; frame < 60; ++frame) {
      rig.Update(kFrame);
      for (const Rect& r : rig.Draw()) {
        // The legs are the four narrow rectangles at the bottom.
        if (r.w > 10 && r.w < 16 && r.h > 15 && r.y > 100) {
          highest_bottom = std::min(highest_bottom, r.y + r.h);
        }
      }
    }
    return highest_bottom;
  };
  MascotRig walking;
  walking.SetTravel(300);
  Advance(walking, 0.5);
  EXPECT_LT(highest_foot(walking), 138.0f) << "a leg lifts when it walks";

  MascotRig standing;
  Advance(standing, 0.5);
  EXPECT_GT(highest_foot(standing), 139.0f);
}

TEST(MascotRigTest, FacingLeftIsTheMirrorImage) {
  MascotRig right;
  MascotRig left;
  left.SetFacingLeft(true);
  Advance(right, 0.7);
  Advance(left, 0.7);
  const Rect a = Torso(right);
  const Rect b = Torso(left);
  EXPECT_NEAR(a.x + a.w / 2 + b.x + b.w / 2, 160.0f, 0.5f);
  EXPECT_NEAR(right.PointerTip().x() + left.PointerTip().x(), 160.0f, 0.001f);
  EXPECT_FLOAT_EQ(right.PointerTip().y(), left.PointerTip().y());
}

TEST(MascotRigTest, TheClickingArmReachesTheTip) {
  MascotRig rig;
  rig.SetMood(MascotMood::kClicking);
  Advance(rig, 2.0);
  float rightmost = 0;
  for (const Rect& r : rig.Draw()) {
    rightmost = std::max(rightmost, r.x + r.w);
  }
  EXPECT_NEAR(rightmost, rig.PointerTip().x(), 3.0f)
      << "the pointing fist and the point the body is placed by must agree";
}

TEST(MascotRigTest, LookingFollowsAndLetsGo) {
  MascotRig rig;
  rig.LookAt(1, 0);
  Advance(rig, 1.0);
  const auto pupil_x = [](const MascotRig& r) {
    for (const Rect& rect : r.Draw()) {
      if (rect.w > 7 && rect.w < 9 && rect.h > 3 && rect.y > 60 &&
          rect.y < 85 && rect.x < 70) {
        return rect.x;
      }
    }
    return -1000.0f;
  };
  const float looking_right = pupil_x(rig);
  rig.LookAt(-1, 0);
  Advance(rig, 1.0);
  EXPECT_LT(pupil_x(rig), looking_right - 4.0f);
}

TEST(MascotRigTest, ItDoesSmallThingsWhenLeftAloneWithoutEverJumping) {
  MascotRig rig;
  const size_t at_rest = rig.Draw().size();
  size_t most = at_rest;
  Rect before = Torso(rig);
  for (int frame = 0; frame < 60 * 70; ++frame) {
    rig.Update(kFrame);
    const Rect now = Torso(rig);
    ASSERT_LT(std::abs(now.y - before.y), 5.5f) << "frame " << frame;
    ASSERT_LT(std::abs(now.x - before.x), 5.5f) << "frame " << frame;
    before = now;
    most = std::max(most, rig.Draw().size());
  }
  // A stretch raises its arms, which nothing else in the idle does.
  EXPECT_GT(most, at_rest) << "nothing happened in seventy idle seconds";
}

TEST(MascotRigTest, ExpressionsAreFacesNotJustPoses) {
  const auto brows = [](MascotMood mood) {
    MascotRig rig;
    rig.SetMood(mood);
    Advance(rig, 1.5);
    int count = 0;
    for (const Rect& r : rig.Draw()) {
      // Brows are the only 6-wide navy blocks.
      if (r.color == zephyrus_setup::kNavy && r.w > 5.0f && r.w < 7.0f) {
        ++count;
      }
    }
    return count;
  };
  EXPECT_EQ(brows(MascotMood::kAlert), 4) << "furrowed";
  EXPECT_EQ(brows(MascotMood::kConfused), 4) << "sad";
  EXPECT_EQ(brows(MascotMood::kAsking), 4) << "raised";
  EXPECT_EQ(brows(MascotMood::kSleeping), 0);
}

TEST(MascotRigTest, AHappyFaceBlushes) {
  MascotRig rig;
  rig.SetMood(MascotMood::kHappy);
  Advance(rig, 1.5);
  int light_cheeks = 0;
  for (const Rect& r : rig.Draw()) {
    // Cheeks are the only flat light-blue slivers: sparkles are 6 high.
    if (r.color == zephyrus_setup::kBlueLight && r.h < 3.5f && r.w > 4 &&
        r.w < 8) {
      ++light_cheeks;
    }
  }
  EXPECT_EQ(light_cheeks, 2);
}

// A developer tool, not a test: writes every mood, sampled over time, as JSON
// for drawing, so a change to the character can be LOOKED at.
//   ZEPHYRUS_RIG_DUMP=D:\path\rig.json zephyrus_agent_unittests.exe
//       --gtest_also_run_disabled_tests --gtest_filter=*DumpEveryMood
TEST(MascotRigTest, DISABLED_DumpEveryMood) {
  const char* path = std::getenv("ZEPHYRUS_RIG_DUMP");
  ASSERT_TRUE(path);
  std::string json = "{";
  bool first_mood = true;
  for (MascotMood mood : kAllMoods) {
    MascotRig rig;
    rig.SetMood(mood);
    if (mood == MascotMood::kScrolling) {
      rig.SetScrollDirection(1);
    }
    json += std::string(first_mood ? "" : ",") + "\"" +
            std::to_string(static_cast<int>(mood)) + "\":[";
    first_mood = false;
    bool first_frame = true;
    for (double sample : {0.3, 0.9, 1.5, 2.1}) {
      rig.SetMood(mood);
      MascotRig fresh;
      fresh.SetMood(mood);
      Advance(fresh, sample);
      json += std::string(first_frame ? "" : ",") + "[";
      first_frame = false;
      bool first = true;
      for (const Rect& r : fresh.Draw()) {
        json += std::string(first ? "" : ",") + "[" + std::to_string(r.x) + "," +
                std::to_string(r.y) + "," + std::to_string(r.w) + "," +
                std::to_string(r.h) + "," + std::to_string(r.color) + "]";
        first = false;
      }
      json += "]";
    }
    json += "]";
  }
  json += "}";
  ASSERT_TRUE(base::WriteFile(base::FilePath::FromUTF8Unsafe(path), json));
}

TEST(MascotRigTest, ASleepingCharacterKeepsItsEyesShut) {
  // The bug this guards: a spring that diverged, so the lid flew open and shut
  // at random and a sleeping mascot stared.
  MascotRig rig;
  rig.SetMood(MascotMood::kSleeping);
  Advance(rig, 1.0);
  for (int frame = 0; frame < 60 * 5; ++frame) {
    rig.Update(kFrame);
    float tallest_eye = 0;
    for (const Rect& r : rig.Draw()) {
      if (r.w > 17.0f && r.w < 19.0f && r.y > 60 && r.y < 90) {
        tallest_eye = std::max(tallest_eye, r.h);
      }
    }
    ASSERT_LT(tallest_eye, 6.0f) << "frame " << frame;
  }
}

TEST(MascotRigTest, HeldHangsFromTheCursorAndReliefSettlesToIdle) {
  MascotRig rig;
  const gfx::PointF resting = rig.PointerTip();
  rig.SetMood(MascotMood::kHeld);
  Advance(rig, 1.0);
  // The body hangs from a point above its head, not out at its side.
  const gfx::PointF held = rig.PointerTip();
  EXPECT_LT(held.y(), 10.0f);
  EXPECT_NE(held.y(), resting.y());
  // Swung sideways it leans; the fists are up (drawn above the head).
  rig.SetSway(1.0f);
  Advance(rig, 1.0);
  float top = 1000;
  for (const Rect& r : rig.Draw()) {
    top = std::min(top, r.y);
  }
  EXPECT_LT(top, 8.0f);

  // Put down: relieved, then back to idle by itself.
  rig.SetSway(0);
  rig.SetMood(MascotMood::kRelieved);
  Advance(rig, 1.0);
  EXPECT_EQ(rig.mood(), MascotMood::kRelieved);
  Advance(rig, 3.0);
  EXPECT_EQ(rig.mood(), MascotMood::kIdle);
}

}  // namespace
}  // namespace zephyrus::agent
