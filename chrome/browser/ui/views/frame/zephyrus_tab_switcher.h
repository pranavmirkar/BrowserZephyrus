// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_TAB_SWITCHER_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_TAB_SWITCHER_H_

#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/events/event_handler.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/animation/animation_delegate_views.h"
#include "ui/views/view.h"

class Browser;
class BrowserView;
class ZephyrusCarouselItem;
class ZephyrusCarouselRing;

namespace aura {
class Window;
}

namespace content {
class WebContents;
}

namespace views {
class ImageView;
class Label;
class View;
}

// Windows 11 Alt+Tab, for tabs.
//
// Holding Ctrl and pressing Tab opens a floating panel showing the tabs in the
// current workspace, MOST RECENTLY USED FIRST. The first Tab lands on the tab
// you were on before this one, so Ctrl+Tab and release flips between your two
// latest tabs; each further Tab walks further back in time. Releasing Ctrl
// switches to the selected tab; Escape cancels and leaves you where you
// started.
//
// The tabs are an M3 MULTI-BROWSE CAROUSEL centred on the selection: the
// selected tab is the large item, its neighbours are medium, and the next ones
// out are thin slivers at the edges that say there is more. Selection is shown
// by size and by being in front, the way an M3 carousel shows its focal item,
// and marked with M3's focus indicator, since this is keyboard selection.
// Moving it animates every item to its new size at once.
//
// The interesting mechanic is the Ctrl RELEASE. Accelerators only fire on key
// press, so while the switcher is open it installs itself as a pre-target
// handler on the window and watches for the Ctrl key-up itself — that is what
// makes this feel like Alt+Tab instead of a menu that needs dismissing.
//
// A VIEW INSIDE THE BROWSER WINDOW, not a bubble. That removes a hazard the
// bubble version had, where committing closed the widget and destroyed `this`
// mid-callstack. (It was first forced by a backdrop blur, which is gone under
// M3 Expressive.)
//
// The view is created once per window and kept hidden; a session shows it,
// builds the carousel, and hides it again.
class ZephyrusTabSwitcher : public views::View,
                            public views::AnimationDelegateViews {
  METADATA_HEADER(ZephyrusTabSwitcher, views::View)

 public:
  explicit ZephyrusTabSwitcher(BrowserView* browser_view);
  ZephyrusTabSwitcher(const ZephyrusTabSwitcher&) = delete;
  ZephyrusTabSwitcher& operator=(const ZephyrusTabSwitcher&) = delete;
  ~ZephyrusTabSwitcher() override;

  // Entry point for Ctrl+Tab / Ctrl+Shift+Tab. Opens the switcher on the first
  // press and advances the selection on every press after that, so holding Ctrl
  // and tapping Tab walks the list. `forward` is false for Shift+Tab.
  // Returns false if the switcher could not be shown (e.g. fewer than two
  // tabs), so the caller can fall back to plain tab switching.
  static bool CycleOrShow(Browser* browser, bool forward);

  // True while a switching session is in progress.
  static bool IsShowing();

  // views::View:
  void Layout(PassKey) override;
  // Colours are applied here, not in the constructor: this view is built
  // inside BrowserView's constructor, before any ColorProvider exists.
  void OnThemeChanged() override;

  // views::AnimationDelegateViews:
  void AnimationProgressed(const gfx::Animation* animation) override;

 private:
  // views::View already derives from ui::EventHandler, so the switcher cannot
  // be one itself (ambiguous base). This small forwarder is what actually gets
  // installed as the window's pre-target handler.
  class KeyWatcher : public ui::EventHandler {
   public:
    explicit KeyWatcher(ZephyrusTabSwitcher* switcher) : switcher_(switcher) {}
    ~KeyWatcher() override = default;

    // ui::EventHandler:
    void OnKeyEvent(ui::KeyEvent* event) override;

   private:
    const raw_ptr<ZephyrusTabSwitcher> switcher_;
  };

  // Called by KeyWatcher for every key event on the window.
  void OnWindowKeyEvent(ui::KeyEvent* event);

  // One tab in the carousel.
  struct Entry {
    raw_ptr<content::WebContents> contents = nullptr;
    raw_ptr<ZephyrusCarouselItem> item = nullptr;
    // Thumbnails are asked for only as an item nears the visible slots, so a
    // window with dozens of tabs does not subscribe to all of them at once.
    bool thumbnail_requested = false;
    // Kept alive for as long as the switcher is open; dropping it unsubscribes.
    std::unique_ptr<ThumbnailImage::Subscription> subscription;
    // The item's animation, in strip coordinates.
    gfx::Rect from;
    gfx::Rect to;
  };

  // One session's carousel, fixed when it is built: the tab count and the
  // window width decide it, and neither changes while Ctrl is held.
  struct Geometry {
    int large = 0;
    int height = 0;
    // The offsets from the selected tab that get a slot, left to right.
    std::vector<int> offsets;
    int strip_width = 0;
  };

  // Builds the carousel from the current workspace's tabs. Returns false when
  // there is nothing worth switching between.
  bool BuildEntries();
  // Tears the carousel down between sessions. Entries are cleared before the
  // views so their raw_ptrs never outlive what they point at.
  void ClearEntries();
  // Ends the session: removes the key handler, drops the items, hides.
  void EndSession();
  void EnsureThumbnail(size_t index);
  void OnThumbnailReceived(size_t index, gfx::ImageSkia image);

  void AdvanceSelection(bool forward);
  // Gives every item its target for the current selection, then snaps to it
  // (`animate` false) or animates there.
  void ApplyLayout(bool animate);
  // Places every item `progress` of the way from `from` to `to`. Progress runs
  // past 1.0 briefly: the spatial curve overshoots.
  void ApplyProgress(double progress);
  // The selected tab's favicon, title and domain, under the strip.
  void UpdateCaption();

  // Activates the selected tab and closes. CancelSwitch() closes without
  // switching. (Not "Cancel" — DialogDelegate already defines that.)
  void Commit();
  void CancelSwitch();

  const raw_ptr<BrowserView> browser_view_;
  // The floating panel. `this` is a full-window scrim around it.
  raw_ptr<views::View> panel_ = nullptr;
  // The carousel. Items are placed by hand, because their bounds ARE the
  // animation, so it has no layout manager.
  raw_ptr<views::View> strip_ = nullptr;
  // The focus ring around the selected item. Lives in the strip, always on top,
  // and follows the selected item's bounds as they animate.
  raw_ptr<ZephyrusCarouselRing> ring_ = nullptr;
  raw_ptr<views::View> caption_ = nullptr;
  raw_ptr<views::ImageView> caption_favicon_ = nullptr;
  raw_ptr<views::Label> caption_title_ = nullptr;
  raw_ptr<views::Label> caption_domain_ = nullptr;

  std::vector<Entry> entries_;
  size_t selected_ = 0;
  Geometry geometry_;
  // False until the first placement of a session, which snaps: opening the
  // switcher should show where you are, not animate toward it.
  bool placed_ = false;
  gfx::LinearAnimation cycle_animation_;
  KeyWatcher key_watcher_{this};
  // The window we registered the pre-target handler on, so we can remove it.
  raw_ptr<aura::Window> handler_target_ = nullptr;

  base::WeakPtrFactory<ZephyrusTabSwitcher> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_TAB_SWITCHER_H_
