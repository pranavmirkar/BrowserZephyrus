// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_customize_panel.h"

#include <algorithm>
#include <memory>

#include "base/functional/bind.h"
#include "base/numerics/safe_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_type.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "url/gurl.h"

namespace zephyrus {

namespace {

// Long enough to read as movement, short enough not to be in the way. Matches
// the feel of the sidebar's reveal rather than Chromium's side panel, which is
// slower because it is also resizing the page as it goes.
constexpr base::TimeDelta kSlideDuration = base::Milliseconds(180);

}  // namespace

ZephyrusCustomizePanel::ZephyrusCustomizePanel(BrowserView* browser_view)
    : views::AnimationDelegateViews(this), browser_view_(browser_view) {
  // NOTE: views::View is the FIRST base, so the View subobject exists by the
  // time AnimationDelegateViews is handed `this`. With the bases the other way
  // round it is given a View that has not been constructed yet, and the
  // animation quietly never ticks -- which left the panel parked at its fully
  // retracted offset and looking like an empty column.
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));

  // Deliberately no layer ON THE PANEL. (The WebView inside it does have one,
  // for rounding -- see EnsureContent(); the two are unrelated.)
  //
  // The obvious way to slide a view is SetPaintToLayer() and a transform,
  // which is what the sidebar does -- but the sidebar is native views and this
  // hosts a WebView. Putting a layer under a NativeViewHost is not a move this
  // codebase has anywhere, and the one time something similar was tried the
  // web contents came back blank. The panel slides by its BOUNDS instead: its
  // x moves and its width does not, so the WebContents never resizes and there
  // is no reflow to pay for either.

  slide_.SetSlideDuration(kSlideDuration);
  slide_.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);

  // Closed until asked for, and costing nothing until then.
  SetVisible(false);
}

ZephyrusCustomizePanel::~ZephyrusCustomizePanel() = default;

int ZephyrusCustomizePanel::GetReservedWidth() const {
  // On screen at all -- open, or still sliding out -- means the layout keeps
  // the whole column clear. See the header for why this is not animated.
  return (is_open_ || slide_.is_animating()) ? kDefaultWidth : 0;
}

void ZephyrusCustomizePanel::Toggle() {
  is_open_ ? Close() : Open();
}

void ZephyrusCustomizePanel::EnsureContent() {
  if (web_view_ || !browser_view_ || !browser_view_->browser()) {
    return;
  }
  auto* web_view = AddChildView(
      std::make_unique<views::WebView>(browser_view_->browser()->profile()));
  web_view_ = web_view;

  // A LAYER, on the WebView itself -- copied from ContentsWebView, which is
  // the page card and is demonstrably round.
  //
  // The earlier note in this file said a layer under a NativeViewHost blanks
  // the contents. That was true of what was tried: a layer on the PANEL, which
  // is the NativeViewHost's grandparent, combined with a transform. A
  // LAYER_SOLID_COLOR on the WebView is a different thing entirely and is what
  // the content card has done all along.
  //
  // It is what carries the rounding in RoundWebContents(): see there.
  web_view->SetPaintToLayer(ui::LAYER_SOLID_COLOR);

  // Contents FIRST, corners after. The order is not cosmetic: see
  // RoundWebContents().
  content::WebContents* web_contents = web_view->GetWebContents();

  // A WebView can swap its contents, and a new native view arrives square.
  web_contents_attached_ = web_view->AddWebContentsAttachedCallback(
      base::BindRepeating(
          [](ZephyrusCustomizePanel* panel, views::WebView*) {
            panel->RoundWebContents();
          },
          base::Unretained(this)));

  // Register the embedding context BEFORE the first navigation.
  //
  // This WebUI is not in a tab, so it resolves its browser through
  // webui::GetBrowserWindowInterface(). Customize Chrome needs that to reach
  // the theme service at all -- without it the page loads and every control is
  // inert, which reads as a broken panel rather than a missing registration.
  webui::SetBrowserWindowInterface(web_contents, browser_view_->browser());

  static_cast<views::BoxLayout*>(GetLayoutManager())
      ->SetFlexForView(web_view, 1);

  web_view->LoadInitialURL(GURL(chrome::kChromeUICustomizeChromeSidePanelURL));

  RoundWebContents();

  // Colours after the view exists, so the base colour under the page is set
  // before it paints and opening never flashes white.
  ApplyPalette();
}

void ZephyrusCustomizePanel::Open() {
  if (is_open_) {
    return;
  }
  EnsureContent();
  is_open_ = true;
  SetVisible(true);

  // Reset to fully retracted, then slide home. Doing this before the layout
  // means the first painted frame is already off to the right rather than a
  // flash of the panel in place.
  slide_.Reset(0.0);
  slide_.Show();

  if (browser_view_) {
    browser_view_->InvalidateLayout();
  }
}

void ZephyrusCustomizePanel::Close() {
  if (!is_open_) {
    return;
  }
  is_open_ = false;
  // Stays visible and keeps its column until the slide finishes; the layout
  // asks GetReservedWidth(), which accounts for that.
  slide_.Hide();
}

void ZephyrusCustomizePanel::RoundWebContents() {
  if (!web_view_) {
    return;
  }
  const gfx::RoundedCornersF radii(kRadiusCard);

  // TWO layers, not one. This is the whole of the corner bug.
  //
  // The return value of SetCornerRadii is worthless: BOTH wrappers
  // (NativeViewHostAura and NativeViewHostAuraWithClipWindow -- Windows gets
  // the latter, it is enabled by default off ChromeOS and Linux) end with a
  // bare `return true`. ApplyRoundedCorners() behind it early-returns when
  // there is no native view yet. So the "MEASURED: ok=1" that this comment
  // used to cite was measuring a constant, and proved nothing about whether a
  // single pixel had been rounded.
  //
  // What actually paints at these corners is a stack, and each member clips
  // only itself:
  //   1. the panel's background/border  -- rounded in ApplyPalette()
  //   2. the WebView's own layer        -- below; ALSO clips its descendants
  //   3. the native view host           -- below
  // The renderer's compositor surface is a descendant, and rounding (3) alone
  // left it square, which is what leaked past the card at the corners and the
  // top and bottom edges. ContentsContainerView::UpdateBorderRoundedCorners
  // hit exactly this on the page card and records the same conclusion.
  //
  // kRadiusCard is a whole number of pixels, which matters: a layer-rounded
  // corner at a fractional value renders blurry under display scaling (the
  // note on kPopupCornerRadius in ZephyrusSettingsPopup records measuring
  // exactly that). It stays 8 even though the WebView sits 1px inside the
  // border -- an 8 arc inset by 1 falls inside the border's 7 arc, so nothing
  // bleeds, and 7 would be half a pixel at 1.5x scaling.
  web_view_->holder()->SetCornerRadii(radii);
  if (ui::Layer* layer = web_view_->layer()) {
    layer->SetRoundedCornerRadius(radii);
    layer->SetIsFastRoundedCorner(true);
  }
}

void ZephyrusCustomizePanel::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  views::View::OnBoundsChanged(previous_bounds);
  // Re-round on every resize.
  //
  // MEASURED: the call made in EnsureContent() SUCCEEDS -- it returns true --
  // but it runs while the panel and its WebView are still 0x0, before any
  // layout has happened. The radii are applied to a native view that has no
  // size yet and are gone by the time it has one, which is why the corners
  // stayed square through several attempts at fixing the "timing".
  //
  // Cheap and idempotent, so the honest fix is to say it again whenever the
  // size changes rather than to guess at the one right moment.
  RoundWebContents();
}

int ZephyrusCustomizePanel::SlideOffset() const {
  // 1 at rest, 0 fully retracted.
  return base::ClampFloor(kDefaultWidth * (1.0 - slide_.GetCurrentValue()));
}

void ZephyrusCustomizePanel::AnimationProgressed(
    const gfx::Animation* animation) {
  // Only the panel is repositioned -- not a full layout. The page's column was
  // settled when the panel opened and does not move again during the slide.
  if (browser_view_) {
    browser_view_->UpdateZephyrusCustomizePanelBounds();
  }
}

void ZephyrusCustomizePanel::AnimationEnded(const gfx::Animation* animation) {
  if (!is_open_) {
    // Only now is the column actually free.
    SetVisible(false);
    if (browser_view_) {
      browser_view_->InvalidateLayout();
    }
    return;
  }
  if (browser_view_) {
    browser_view_->UpdateZephyrusCustomizePanelBounds();
  }
}

void ZephyrusCustomizePanel::OnThemeChanged() {
  views::View::OnThemeChanged();
  ApplyPalette();
}

void ZephyrusCustomizePanel::ApplyPalette() {
  // The same card as the agent panel: a rounded surface and a 1px hairline.
  // Separation is a line, not a shadow.
  //
  // A flush, borderless version painted in the window's colour was tried on
  // 2026-09-10 and reverted -- it dissolved the panel into the chrome, which
  // took the card language with it.
  const Palette palette = PaletteFor(*this);
  const SkColor ground = palette.ground;
  SetBackground(views::CreateRoundedRectBackground(ground, kRadiusCard));
  SetBorder(views::CreateRoundedRectBorder(1, kRadiusCard, palette.rule));

  if (!web_view_) {
    return;
  }
  // The LAYER's colour, not a View background.
  //
  // With SetPaintToLayer(LAYER_SOLID_COLOR) the view no longer paints itself,
  // so a views::Background here would be dead code -- the layer's own colour
  // is the fill.
  if (ui::Layer* layer = web_view_->layer()) {
    layer->SetColor(ground);
  }

  // And the renderer's own fill, so the page does not flash a default white
  // rectangle into the window before its first frame arrives. ContentsWebView
  // sets both for the same reason; the colour has to be opaque, which the
  // window's ground always is.
  if (content::WebContents* contents = web_view_->GetWebContents()) {
    contents->SetPageBaseBackgroundColor(ground);
    if (content::RenderWidgetHostView* rwhv =
            contents->GetRenderWidgetHostView()) {
      rwhv->SetBackgroundColor(ground);
    }
  }
}

BEGIN_METADATA(ZephyrusCustomizePanel)
END_METADATA

}  // namespace zephyrus
