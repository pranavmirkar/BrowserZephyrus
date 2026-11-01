// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_GLANCE_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_GLANCE_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "content/public/browser/web_contents_delegate.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "url/gurl.h"

class BrowserView;

namespace content {
class WebContents;
}

namespace views {
class WebView;
}

// Glance: a link opened as a card over the page you are on, so you can look at
// it without leaving. Alt+click a link (the setting can change the key); close
// it, or promote it to a real tab, or put it beside the page in split view.
// See chrome/browser/zephyrus/docs/glance-plan.md.
//
// This is the CARD. Which click opens it, and where it sits and moves, are in
// chrome/browser/zephyrus/glance/glance_policy.*; the click interception is in
// ZephyrusGlanceController.
//
// THE PAGE IS NOT A TAB. It is a loose WebContents held by this view, with the
// tab helpers attached so the adblocker and the privacy features see it like
// any other page. A real hidden tab (Zen's way) would appear in the sidebar,
// the session file and tab search, and every one of those would need a filter.
// Expanding hands the same WebContents to the tab strip, so the page does not
// reload.
//
// The card GROWS by painting, not by transforming a native view: the page is a
// native window, and a scale transform above it is exactly where its bounds go
// wrong. So the page is attached only once the card has landed; until then it
// loads in the background behind a painted card.
class ZephyrusGlanceView : public views::View,
                           public content::WebContentsDelegate,
                           public gfx::AnimationDelegate {
  METADATA_HEADER(ZephyrusGlanceView, views::View)

 public:
  explicit ZephyrusGlanceView(BrowserView* browser_view);
  ZephyrusGlanceView(const ZephyrusGlanceView&) = delete;
  ZephyrusGlanceView& operator=(const ZephyrusGlanceView&) = delete;
  ~ZephyrusGlanceView() override;

  // True from the moment a glance starts opening until it has fully closed.
  bool showing() const { return state_ != State::kHidden; }

  // Opens `url` in a card growing from `origin` (in the page area's
  // coordinates), in the same cookie jar as `parent`. Does nothing if a glance
  // is already up or the address is not one we open.
  void Open(const GURL& url,
            content::WebContents* parent,
            const gfx::Point& origin);

  // Animated close. A no-op when nothing is showing.
  void Close();
  // Gone this instant, no animation: the parent tab changed or is closing.
  void CloseImmediately();

  // Becomes a real tab beside the parent, and active. `split` also puts it in
  // split view with the parent.
  void Expand(bool split);

  // The page area's rect in the browser window's coordinates is this view's
  // bounds; called from BrowserView::Layout while a glance is up.
  void FitToContents();

  content::WebContents* contents() { return contents_.get(); }
  base::WeakPtr<ZephyrusGlanceView> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // views::View:
  void Layout(PassKey) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  void OnThemeChanged() override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  // content::WebContentsDelegate:
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override;
  content::WebContents* AddNewContents(
      content::WebContents* source,
      std::unique_ptr<content::WebContents> new_contents,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features,
      bool user_gesture,
      bool* was_blocked) override;
  void CloseContents(content::WebContents* source) override;
  bool HandleContextMenu(content::RenderFrameHost& render_frame_host,
                         const content::ContextMenuParams& params) override;
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override;

 private:
  enum class State { kHidden, kOpening, kOpen, kClosing };

  // Tears the page down and hides the view. Called once the card is gone.
  void Finish();
  // Puts the page into the card: attaches it and gives it focus.
  void RevealPage();
  void UpdateButtons();

  const raw_ptr<BrowserView> browser_view_;
  State state_ = State::kHidden;

  std::unique_ptr<content::WebContents> contents_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<views::View> buttons_ = nullptr;

  gfx::SlideAnimation animation_{this};
  gfx::Rect origin_rect_;
  gfx::Rect card_rect_;
  // Whether the page is attached yet. False while the card is still growing.
  bool page_revealed_ = false;

  base::WeakPtrFactory<ZephyrusGlanceView> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_GLANCE_VIEW_H_
