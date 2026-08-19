// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Paint coverage for the search overlay.
//
// This exists because the overlay shipped three separate paint-time DCHECK
// crashes that only appeared when a human pressed Ctrl+T on a DCHECK build:
// a focusable chip with no accessible name, and two labels asking for subpixel
// antialiasing inside a non-opaque layer. Each one aborts the paint, so they
// surfaced one at a time — fixing the first only revealed the second.
//
// Constructing the view is not enough to catch any of them. They fire from
// View::Paint, so the test paints the whole window into a canvas. In an
// official Release build these checks are compiled out and the same defects
// render as colour-fringed text instead of crashing, which is why this must
// run on a DCHECK build to mean anything.

#include "chrome/browser/ui/views/frame/zephyrus_search_overlay.h"

#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/compositor/canvas_painter.h"
#include "ui/views/paint_info.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace {

class ZephyrusSearchOverlayBrowserTest : public InProcessBrowserTest {
 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  // Paints the whole window, overlay included, through the same View::Paint
  // path the compositor uses — which is where the accessibility and subpixel
  // checks live. Painting only the overlay would miss anything that depends on
  // the layer it is hosted in, and the layer is the reason two of the three
  // original crashes existed.
  void PaintWindow() {
    views::View* root = browser_view()->GetWidget()->GetRootView();
    root->GetWidget()->LayoutRootViewIfNecessary();
    const gfx::Size size = root->size();
    ASSERT_FALSE(size.IsEmpty());
    SkBitmap bitmap;
    bitmap.allocN32Pixels(size.width(), size.height());
    ui::CanvasPainter painter(&bitmap, size, 1.f, SK_ColorTRANSPARENT,
                              /*is_pixel_canvas=*/false);
    root->Paint(views::PaintInfo::CreateRootPaintInfo(painter.context(), size));
  }
};

// The path the user actually took when this crashed: open the overlay and let
// it paint.
IN_PROC_BROWSER_TEST_F(ZephyrusSearchOverlayBrowserTest, PaintsWhenRevealed) {
  ZephyrusSearchOverlay::Show(browser());
  base::RunLoop().RunUntilIdle();
  PaintWindow();
}

// Dismissal runs an exit animation and hides the card. Painting during that
// window covers the state where the view is still in the tree but on its way
// out, which is not the same tree the reveal test paints.
IN_PROC_BROWSER_TEST_F(ZephyrusSearchOverlayBrowserTest, PaintsWhileDismissing) {
  ZephyrusSearchOverlay::Show(browser());
  base::RunLoop().RunUntilIdle();
  PaintWindow();

  // Show() toggles: a second call dismisses.
  ZephyrusSearchOverlay::Show(browser());
  PaintWindow();
  base::RunLoop().RunUntilIdle();
  PaintWindow();
}

// Reopening rebuilds the chips and re-requests shortcuts and favicons, so the
// second reveal paints a different tree from the first. The engine chip in
// particular is rebuilt, and that chip is where the missing accessible name
// was.
IN_PROC_BROWSER_TEST_F(ZephyrusSearchOverlayBrowserTest, PaintsOnReopen) {
  for (int i = 0; i < 3; ++i) {
    ZephyrusSearchOverlay::Show(browser());  // reveal
    base::RunLoop().RunUntilIdle();
    PaintWindow();
    ZephyrusSearchOverlay::Show(browser());  // dismiss
    base::RunLoop().RunUntilIdle();
  }
}

// RTL rebuilds the layout and flips alignment, and gfx::ALIGN_TO_HEAD on the
// wrong class is exactly the sort of thing that only asserts once mirrored.
class ZephyrusSearchOverlayRtlBrowserTest
    : public ZephyrusSearchOverlayBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    ZephyrusSearchOverlayBrowserTest::SetUpCommandLine(command_line);
    // --lang, not an ICU override: the locale is reset during startup, so a
    // test that only sets the ICU locale passes without ever being in RTL.
    command_line->AppendSwitchASCII("lang", "he");
  }
};

IN_PROC_BROWSER_TEST_F(ZephyrusSearchOverlayRtlBrowserTest, PaintsInRtl) {
  ASSERT_TRUE(base::i18n::IsRTL()) << "the RTL case must actually be RTL";
  ZephyrusSearchOverlay::Show(browser());
  base::RunLoop().RunUntilIdle();
  PaintWindow();
}

}  // namespace
