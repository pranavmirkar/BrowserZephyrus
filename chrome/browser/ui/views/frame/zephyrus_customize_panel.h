// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_CUSTOMIZE_PANEL_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_CUSTOMIZE_PANEL_H_

#include <memory>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/views/animation/animation_delegate_views.h"
#include "ui/views/view.h"

class BrowserView;

namespace views {
class WebView;
}

namespace zephyrus {

// "Customize Zephyrus", as a Zephyrus panel rather than a Chromium side panel.
//
// WHY THIS EXISTS, given Chromium already has a side panel that hosts exactly
// this page.
//
// Chromium's SidePanel is a pane of the WINDOW: it pins itself to the frame,
// paints the browser's themed background, and owns a column the page is then
// squeezed out of. Zephyrus's chrome is not built that way -- the page is a
// card floating in the window, and the sidebar and the agent panel are cards
// beside it. Retuning the side panel's padding and radii to imitate that was
// tried, and it is the wrong shape of fix: it makes an upstream pane look
// approximately like a card while still being laid out as a pane, and every
// value has to be re-guessed on each rebase.
//
// This is the same construction as ZephyrusAgentPanel, which is already a
// working right-hand panel: a rounded surface with a hairline, a reserved
// width the layout takes off the contents, and bounds derived from the content
// card so the two edges agree. The only new part is that its content is a
// WebView rather than native views, and ZephyrusSettingsPopup already
// establishes how to host a WebUI inside a Zephyrus surface.
//
// It is a CARD, like the sidebar and the agent panel. A flush chrome-coloured
// version was tried on 2026-09-10 and reverted the same evening.
class ZephyrusCustomizePanel : public views::View,
                               public views::AnimationDelegateViews {
  METADATA_HEADER(ZephyrusCustomizePanel, views::View)

 public:
  // Matches ZephyrusAgentPanel so the two right-hand panels are the same
  // column, rather than the window changing width depending which is open.
  static constexpr int kDefaultWidth = 340;

  explicit ZephyrusCustomizePanel(BrowserView* browser_view);
  ~ZephyrusCustomizePanel() override;

  ZephyrusCustomizePanel(const ZephyrusCustomizePanel&) = delete;
  ZephyrusCustomizePanel& operator=(const ZephyrusCustomizePanel&) = delete;

  bool is_open() const { return is_open_; }

  // What the layout must take off the right of the contents.
  //
  // The FULL width for as long as the panel is on screen at all, including
  // while it slides out -- not a fraction of it. Animating this instead would
  // resize the page on every frame of the slide, which is the reflow storm the
  // sidebar needed a renderer pin to avoid. Reserving once means the page
  // resizes exactly twice per open/close, and the movement the user sees is
  // the panel's own x sliding, which the WebContents never feels.
  int GetReservedWidth() const;

  // How far right of its resting place the panel currently sits, in pixels.
  //
  // Zero at rest, kDefaultWidth when fully retracted. BrowserView adds this to
  // the panel's x, which is the whole of the animation.
  int SlideOffset() const;

  void Toggle();
  void Open();
  void Close();

  // views::View:
  void OnThemeChanged() override;
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;

  // views::AnimationDelegateViews:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  void ApplyPalette();

  // Built on first open, not in the constructor.
  //
  // Every browser window would otherwise pay for a WebUI it may never show.
  // The agent panel gets away with native views; a WebContents is not free.
  void EnsureContent();

  // Rounds the hosted page to the card radius.
  //
  // Rounds TWO layers, and the reason is in the .cc: the native view host and
  // the WebView's own layer, because the renderer's surface is a descendant of
  // the latter and rounding only the former leaves it square.
  //
  // Re-run from OnBoundsChanged and from the attach callback rather than at
  // one supposedly-correct moment: SetCornerRadii() returns true
  // unconditionally in both NativeViewHost wrappers, so there is no way to ask
  // whether a given call took effect -- saying it again is cheap, idempotent,
  // and does not depend on guessing when the native view arrives.
  void RoundWebContents();

  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<views::WebView> web_view_ = nullptr;

  // Open means "the user wants it", which is true from the moment the slide
  // starts and false from the moment the slide out starts. `slide_` is where
  // it actually is on screen; the two differ only during the animation.
  bool is_open_ = false;
  gfx::SlideAnimation slide_{this};
  base::CallbackListSubscription web_contents_attached_;
};

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_CUSTOMIZE_PANEL_H_
