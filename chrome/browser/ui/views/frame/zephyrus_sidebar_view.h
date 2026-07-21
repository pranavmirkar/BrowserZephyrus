// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SIDEBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SIDEBAR_VIEW_H_

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/mouse_watcher.h"
#include "ui/views/view.h"

class BrowserView;
class TabStripModel;

namespace content {
class WebContents;
}  // namespace content

namespace gfx {
struct VectorIcon;
}  // namespace gfx

namespace views {
class BoxLayout;
class ImageButton;
class MenuRunner;
}  // namespace views

// An invisible thin strip along the window's left edge. When the cursor enters
// it, the sidebar slides in. Kept separate from the sidebar so it can keep
// listening for hover while the sidebar itself is tucked off-screen.
class ZephyrusSidebarHotZone : public views::View {
  METADATA_HEADER(ZephyrusSidebarHotZone, views::View)

 public:
  explicit ZephyrusSidebarHotZone(base::RepeatingClosure on_enter);
  ~ZephyrusSidebarHotZone() override;

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override;

 private:
  base::RepeatingClosure on_enter_;
};

// Zephyrus: a floating vertical sidebar that lists the window's tabs (favicon +
// title), highlights the active tab, and allows switching and closing tabs. It
// is the foundation for the workspace switcher (added later). Lives as an
// overlay on the left edge of the browser's content area and slides in/out on
// hover via a GPU layer transform.
class ZephyrusSidebarView : public views::View,
                            public TabStripModelObserver,
                            public views::MouseWatcherListener,
                            public views::ContextMenuController,
                            public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(ZephyrusSidebarView, views::View)

 public:
  // 240 per the Zephyrus Browser Design Figma (sidebar "Example" component).
  static constexpr int kSidebarWidth = 240;

  explicit ZephyrusSidebarView(BrowserView* browser_view);
  ZephyrusSidebarView(const ZephyrusSidebarView&) = delete;
  ZephyrusSidebarView& operator=(const ZephyrusSidebarView&) = delete;
  ~ZephyrusSidebarView() override;

  // Slides the sidebar fully into view.
  void Reveal();

  // Zephyrus: adapts the sidebar's panel, text and icon colors to the active
  // page color. std::nullopt restores the default dark frosted look.
  void SetZephyrusColor(std::optional<SkColor> page_color);

  // views::View:
  void OnThemeChanged() override;

  // views::MouseWatcherListener:
  void MouseMovedOutOfHost() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      int index,
                      TabChangeType change_type) override;
  // Pinning usually reorders the strip (which rebuilds the list), but not when
  // the tab is already in place — so rebuild explicitly.
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  // Rebuilds the tab row list from the current TabStripModel state.
  void RebuildTabList();

  // Rebuilds the "Favorites" section from the bookmark bar's entries.
  void RebuildFavorites();

  // Animates the layer transform to the tucked-away (off-screen) position.
  void TuckAway();

  // Polls the cursor position; reveals the sidebar when the cursor reaches the
  // window's left edge. Polling is used because mouse-move events over the web
  // contents don't reliably reach this overlay view.
  void OnRevealPoll();

  // Row callbacks.
  void ActivateTab(int model_index);
  void CloseTab(int model_index);
  // Mutes/unmutes a tab straight from its sidebar row, so background audio can
  // be silenced without first hunting down the tab that's making it.
  void ToggleTabMuted(int model_index);

  // Runs a browser command (used by the bottom bar buttons).
  void ExecuteBrowserCommand(int command);
  // Swaps this window for the Private Workspace window (or back out again if
  // this window already is the private one).
  void EnterPrivateWorkspace();

  // Color helpers derived from the active page color (or dark defaults).
  // This window's base theme color (the Private Workspace variant when the
  // window is off the record), pushed in via SetZephyrusColor().
  SkColor GetZephyrusBase() const;
  SkColor GetForegroundColor() const;
  SkColor GetPanelColor() const;

  raw_ptr<BrowserView> browser_view_;
  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<views::View> tab_list_container_ = nullptr;
  raw_ptr<views::View> favorites_container_ = nullptr;

  std::optional<SkColor> page_color_;

  bool revealed_ = false;
  std::unique_ptr<views::MouseWatcher> mouse_watcher_;
  base::RepeatingTimer reveal_poll_timer_;

  // Hidden when there are no bookmarks, so the heading never labels nothing.
  raw_ptr<views::View> favorites_header_ = nullptr;

  // Right-click context menu ("Move to workspace", "Close tab").
  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> workspace_submenu_model_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;
  // The tab the menu was opened on, tracked by identity rather than by index:
  // the strip can change while the menu is open (a background tab closes, a
  // page opens a tab), and a stale index would act on a different tab.
  raw_ptr<content::WebContents> context_menu_contents_ = nullptr;
  std::vector<int> context_menu_workspace_ids_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SIDEBAR_VIEW_H_
