// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/glance/glance_policy.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::glance {
namespace {

Modifiers Alt() {
  Modifiers m;
  m.alt = true;
  return m;
}

GURL Link() {
  return GURL("https://example.com/page");
}

// ---- The trigger setting ------------------------------------------------

TEST(GlanceTriggerTest, NamesRoundTripAndAnythingElseIsAlt) {
  for (Trigger t : {Trigger::kAlt, Trigger::kCtrl, Trigger::kShift}) {
    EXPECT_EQ(TriggerFromName(NameOf(t)), t);
  }
  EXPECT_EQ(TriggerFromName(""), Trigger::kAlt);
  EXPECT_EQ(TriggerFromName("ALT"), Trigger::kAlt);
  EXPECT_EQ(TriggerFromName("meta"), Trigger::kAlt);
  EXPECT_EQ(TriggerFromName("ctrl\n"), Trigger::kAlt);
}

// ---- Which addresses ----------------------------------------------------

TEST(GlanceUrlTest, OnlyWebAddressesAreAllowed) {
  EXPECT_TRUE(IsGlanceableUrl(GURL("https://example.com/")));
  EXPECT_TRUE(IsGlanceableUrl(GURL("http://example.com/a?b=c#d")));
  EXPECT_TRUE(IsGlanceableUrl(GURL("https://user:pw@example.com/")));

  EXPECT_FALSE(IsGlanceableUrl(GURL()));
  EXPECT_FALSE(IsGlanceableUrl(GURL("not a url")));
  EXPECT_FALSE(IsGlanceableUrl(GURL("javascript:alert(1)")));
  EXPECT_FALSE(IsGlanceableUrl(GURL("data:text/html,<b>x</b>")));
  EXPECT_FALSE(IsGlanceableUrl(GURL("file:///C:/secret.txt")));
  EXPECT_FALSE(IsGlanceableUrl(GURL("chrome://settings/")));
  EXPECT_FALSE(IsGlanceableUrl(GURL("about:blank")));
  EXPECT_FALSE(IsGlanceableUrl(GURL("blob:https://example.com/abc")));
  EXPECT_FALSE(IsGlanceableUrl(GURL("ftp://example.com/file")));
  EXPECT_FALSE(IsGlanceableUrl(GURL("https://")));
}

TEST(GlanceUrlTest, AnAbsurdlyLongAddressIsRefused) {
  const GURL ok("https://example.com/" + std::string(1000, 'a'));
  const GURL huge("https://example.com/" + std::string(20000, 'a'));
  EXPECT_TRUE(IsGlanceableUrl(ok));
  EXPECT_FALSE(IsGlanceableUrl(huge));
}

// ---- Which click --------------------------------------------------------

TEST(GlanceClickTest, ExactlyTheTriggerKeyWithTheLeftButton) {
  Config config;
  EXPECT_TRUE(IsGlanceClick(config, Alt(), Button::kLeft));
  EXPECT_FALSE(IsGlanceClick(config, Modifiers(), Button::kLeft));
  EXPECT_FALSE(IsGlanceClick(config, Alt(), Button::kMiddle));
  EXPECT_FALSE(IsGlanceClick(config, Alt(), Button::kRight));
  EXPECT_FALSE(IsGlanceClick(config, Alt(), Button::kOther));
}

TEST(GlanceClickTest, AChordThatIncludesTheTriggerIsNotOurs) {
  Config config;
  // Alt+Ctrl is split view in Blink, Alt+Shift is not ours either.
  Modifiers alt_ctrl = Alt();
  alt_ctrl.ctrl = true;
  Modifiers alt_shift = Alt();
  alt_shift.shift = true;
  Modifiers alt_meta = Alt();
  alt_meta.meta = true;
  EXPECT_FALSE(IsGlanceClick(config, alt_ctrl, Button::kLeft));
  EXPECT_FALSE(IsGlanceClick(config, alt_shift, Button::kLeft));
  EXPECT_FALSE(IsGlanceClick(config, alt_meta, Button::kLeft));
}

TEST(GlanceClickTest, TheSettingPicksTheKey) {
  Modifiers ctrl;
  ctrl.ctrl = true;
  Modifiers shift;
  shift.shift = true;
  Config config;
  config.trigger = Trigger::kCtrl;
  EXPECT_TRUE(IsGlanceClick(config, ctrl, Button::kLeft));
  EXPECT_FALSE(IsGlanceClick(config, Alt(), Button::kLeft));
  config.trigger = Trigger::kShift;
  EXPECT_TRUE(IsGlanceClick(config, shift, Button::kLeft));
  EXPECT_FALSE(IsGlanceClick(config, ctrl, Button::kLeft));
}

TEST(GlanceClickTest, OffMeansOff) {
  Config config;
  config.enabled = false;
  EXPECT_FALSE(IsGlanceClick(config, Alt(), Button::kLeft));
}

// ---- The press/release gate ---------------------------------------------

TEST(ClickGateTest, PressIsSwallowedAndReleaseOpens) {
  ClickGate gate;
  Config config;
  GURL opened;
  EXPECT_EQ(gate.OnMouse(config, MouseType::kDown, Button::kLeft, Alt(), Link(),
                         &opened),
            ClickGate::Action::kSwallow);
  EXPECT_TRUE(gate.has_pending());
  EXPECT_FALSE(opened.is_valid());
  // The pointer has left the link by the time the button comes up: what was
  // under it at the PRESS is what opens.
  EXPECT_EQ(gate.OnMouse(config, MouseType::kUp, Button::kLeft, Alt(), GURL(),
                         &opened),
            ClickGate::Action::kOpen);
  EXPECT_EQ(opened, Link());
  EXPECT_FALSE(gate.has_pending());
}

TEST(ClickGateTest, AClickThatIsNotOursPassesUntouched) {
  ClickGate gate;
  Config config;
  GURL opened;
  // No key.
  EXPECT_EQ(gate.OnMouse(config, MouseType::kDown, Button::kLeft, Modifiers(),
                         Link(), &opened),
            ClickGate::Action::kPass);
  EXPECT_EQ(gate.OnMouse(config, MouseType::kUp, Button::kLeft, Modifiers(),
                         Link(), &opened),
            ClickGate::Action::kPass);
  // Not over a link.
  EXPECT_EQ(gate.OnMouse(config, MouseType::kDown, Button::kLeft, Alt(), GURL(),
                         &opened),
            ClickGate::Action::kPass);
  // Over a link we will not open.
  EXPECT_EQ(gate.OnMouse(config, MouseType::kDown, Button::kLeft, Alt(),
                         GURL("javascript:void(0)"), &opened),
            ClickGate::Action::kPass);
  // Other mouse events (moves, wheel) always pass.
  EXPECT_EQ(gate.OnMouse(config, MouseType::kOther, Button::kLeft, Alt(), Link(),
                         &opened),
            ClickGate::Action::kPass);
  EXPECT_FALSE(gate.has_pending());
  EXPECT_FALSE(opened.is_valid());
}

TEST(ClickGateTest, ReleasingTheKeyFirstCancelsButStillHidesTheReleaseFromThePage) {
  ClickGate gate;
  Config config;
  GURL opened;
  gate.OnMouse(config, MouseType::kDown, Button::kLeft, Alt(), Link(), &opened);
  // The page never saw the press, so it must not see the release.
  EXPECT_EQ(gate.OnMouse(config, MouseType::kUp, Button::kLeft, Modifiers(),
                         Link(), &opened),
            ClickGate::Action::kSwallow);
  EXPECT_FALSE(opened.is_valid());
  EXPECT_FALSE(gate.has_pending());
}

TEST(ClickGateTest, AReleaseWithNothingPendingPasses) {
  ClickGate gate;
  Config config;
  GURL opened;
  EXPECT_EQ(gate.OnMouse(config, MouseType::kUp, Button::kLeft, Alt(), Link(),
                         &opened),
            ClickGate::Action::kPass);
  EXPECT_FALSE(opened.is_valid());
}

TEST(ClickGateTest, ResetForgetsASwallowedPress) {
  ClickGate gate;
  Config config;
  GURL opened;
  gate.OnMouse(config, MouseType::kDown, Button::kLeft, Alt(), Link(), &opened);
  gate.Reset();
  EXPECT_FALSE(gate.has_pending());
  EXPECT_EQ(gate.OnMouse(config, MouseType::kUp, Button::kLeft, Alt(), Link(),
                         &opened),
            ClickGate::Action::kPass);
  EXPECT_FALSE(opened.is_valid());
}

TEST(ClickGateTest, ALaterOrdinaryPressEndsAnEarlierSwallowedOne) {
  ClickGate gate;
  Config config;
  GURL opened;
  gate.OnMouse(config, MouseType::kDown, Button::kLeft, Alt(), Link(), &opened);
  // A press with the key up: the earlier one is stale, and this one is the
  // page's.
  EXPECT_EQ(gate.OnMouse(config, MouseType::kDown, Button::kLeft, Modifiers(),
                         Link(), &opened),
            ClickGate::Action::kPass);
  EXPECT_FALSE(gate.has_pending());
  EXPECT_EQ(gate.OnMouse(config, MouseType::kUp, Button::kLeft, Alt(), Link(),
                         &opened),
            ClickGate::Action::kPass);
  EXPECT_FALSE(opened.is_valid());
}

TEST(ClickGateTest, DisabledNeverSwallowsAnything) {
  ClickGate gate;
  Config config;
  config.enabled = false;
  GURL opened;
  EXPECT_EQ(gate.OnMouse(config, MouseType::kDown, Button::kLeft, Alt(), Link(),
                         &opened),
            ClickGate::Action::kPass);
  EXPECT_FALSE(gate.has_pending());
}

TEST(ClickGateTest, TurningItOffBetweenPressAndReleaseDoesNotOpen) {
  ClickGate gate;
  Config config;
  GURL opened;
  gate.OnMouse(config, MouseType::kDown, Button::kLeft, Alt(), Link(), &opened);
  config.enabled = false;
  EXPECT_EQ(gate.OnMouse(config, MouseType::kUp, Button::kLeft, Alt(), Link(),
                         &opened),
            ClickGate::Action::kSwallow);
  EXPECT_FALSE(opened.is_valid());
}

TEST(ClickGateTest, ARightPressDoesNotDisturbAPendingLeft) {
  ClickGate gate;
  Config config;
  GURL opened;
  gate.OnMouse(config, MouseType::kDown, Button::kLeft, Alt(), Link(), &opened);
  // Another button's release is not ours to act on.
  EXPECT_EQ(gate.OnMouse(config, MouseType::kUp, Button::kRight, Alt(), Link(),
                         &opened),
            ClickGate::Action::kPass);
  EXPECT_TRUE(gate.has_pending());
}

// ---- Where the card sits ------------------------------------------------

TEST(GlanceGeometryTest, TheCardIsEightyPercentWideAndCentred) {
  const gfx::Size area(1000, 700);
  const gfx::Rect card = CardBounds(area);
  EXPECT_EQ(card.width(), 800);
  EXPECT_EQ(card.height(), 700 - 2 * kCardVerticalMargin);
  EXPECT_EQ(card.x(), 100);
  EXPECT_EQ(card.CenterPoint().x(), area.width() / 2);
  EXPECT_EQ(card.CenterPoint().y(), area.height() / 2);
}

TEST(GlanceGeometryTest, ANarrowWindowLeavesRoomForTheButtonsOnBothSides) {
  const gfx::Size area(500, 700);
  const gfx::Rect card = CardBounds(area);
  // 80% would be 400, leaving 50 a side, less than the column and its gap.
  EXPECT_GE(card.x(), kButtonColumnWidth + kButtonColumnGap - 1);
  EXPECT_EQ(card.x(), area.width() - card.right());
  EXPECT_LE(card.right(), area.width());
}

TEST(GlanceGeometryTest, TheCardNeverOverflowsOrCollapses) {
  for (int w : {1, 50, 200, 320, 360, 480, 700, 1920, 5120}) {
    for (int h : {1, 10, 24, 25, 300, 1440}) {
      const gfx::Size area(w, h);
      const gfx::Rect card = CardBounds(area);
      EXPECT_GE(card.width(), 1) << w << "x" << h;
      EXPECT_GE(card.height(), 1) << w << "x" << h;
      EXPECT_TRUE(gfx::Rect(area).Contains(card)) << w << "x" << h;
    }
  }
  EXPECT_TRUE(CardBounds(gfx::Size()).IsEmpty());
  EXPECT_TRUE(CardBounds(gfx::Size(0, 500)).IsEmpty());
}

TEST(GlanceGeometryTest, ButtonsSitBesideTheCardWhenThereIsRoom) {
  const gfx::Size area(1200, 700);
  const gfx::Rect card = CardBounds(area);
  const gfx::Rect column = ButtonColumnBounds(card, area);
  EXPECT_EQ(column.x(), card.right() + kButtonColumnGap);
  EXPECT_EQ(column.width(), kButtonColumnWidth);
  EXPECT_LE(column.right(), area.width());
  EXPECT_FALSE(column.Intersects(card));
}

TEST(GlanceGeometryTest, ButtonsMoveInsideWhenTheWindowIsTooNarrow) {
  // Force the case directly: a card that fills the width.
  const gfx::Size area(400, 600);
  const gfx::Rect card(0, 12, 400, 576);
  const gfx::Rect column = ButtonColumnBounds(card, area);
  EXPECT_TRUE(card.Contains(column));
  EXPECT_LE(column.right(), area.width());
}

TEST(GlanceGeometryTest, TheOriginPatchIsCentredOnTheClickAndStaysInside) {
  const gfx::Size area(1000, 700);
  const gfx::Rect middle = OriginRect(gfx::Point(500, 350), area);
  EXPECT_EQ(middle.CenterPoint(), gfx::Point(500, 350));

  // A click in a corner: the patch is pushed in, not cut off or left outside.
  for (const gfx::Point& p :
       {gfx::Point(0, 0), gfx::Point(999, 0), gfx::Point(0, 699),
        gfx::Point(999, 699), gfx::Point(-50, 900)}) {
    EXPECT_TRUE(gfx::Rect(area).Contains(OriginRect(p, area)))
        << p.ToString();
  }
}

// ---- Motion -------------------------------------------------------------

TEST(GlanceMotionTest, EaseGoesFromZeroToOneAndNeverBacksUp) {
  EXPECT_DOUBLE_EQ(Ease(0), 0);
  EXPECT_DOUBLE_EQ(Ease(1), 1);
  EXPECT_DOUBLE_EQ(Ease(0.5), 0.5);
  EXPECT_DOUBLE_EQ(Ease(-3), 0);
  EXPECT_DOUBLE_EQ(Ease(7), 1);
  double last = 0;
  for (int i = 0; i <= 100; ++i) {
    const double v = Ease(i / 100.0);
    EXPECT_GE(v, last);
    last = v;
  }
}

TEST(GlanceMotionTest, TheCardLeavesAndLandsExactly) {
  const gfx::Rect from(480, 340, 96, 32);
  const gfx::Rect to(100, 12, 800, 676);
  EXPECT_EQ(RectAt(0, from, to), from);
  EXPECT_EQ(RectAt(1, from, to), to);
  EXPECT_EQ(RectAt(-1, from, to), from);
  EXPECT_EQ(RectAt(2, from, to), to);
}

TEST(GlanceMotionTest, TheCentreRisesInTheMiddleOfTheTrip) {
  // Different centres, same height: any vertical offset at the midpoint is the
  // arc. (Two cards with the same centre have nowhere to travel, so no arc.)
  const gfx::Rect from(150, 350, 100, 100);
  const gfx::Rect to(100, 350, 800, 100);
  const gfx::Rect mid = RectAt(0.5, from, to);
  const double straight_cy =
      (from.CenterPoint().y() + to.CenterPoint().y()) / 2.0;
  EXPECT_LT(mid.CenterPoint().y(), straight_cy);
  EXPECT_GE(straight_cy - mid.CenterPoint().y(), 1);
  EXPECT_LE(straight_cy - mid.CenterPoint().y(), 21);
}

TEST(GlanceMotionTest, TheCardIsNeverEmptyAlongTheWay) {
  const gfx::Rect from(0, 0, 1, 1);
  const gfx::Rect to(0, 0, 1, 1);
  for (int i = 0; i <= 20; ++i) {
    EXPECT_FALSE(RectAt(i / 20.0, from, to).IsEmpty());
  }
}

TEST(GlanceMotionTest, TheButtonsWaitForTheCardThenFadeIn) {
  EXPECT_DOUBLE_EQ(ButtonsProgress(base::Milliseconds(0)), 0);
  EXPECT_DOUBLE_EQ(ButtonsProgress(kButtonsDelay), 0);
  EXPECT_GT(ButtonsProgress(kButtonsDelay + kButtonsDuration / 2), 0);
  EXPECT_LT(ButtonsProgress(kButtonsDelay + kButtonsDuration / 2), 1);
  EXPECT_DOUBLE_EQ(ButtonsProgress(kButtonsDelay + kButtonsDuration), 1);
  EXPECT_DOUBLE_EQ(ButtonsProgress(base::Seconds(10)), 1);
}

}  // namespace
}  // namespace zephyrus::glance
