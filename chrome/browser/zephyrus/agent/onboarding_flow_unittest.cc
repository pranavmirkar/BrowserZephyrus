// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/onboarding_flow.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "chrome/browser/zephyrus/agent/model_settings.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

TEST(OnboardingFlowTest, StartsOnTheWelcomeScreenAndCannotGoBack) {
  OnboardingFlow flow;
  EXPECT_EQ(flow.step(), OnboardingStep::kWelcome);
  EXPECT_FALSE(flow.CanGoBack());
  EXPECT_FALSE(flow.Back());
  EXPECT_EQ(flow.index(), 0);
  EXPECT_FALSE(flow.IsLastStep());
}

TEST(OnboardingFlowTest, WalksThroughEveryScreenInOrderAndThenFinishes) {
  OnboardingFlow flow;
  const std::array<OnboardingStep, kOnboardingStepCount> order = {{
      OnboardingStep::kWelcome, OnboardingStep::kLook,
      OnboardingStep::kWorkspaces, OnboardingStep::kAgentAndVoice,
      OnboardingStep::kPrivacyAndImport}};
  for (int i = 0; i < kOnboardingStepCount; ++i) {
    EXPECT_EQ(flow.step(), order[i]);
    EXPECT_FALSE(flow.finished());
    if (i + 1 < kOnboardingStepCount) {
      EXPECT_TRUE(flow.Next());
    }
  }
  EXPECT_TRUE(flow.IsLastStep());
  // Next on the last screen is "Finish": the screen does not change, the flow
  // ends.
  EXPECT_FALSE(flow.Next());
  EXPECT_TRUE(flow.finished());
  EXPECT_FALSE(flow.skipped());
}

TEST(OnboardingFlowTest, BackReturnsToThePreviousScreen) {
  OnboardingFlow flow;
  ASSERT_TRUE(flow.Next());
  ASSERT_TRUE(flow.Next());
  EXPECT_TRUE(flow.CanGoBack());
  EXPECT_TRUE(flow.Back());
  EXPECT_EQ(flow.step(), OnboardingStep::kLook);
}

TEST(OnboardingFlowTest, SkipEndsItFromAnyScreenAndSaysSo) {
  OnboardingFlow flow;
  ASSERT_TRUE(flow.Next());
  flow.Skip();
  EXPECT_TRUE(flow.finished());
  EXPECT_TRUE(flow.skipped());
  // Nothing moves a flow that is over.
  EXPECT_FALSE(flow.Next());
  EXPECT_FALSE(flow.Back());
  EXPECT_FALSE(flow.GoTo(0));
  EXPECT_FALSE(flow.CanGoBack());
}

TEST(OnboardingFlowTest, GoToIgnoresScreensThatDoNotExist) {
  OnboardingFlow flow;
  EXPECT_FALSE(flow.GoTo(-1));
  EXPECT_FALSE(flow.GoTo(kOnboardingStepCount));
  EXPECT_FALSE(flow.GoTo(0));  // already there
  EXPECT_TRUE(flow.GoTo(kOnboardingStepCount - 1));
  EXPECT_TRUE(flow.IsLastStep());
}

TEST(OnboardingAgentAccessTest, BuiltInOnlyWhenItIsThereAndSwitchedOn) {
  EXPECT_EQ(AgentAccessFor(true, true, false), AgentAccess::kBuiltIn);
  // Switched on but the build has none: nothing to use.
  EXPECT_EQ(AgentAccessFor(false, true, false), AgentAccess::kNoKeyYet);
  // Built-in is there but the person turned it off and has no key of their own.
  EXPECT_EQ(AgentAccessFor(true, false, false), AgentAccess::kNoKeyYet);
}

TEST(OnboardingAgentAccessTest, OwnKeyIsUsedWhenBuiltInIsNotInUse) {
  EXPECT_EQ(AgentAccessFor(true, false, true), AgentAccess::kOwnKey);
  EXPECT_EQ(AgentAccessFor(false, false, true), AgentAccess::kOwnKey);
  // Built-in in use wins even when a key was saved before: it is what Zep uses.
  EXPECT_EQ(AgentAccessFor(true, true, true), AgentAccess::kBuiltIn);
}

TEST(OnboardingSkyTest, TheSkyFollowsTheClock) {
  EXPECT_EQ(SkyTimeForClock(4, 59), SkyTime::kNight);
  EXPECT_EQ(SkyTimeForClock(5, 0), SkyTime::kDawn);
  EXPECT_EQ(SkyTimeForClock(7, 59), SkyTime::kDawn);
  EXPECT_EQ(SkyTimeForClock(8, 0), SkyTime::kDay);
  EXPECT_EQ(SkyTimeForClock(16, 59), SkyTime::kDay);
  EXPECT_EQ(SkyTimeForClock(17, 0), SkyTime::kSunset);
  EXPECT_EQ(SkyTimeForClock(19, 29), SkyTime::kSunset);
  EXPECT_EQ(SkyTimeForClock(19, 30), SkyTime::kNight);
  EXPECT_EQ(SkyTimeForClock(0, 0), SkyTime::kNight);
  EXPECT_EQ(SkyTimeForClock(23, 59), SkyTime::kNight);
}

TEST(OnboardingSkyTest, TheDevSwitchNamesASkyAndNonsenseFallsBack) {
  EXPECT_EQ(SkyTimeFromName("sunset", SkyTime::kDay), SkyTime::kSunset);
  EXPECT_EQ(SkyTimeFromName("night", SkyTime::kDay), SkyTime::kNight);
  EXPECT_EQ(SkyTimeFromName("NIGHT", SkyTime::kDay), SkyTime::kDay);
  EXPECT_EQ(SkyTimeFromName("", SkyTime::kDawn), SkyTime::kDawn);
}

TEST(OnboardingFlightTest, ZepStaysOnScreenAndAwayFromTheMiddle) {
  // Over several laps, at 30 frames a second.
  double nearest_centre = 1.0;
  for (int frame = 0; frame < 30 * 200; ++frame) {
    const FlightPoint p = FlightPathAt(frame / 30.0);
    EXPECT_GT(p.x, 0.02) << frame;
    EXPECT_LT(p.x, 0.98) << frame;
    EXPECT_GT(p.y, 0.05) << frame;
    EXPECT_LT(p.y, 0.95) << frame;
    nearest_centre = std::min(
        nearest_centre, std::hypot((p.x - 0.5) / 0.4, (p.y - 0.48) / 0.36));
  }
  // It goes round the card, never through the middle of the window.
  EXPECT_GT(nearest_centre, 0.8);
}

TEST(OnboardingFlightTest, TheLoopGoesRoundTheBoxItIsGiven) {
  // A card centred at (0.5, 0.55) and the loop fitted just outside it.
  for (int frame = 0; frame < 30 * 60; ++frame) {
    const FlightPoint p = FlightPathAt(frame / 30.0, 0.5, 0.55, 0.42, 0.40);
    const double r = std::hypot((p.x - 0.5) / 0.42, (p.y - 0.55) / 0.40);
    EXPECT_GT(r, 0.85) << frame;
    EXPECT_LT(r, 1.15) << frame;
  }
}

TEST(OnboardingFlightTest, ThePathIsSmoothFrameToFrame) {
  FlightPoint last = FlightPathAt(0);
  for (int frame = 1; frame < 30 * 120; ++frame) {
    const FlightPoint p = FlightPathAt(frame / 30.0);
    // At most about a hundredth of the window per frame: no jumps.
    EXPECT_LT(std::hypot(p.x - last.x, p.y - last.y), 0.01) << frame;
    last = p;
  }
}

class OnboardingPrefTest : public testing::Test {
 protected:
  OnboardingPrefTest() { RegisterProfilePrefs(prefs_.registry()); }
  TestingPrefServiceSimple prefs_;
};

TEST_F(OnboardingPrefTest, ShowsUntilItIsDoneThenNeverAgain) {
  EXPECT_TRUE(ShouldShowOnboarding(prefs_, /*forced=*/false, /*enabled=*/true));
  MarkOnboardingDone(prefs_);
  EXPECT_FALSE(
      ShouldShowOnboarding(prefs_, /*forced=*/false, /*enabled=*/true));
}

TEST_F(OnboardingPrefTest, InstalledBrowserShowsOnboardingWithoutDevSwitches) {
  EXPECT_TRUE(ShouldShowOnboarding(prefs_, /*forced=*/false,
                                 kOnboardingEnabled));
  MarkOnboardingDone(prefs_);
  EXPECT_FALSE(ShouldShowOnboarding(prefs_, /*forced=*/false,
                                  kOnboardingEnabled));
}

TEST_F(OnboardingPrefTest, TheDevSwitchShowsItAgainWhateverTheProfileSays) {
  MarkOnboardingDone(prefs_);
  EXPECT_TRUE(ShouldShowOnboarding(prefs_, /*forced=*/true, /*enabled=*/true));
  EXPECT_TRUE(ShouldShowOnboarding(prefs_, /*forced=*/true, /*enabled=*/false));
}

TEST_F(OnboardingPrefTest, RunSetupAgainBringsItBack) {
  MarkOnboardingDone(prefs_);
  ResetOnboarding(prefs_);
  EXPECT_TRUE(ShouldShowOnboarding(prefs_, /*forced=*/false,
                                 kOnboardingEnabled));
}

TEST_F(OnboardingPrefTest, SwitchedOffItNeverShowsByItself) {
  // Retain coverage for explicitly disabling the automatic flow.
  EXPECT_FALSE(
      ShouldShowOnboarding(prefs_, /*forced=*/false, /*enabled=*/false));
}

}  // namespace
}  // namespace zephyrus::agent
