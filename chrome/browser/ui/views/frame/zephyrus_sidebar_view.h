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
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/split_tabs/split_tab_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
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

// The draggable seam between the sidebar panel and the page.
//
// Owned by BrowserView, NOT by the sidebar, and that is the whole point. The
// visible dividing line is not inside the panel: the content card is inset by
// kZephyrusContentMargin (contents_container_view.cc), so the channel the user
// aims at belongs to the contents container. A child of the sidebar is clipped
// to the sidebar's bounds and can never receive an event out there -- which is
// exactly why the first version of this showed no resize cursor at all.
//
// Reports width in ROOT coordinates. Measuring within this view instead would
// drift, because the handle moves as the panel resizes, so every drag frame
// would measure from a new origin and the panel would run away from the cursor.
class ZephyrusSidebarResizeHandle : public views::View {
  METADATA_HEADER(ZephyrusSidebarResizeHandle, views::View)

 public:
  ZephyrusSidebarResizeHandle(base::RepeatingCallback<int()> current_width,
                              base::RepeatingCallback<void(int)> on_width,
                              base::RepeatingClosure on_finished,
                              base::RepeatingClosure on_reset);
  ~ZephyrusSidebarResizeHandle() override;

  // views::View:
  ui::Cursor GetCursor(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;

 private:
  base::RepeatingCallback<int()> current_width_;
  base::RepeatingCallback<void(int)> on_width_;
  base::RepeatingClosure on_finished_;
  base::RepeatingClosure on_reset_;
  int press_root_x_ = 0;
  int start_width_ = 0;
};

// Zephyrus: a floating vertical sidebar that lists the window's tabs (favicon +
// title), highlights the active tab, and allows switching and closing tabs. It
// is the foundation for the workspace switcher (added later). Lives as an
// overlay on the left edge of the browser's content area and slides in/out on
// hover via a GPU layer transform.
class ZephyrusSidebarView : public views::View,
                            public gfx::AnimationDelegate,
                            public TabStripModelObserver,
                            public views::MouseWatcherListener,
                            public views::ContextMenuController,
                            public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(ZephyrusSidebarView, views::View)

 public:
  // 240 per the Zephyrus Browser Design Figma (sidebar "Example" component).
  // This is now only the STARTING width: the user resizes the panel by
  // dragging its right edge, and the choice persists per profile.
  static constexpr int kDefaultSidebarWidth = 240;

  // The range the drag is clamped to. The lower bound is where a tab row stops
  // being readable -- favicon, title, mute and close button all have to fit,
  // and below this the title is elided to a couple of characters, which makes
  // the sidebar useless rather than compact. The upper bound keeps the panel
  // from eating a small laptop screen; at 480 it is already a third of a
  // 1366-wide display.
  static constexpr int kMinSidebarWidth = 180;
  static constexpr int kMaxSidebarWidth = 480;

  // Registered in browser_prefs.cc as a bare string literal, deliberately:
  // //chrome/browser must not depend on //chrome/browser/ui/views (see the
  // note beside the other Zephyrus prefs there). Keep the two spellings in
  // sync -- a typo here means the pref silently reads as 0 and the width
  // clamps to the minimum on every launch.
  static constexpr char kWidthPrefName[] = "zephyrus.sidebar.width";

  // The width to use right now: the live drag value while the user is
  // resizing, otherwise the persisted pref. Always clamped, so a corrupt or
  // hand-edited pref cannot produce an unusable panel.
  //
  // This is the single source of truth for the width. The panel's right edge
  // and the page's left edge are the same seam, so the layout and the slide
  // transform must both come from here -- see TransformForReveal().
  int GetSidebarWidth() const;

  explicit ZephyrusSidebarView(BrowserView* browser_view);
  ZephyrusSidebarView(const ZephyrusSidebarView&) = delete;
  ZephyrusSidebarView& operator=(const ZephyrusSidebarView&) = delete;
  ~ZephyrusSidebarView() override;

  // Slides the sidebar fully into view.
  void Reveal();

  // Resize entry points, driven by ZephyrusSidebarResizeHandle (owned by
  // BrowserView). Width is committed to prefs only when the drag ENDS: writing
  // on every frame would push a pref change, its observers, and eventually a
  // disk write through dozens of times a second for a value the user has not
  // settled on yet.
  void OnResizeDragged(int new_width);
  void OnResizeFinished();
  void ResetWidthToDefault();

  // Pinned: the sidebar stays out and never auto-tucks. Mirrors the title bar's
  // pin (BrowserView::ToggleZephyrusTitlebarPinned).
  bool is_pinned() const { return pinned_; }
  void TogglePinned();

  // 0 tucked, 1 fully out. Drives how much of the window column the layout
  // reserves, so the page's edge tracks the panel instead of jumping once.
  //
  // Deliberately a separate animation from the layer transform that moves the
  // panel: that one carries an overshoot-and-settle chain, and the page's edge
  // should not overshoot with it.
  double reveal_amount() const { return reveal_animation_.GetCurrentValue(); }

  // Whether the column is mid-slide. Lets the browser skip per-frame work that
  // cannot change during a 200ms animation.
  bool is_revealing_or_tucking() const {
    return reveal_animation_.is_animating();
  }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

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
  // Split view. Two tabs shown as one joined card with a break control between
  // them; breaking keeps both tabs and activates whichever was in use more
  // recently.
  void BreakSplit(split_tabs::SplitTabId split_id);

  // Drag-to-reorder, driven by the rows themselves.
  //
  // `y_in_container` is the cursor's position in tab_list_container_'s
  // coordinate space, which is the space the rows are laid out in.
  void OnRowDragged(views::View* row, int y_in_container);
  void OnRowDragFinished();
  void CancelRowDrag();
  // Tucks the panel if the cursor ended the gesture outside it.
  void TuckAwayIfCursorLeft();

  // Rebuilds the tab row list from the current TabStripModel state.
  //
  // DANGER: this destroys every row view, including whichever one the user is
  // currently clicking. Call it directly ONLY from a context that is not
  // running inside a row's own callback; from tab-strip observer callbacks use
  // ScheduleRebuildTabList() instead. See its comment.
  void RebuildTabList();

  // Posts RebuildTabList() so the pending input event finishes unwinding first.
  void ScheduleRebuildTabList();

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

  // Color helpers derived from the active page color (or dark defaults).
  // This window's base theme color (the Private Workspace variant when the
  // window is off the record), pushed in via SetZephyrusColor().
  SkColor GetZephyrusBase() const;
  SkColor GetForegroundColor() const;
  SkColor GetPanelColor() const;

  // Set only while a drag is in progress; GetSidebarWidth() prefers it.
  std::optional<int> drag_width_;
  // True for the duration of a resize drag. Suppresses the auto-tuck: the seam
  // is at the panel's edge, so dragging rightward moves the cursor off the
  // panel, which would otherwise fire MouseMovedOutOfHost and tuck the sidebar
  // away mid-drag -- making the feature unusable.
  bool resizing_ = false;

  raw_ptr<BrowserView> browser_view_;
  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<views::View> tab_list_container_ = nullptr;
  raw_ptr<views::View> favorites_container_ = nullptr;

  std::optional<SkColor> page_color_;

  bool revealed_ = false;
  std::unique_ptr<views::MouseWatcher> mouse_watcher_;
  base::RepeatingTimer reveal_poll_timer_;
  // Reserving the column is now driven by this animation rather than a timer:
  // its end IS the moment the panel is fully off screen, so there is no
  // duration to keep in sync by hand.
  gfx::SlideAnimation reveal_animation_{this};
  bool pinned_ = false;

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

  // --- Drag-to-reorder state -------------------------------------------
  // The tab being dragged, tracked by IDENTITY rather than by index -- the
  // same rule the context menu follows a few members down, and for the same
  // reason: the strip can change mid-gesture and a stale index would reorder
  // a different tab than the one under the cursor.
  raw_ptr<content::WebContents> drag_contents_ = nullptr;
  bool dragging_row_ = false;
  // True while the cursor has left the panel and is over the page. The drop
  // then means "split with what is open" instead of "reorder the list" -- one
  // gesture with two outcomes, chosen by where it ends.
  bool drag_over_content_ = false;
  // Overlay shown on the page while a tab is held over it. Parented into
  // BrowserView so it can cover the web contents, and deliberately incapable
  // of receiving events -- an input-transparent overlay cannot become the
  // invisible sheet that swallowed the whole title bar during the omnibox
  // experiment.
  raw_ptr<views::View> split_drop_indicator_ = nullptr;
  // The tab as it appears once it has left the panel: a stand-in parented into
  // BrowserView so it can travel anywhere in the window.
  raw_ptr<views::View> drag_proxy_ = nullptr;
  gfx::Point drag_grab_offset_;
  void UpdateSplitDropIndicator(bool visible);
  void CreateDragProxy(views::View* row);
  void MoveDragProxyTo(const gfx::Point& cursor_screen);
  void DestroyDragProxy();
  // A rebuild that arrived while a drag was in progress. Running it there
  // would delete the row being dragged -- the use-after-free described on
  // ScheduleRebuildTabList -- so it is held until the gesture ends.
  bool rebuild_deferred_ = false;

  // Guards the posted rebuild against the sidebar being torn down first.
  base::WeakPtrFactory<ZephyrusSidebarView> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SIDEBAR_VIEW_H_
