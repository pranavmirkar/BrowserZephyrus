// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/toolbar/toolbar_view.h"

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/run_loop.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/toolbar/pinned_toolbar/pinned_toolbar_actions_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/home_button.h"
#include "chrome/browser/ui/views/toolbar/pinned_toolbar_actions_container.h"
#include "chrome/browser/ui/views/toolbar/reload_button.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/compositor/canvas_painter.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/views/layout/animating_layout_manager_test_util.h"
#include "ui/views/paint_info.h"
#include "ui/views/widget/widget.h"

class ZephyrusTitlebarGroupsBrowserTest : public InProcessBrowserTest {
 protected:
  ToolbarView* toolbar() {
    return BrowserView::GetBrowserViewForBrowser(browser())->toolbar();
  }
  void Layout() {
    toolbar()->InvalidateLayout();
    toolbar()->GetWidget()->LayoutRootViewIfNecessary();
  }
  auto Groups() { return toolbar()->ZephyrusTitlebarGroups(); }
  views::View* shield() { return toolbar()->zephyrus_adblock_button_; }
  views::View* close() { return toolbar()->zephyrus_close_button_; }
  views::View* new_tab() { return toolbar()->zephyrus_new_tab_button_; }
  views::View* minimize() { return toolbar()->zephyrus_minimize_button_; }
  auto* actions() { return toolbar()->pinned_toolbar_actions_container_.get(); }
  std::vector<ZephyrusGroupSegment> GroupContaining(
      const std::vector<std::vector<ZephyrusGroupSegment>>& groups,
      const views::View* member) {
    for (const auto& group : groups) {
      for (const auto& segment : group) {
        if (segment.view == member) {
          return group;
        }
      }
    }
    ADD_FAILURE() << "control is in no title-bar group";
    return {};
  }
  SkRRect Shape(views::View* button) {
    SkRRect shape;
    EXPECT_TRUE(toolbar()->GetZephyrusButtonShape(button, &shape));
    return shape;
  }
  void Paint(const char* name) {
    const gfx::Size size = toolbar()->size();
    ASSERT_FALSE(size.IsEmpty());
    SkBitmap bitmap;
    bitmap.allocN32Pixels(size.width(), size.height());
    {
      ui::CanvasPainter painter(&bitmap, size, 1.f, SK_ColorTRANSPARENT, false);
      toolbar()->Paint(views::PaintInfo::CreateRootPaintInfo(painter.context(), size));
    }
    const base::FilePath output = base::CommandLine::ForCurrentProcess()->
        GetSwitchValuePath("zephyrus-test-artifacts");
    if (!output.empty()) {
      ASSERT_TRUE(base::CreateDirectory(output));
      auto png = gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, false);
      ASSERT_TRUE(png.has_value());
      ASSERT_TRUE(base::WriteFile(output.AppendASCII(name), *png));
    }
  }
};

IN_PROC_BROWSER_TEST_F(ZephyrusTitlebarGroupsBrowserTest, GroupMembershipAndStates) {
  toolbar()->GetWidget()->SetBounds(gfx::Rect(40, 40, 1400, 800));
  toolbar()->home_button()->SetVisible(true);
  toolbar()->forward_button()->SetVisible(true);
  Layout();
  auto groups = Groups();
  const auto nav = GroupContaining(groups, toolbar()->reload_button());
  ASSERT_EQ(4u, nav.size());
  EXPECT_EQ(toolbar()->reload_button(), nav[2].view);
  EXPECT_EQ(new_tab(), nav[3].view);
  // The shield stands alone: it is not a navigation control and not an action.
  EXPECT_EQ(1u, GroupContaining(groups, shield()).size());
  const auto caption = GroupContaining(groups, close());
  ASSERT_EQ(3u, caption.size());
  EXPECT_EQ(minimize(), caption[0].view);
  EXPECT_EQ(close(), caption[2].view);
  // Home is an action, so it joins the actions run rather than the arrows.
  EXPECT_NE(toolbar()->reload_button(),
            GroupContaining(groups, toolbar()->home_button())[0].view);

  // Every control is its own circle: no joined runs, no resting container.
  const auto normal = Shape(toolbar()->reload_button());
  EXPECT_FLOAT_EQ(normal.height() / 2,
                  normal.radii(SkRRect::kUpperLeft_Corner).x());
  toolbar()->reload_button()->SetState(views::Button::STATE_PRESSED);
  const auto pressed = Shape(toolbar()->reload_button());
  EXPECT_EQ(normal.rect(), pressed.rect());
  // Pressed squares the corners up (M3's small-button pressed shape).
  EXPECT_FLOAT_EQ(8.f, pressed.radii(SkRRect::kUpperLeft_Corner).x());
  Paint("titlebar-pressed.png");
  toolbar()->reload_button()->SetState(views::Button::STATE_NORMAL);
  Paint("titlebar-normal.png");

  // Removing an optional action leaves the rest as they were: still circles.
  toolbar()->home_button()->SetVisible(false);
  Layout();
  groups = Groups();
  for (const auto& group : groups) {
    EXPECT_NE(toolbar()->home_button(), group[0].view);
    const auto lead = Shape(const_cast<views::View*>(group[0].view.get()));
    EXPECT_FLOAT_EQ(lead.height() / 2,
                    lead.radii(SkRRect::kUpperLeft_Corner).x());
  }
}

IN_PROC_BROWSER_TEST_F(ZephyrusTitlebarGroupsBrowserTest, CustomizedActionsAndResize) {
  auto* model = PinnedToolbarActionsModel::Get(browser()->profile());
  model->UpdatePinnedState(kActionShowChromeLabs, true);
  views::test::WaitForAnimatingLayoutManager(actions());
  Layout();
  auto* pinned = actions()->GetButtonFor(kActionShowChromeLabs);
  ASSERT_TRUE(pinned);
  EXPECT_FALSE(Shape(pinned).isEmpty());
  for (int width : {1400, 900, 640}) {
    toolbar()->GetWidget()->SetBounds(gfx::Rect(40, 40, width, 800));
    Layout();
    const auto groups = Groups();
    ASSERT_GE(groups.size(), 3u);
    for (const auto& group : groups) {
      for (size_t i = 0; i < group.size(); ++i) {
        // A container centred on its control is what keeps the glyph centred.
        const auto shape = Shape(const_cast<views::View*>(group[i].view.get()));
        const gfx::Rect cell = group[i].view->GetLocalBounds();
        EXPECT_NEAR(cell.CenterPoint().x(), shape.rect().centerX(), 0.5f)
            << "segment " << i << " is off centre on its control";
      }
    }
    // State layers are square: a cell narrower than it is tall was what made
    // lone controls read as vertical ovals.
    for (const auto& group : groups) {
      for (const auto& segment : group) {
        const auto shape =
            Shape(const_cast<views::View*>(segment.view.get()));
        EXPECT_NEAR(shape.width(), shape.height(), 1.f)
            << "container is not square";
      }
    }
    Paint("titlebar-narrow.png");
  }
}
