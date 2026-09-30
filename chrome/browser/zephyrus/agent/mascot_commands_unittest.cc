// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/mascot_commands.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

TEST(MascotCommands, ReadsRequestsToMove) {
  EXPECT_EQ(ParseMascotMove("move to the left"), MascotSpot::kLeft);
  EXPECT_EQ(ParseMascotMove("Zep, please move to the right."), MascotSpot::kRight);
  EXPECT_EQ(ParseMascotMove("can you go to the top right corner"),
            MascotSpot::kTopRight);
  EXPECT_EQ(ParseMascotMove("scoot to the bottom left"), MascotSpot::kBottomLeft);
  EXPECT_EQ(ParseMascotMove("move to the middle"), MascotSpot::kCenter);
  EXPECT_EQ(ParseMascotMove("get out of the way"), MascotSpot::kAway);
  EXPECT_EQ(ParseMascotMove("Zep you're blocking the page"), MascotSpot::kAway);
  EXPECT_EQ(ParseMascotMove("you are blocking"), MascotSpot::kAway);
  EXPECT_EQ(ParseMascotMove("go away"), MascotSpot::kAway);
  EXPECT_EQ(ParseMascotMove("step aside"), MascotSpot::kAway);
  EXPECT_EQ(ParseMascotMove("come back"), MascotSpot::kHome);
  EXPECT_EQ(ParseMascotMove("go home"), MascotSpot::kHome);
}

TEST(MascotCommands, LeavesTasksAlone) {
  // Each of these is for the agent, and must not be taken for a request to the
  // mascot: a false positive would swallow a real task.
  EXPECT_EQ(ParseMascotMove("move the slider to the left"), std::nullopt);
  EXPECT_EQ(ParseMascotMove("go to the top of the page and click login"),
            std::nullopt);
  EXPECT_EQ(ParseMascotMove("go back"), std::nullopt);  // the browser's Back
  EXPECT_EQ(ParseMascotMove("scroll to the bottom"), std::nullopt);
  EXPECT_EQ(ParseMascotMove("go to youtube"), std::nullopt);
  EXPECT_EQ(ParseMascotMove("open notion and write an essay"), std::nullopt);
  EXPECT_EQ(ParseMascotMove("left"), std::nullopt);  // no request in it
  EXPECT_EQ(ParseMascotMove(""), std::nullopt);
}

}  // namespace
}  // namespace zephyrus::agent
