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
#include "ui/gfx/geometry/size.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/events/event_handler.h"
#include "ui/views/view.h"

class Browser;
class BrowserView;

namespace aura {
class Window;
}

namespace content {
class WebContents;
}

namespace views {
class ImageView;
class View;
}

// Windows 11 Alt+Tab, for tabs.
//
// Holding Ctrl and pressing Tab opens a floating card showing every tab in the
// current workspace as a live page thumbnail. Each further Tab moves the
// selection; releasing Ctrl switches to the selected tab; Escape cancels and
// leaves you where you started.
//
// The interesting mechanic is the Ctrl RELEASE. Accelerators only fire on key
// press, so while the switcher is open it installs itself as a pre-target
// handler on the window and watches for the Ctrl key-up itself — that is what
// makes this feel like Alt+Tab instead of a menu that needs dismissing.
//
// A VIEW INSIDE THE BROWSER WINDOW, not a bubble. Two reasons: the card strip
// carries a compositor backdrop blur, and a backdrop filter can only sample its
// own compositor frame — as a separate widget there was nothing behind it. It
// also removes a hazard the bubble version had, where committing closed the
// widget and destroyed `this` mid-callstack.
//
// The view is created once per window and kept hidden; a session shows it,
// builds cards, and hides it again.
class ZephyrusTabSwitcher : public views::View {
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

  // One card in the strip: thumbnail + title, highlighted when selected.
  struct Entry {
    raw_ptr<content::WebContents> contents = nullptr;
    raw_ptr<views::View> card = nullptr;
    raw_ptr<views::ImageView> image = nullptr;
    // Kept alive for as long as the switcher is open; dropping it unsubscribes.
    std::unique_ptr<ThumbnailImage::Subscription> subscription;
  };

  // Builds the card strip from the current workspace's tabs. Returns false when
  // there is nothing worth switching between.
  bool BuildEntries();
  // Tears the strip down between sessions. Entries are cleared before the views
  // so their raw_ptrs never outlive what they point at.
  void ClearEntries();
  // Ends the session: removes the key handler, drops the cards, hides.
  void EndSession();
  // Asks each tab for its latest thumbnail; they arrive asynchronously.
  void RequestThumbnails();
  void OnThumbnailReceived(size_t index, gfx::ImageSkia image);

  void AdvanceSelection(bool forward);
  void UpdateSelectionVisuals();

  // Activates the selected tab and closes. CancelSwitch() closes without
  // switching. (Not "Cancel" — DialogDelegate already defines that.)
  void Commit();
  void CancelSwitch();

  const raw_ptr<BrowserView> browser_view_;
  // The card strip. `this` is a full-window scrim around it.
  raw_ptr<views::View> panel_ = nullptr;
  std::vector<Entry> entries_;
  size_t selected_ = 0;
  // Card thumbnail size, computed at build time to fit the window.
  gfx::Size thumb_size_;
  KeyWatcher key_watcher_{this};
  // The window we registered the pre-target handler on, so we can remove it.
  raw_ptr<aura::Window> handler_target_ = nullptr;

  base::WeakPtrFactory<ZephyrusTabSwitcher> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_TAB_SWITCHER_H_
