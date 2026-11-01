// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/lens_mood_map.h"

#include <set>

#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

constexpr MascotMood kAllMoods[] = {
    MascotMood::kIdle,      MascotMood::kSleeping, MascotMood::kYawning,
    MascotMood::kWaving,    MascotMood::kListening, MascotMood::kThinking,
    MascotMood::kReading,   MascotMood::kSearching, MascotMood::kTyping,
    MascotMood::kClicking,  MascotMood::kScrolling, MascotMood::kAsking,
    MascotMood::kHappy,     MascotMood::kConfused,  MascotMood::kAlert,
    MascotMood::kWorking,   MascotMood::kHeld,      MascotMood::kRelieved,
};

TEST(LensMoodMapTest, TheApprovedTable) {
  // docs/mascot-lens-plan.md, "Mapping Zep's moods to the lens states".
  EXPECT_EQ(LensStateForMood(MascotMood::kIdle, 5), LensState::kIdle);
  EXPECT_EQ(LensStateForMood(MascotMood::kSleeping, 5), LensState::kSleeping);
  EXPECT_EQ(LensStateForMood(MascotMood::kYawning, 5), LensState::kSleepy);
  EXPECT_EQ(LensStateForMood(MascotMood::kWaving, 5), LensState::kGreeting);
  EXPECT_EQ(LensStateForMood(MascotMood::kListening, 5), LensState::kListening);
  EXPECT_EQ(LensStateForMood(MascotMood::kThinking, 5), LensState::kThinking);
  EXPECT_EQ(LensStateForMood(MascotMood::kReading, 5), LensState::kReading);
  EXPECT_EQ(LensStateForMood(MascotMood::kSearching, 5), LensState::kSearching);
  EXPECT_EQ(LensStateForMood(MascotMood::kTyping, 5), LensState::kTyping);
  EXPECT_EQ(LensStateForMood(MascotMood::kClicking, 5), LensState::kClicking);
  EXPECT_EQ(LensStateForMood(MascotMood::kScrolling, 5), LensState::kScrolling);
  EXPECT_EQ(LensStateForMood(MascotMood::kAsking, 5), LensState::kAsking);
  EXPECT_EQ(LensStateForMood(MascotMood::kConfused, 5), LensState::kConfused);
  EXPECT_EQ(LensStateForMood(MascotMood::kAlert, 5), LensState::kSurprise);
  EXPECT_EQ(LensStateForMood(MascotMood::kWorking, 5), LensState::kWorking);
  EXPECT_EQ(LensStateForMood(MascotMood::kHeld, 5), LensState::kHeld);
  EXPECT_EQ(LensStateForMood(MascotMood::kRelieved, 5), LensState::kRelieved);
}

TEST(LensMoodMapTest, AFinishedTaskIsDoneAndThenSettlesIntoJoy) {
  EXPECT_EQ(LensStateForMood(MascotMood::kHappy, 0), LensState::kDone);
  EXPECT_EQ(LensStateForMood(MascotMood::kHappy, kLensDoneSeconds - 0.01),
            LensState::kDone);
  EXPECT_EQ(LensStateForMood(MascotMood::kHappy, kLensDoneSeconds),
            LensState::kJoy);
  EXPECT_EQ(LensStateForMood(MascotMood::kHappy, 60), LensState::kJoy);
}

TEST(LensMoodMapTest, EveryMoodHasALensStateAndNoTwoMoodsShareOne) {
  std::set<LensState> seen;
  for (MascotMood mood : kAllMoods) {
    const LensState s = LensStateForMood(mood, 10);
    EXPECT_LT(static_cast<int>(s), kLensStateCount);
    EXPECT_TRUE(seen.insert(s).second)
        << "two moods look the same on the lens: " << LensStateKey(s);
  }
  EXPECT_EQ(seen.size(), 18u);
}

TEST(LensMoodMapTest, MoodsThatRunTheSameStateMachineStillSettleLikeZep) {
  // Waving and yawning settle by themselves in the rig; the lens maps each moment
  // of that, so a wave that has become idle shows idle and a yawn that has
  // become sleep shows sleeping.
  EXPECT_NE(LensStateForMood(MascotMood::kWaving, 1),
            LensStateForMood(MascotMood::kIdle, 1));
  EXPECT_NE(LensStateForMood(MascotMood::kYawning, 1),
            LensStateForMood(MascotMood::kSleeping, 1));
}

}  // namespace
}  // namespace zephyrus::agent
