// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_TAB_STRIP_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_TAB_STRIP_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tab_groups/tab_group_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

class BrowserView;
class TabStripModel;

namespace content {
class WebContents;
}

namespace views {
class MenuRunner;
}

// The horizontal-tabs layout's strip: this workspace's tabs in a row under the
// title bar (zephyrus::UiLayout::kHorizontalTabs).
//
// Not Chrome's TabStrip. That one sits ABOVE the toolbar, and its tab shape is
// baked into thousands of lines of painting and drag code; this strip sits
// below the title bar and draws M3 Expressive chips. It reads the same model
// the sidebar does -- workspace filter, pinned tabs, split pairs -- so the two
// layouts always agree about which tabs exist.
//
// Rebuilds are POSTED and coalesced, never run from inside an observer
// callback: a synchronous rebuild deletes the very close button whose click is
// still on the stack.
class ZephyrusTabStrip : public views::View,
                         public gfx::AnimationDelegate,
                         public TabStripModelObserver,
                         public views::ContextMenuController,
                         public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(ZephyrusTabStrip, views::View)

 public:
  // 28dp segments with 4dp below them: compact, since the page gives this
  // height up on every window. Above them, SetTopInset() decides: nothing
  // under the title bar (the bar's own padding is the gap), 4dp when the bar
  // is hidden and the tabs would otherwise touch the window edge.
  // 33dp under the title bar: the 26dp tab sits 1dp down it, which leaves 6dp
  // above it (the bar's own 5dp of bottom padding, plus that 1dp) and 6dp below
  // -- the same gap on both sides.
  static constexpr int kBandHeight = 33;
  // In the title bar's row (one strip) the band is centred on the row.
  static constexpr int kOneStripHeight = 32;
  static constexpr int kTopInsetAlone = 4;
  // ONE-STRIP layout: this same strip (same tabs, same design) lives in the
  // title bar's row, after the omnibox; the last 24dp of the row stays title bar.
  static constexpr int kDragZone = 24;

  void SetTopInset(int inset);

  // ONE-STRIP layout. On, the strip is a child of the toolbar's row: it takes the
  // room the omnibox leaves (so it re-lays out, smoothly, as the omnibox grows
  // and shrinks), paints no background of its own, clips to its bounds, and does
  // not show sets -- they stay in the model and come back with the other
  // layouts.
  void SetOneStrip(bool on);
  bool one_strip() const { return one_strip_; }
  // The bar's own colour, for tabs that lighten it to raise the active one.
  SkColor GroundColor() const;

  explicit ZephyrusTabStrip(BrowserView* browser_view);
  ZephyrusTabStrip(const ZephyrusTabStrip&) = delete;
  ZephyrusTabStrip& operator=(const ZephyrusTabStrip&) = delete;
  ~ZephyrusTabStrip() override;

  // Actions, by tab identity rather than index (an index goes stale the
  // moment the strip reorders before the next rebuild).
  void ActivateTab(base::WeakPtr<content::WebContents> contents);
  void CloseTab(base::WeakPtr<content::WebContents> contents);
  // Mutes a playing tab, or unmutes a muted one. The strip rebuilds after.
  void ToggleTabMuted(base::WeakPtr<content::WebContents> contents);
  void BreakSplit(split_tabs::SplitTabId split_id);
  void NewTab();

  // SETS in the horizontal layout are a pill, not a run of tabs: the pill opens
  // a DROPDOWN LIST of the set's tabs over the page, so the strip stays as
  // short as it is without sets.
  void ToggleSetDropdown(const tab_groups::TabGroupId& id);
  void CloseSetDropdown();
  void NewTabInSet(const tab_groups::TabGroupId& id);
  // The tabs of a set, in order, as last built; null for an unknown set.
  const std::vector<base::WeakPtr<content::WebContents>>* TabsInSet(
      const tab_groups::TabGroupId& id) const;
  // Whether a point (in widget coordinates) is on that set's pill: a press there
  // is the pill's to handle, not an "outside" click.
  bool PillContainsWidgetPoint(const tab_groups::TabGroupId& id,
                               const gfx::Point& point) const;
  // The dropdown has finished closing; it is removed on a posted task.
  void OnSetDropdownClosed();

  // Drag to reorder: the dragged view follows the pointer, and the model moves
  // once, on release.
  // `pointer_x` is where the pointer is: a set is hit by the POINTER, since the
  // held tab is wide and its middle is rarely over a small pill.
  void OnTabDragged(views::View* tab, int x_in_strip, int pointer_x);
  void OnTabDragEnded(views::View* tab,
                      base::WeakPtr<content::WebContents> contents);
  // The same for a set's pill: the set (its tabs with it) follows the pointer,
  // and where it is dropped decides the sets' order.
  void OnPillDragged(views::View* pill, int x_in_strip);
  void OnPillDragEnded(views::View* pill, bool cancelled);

  // views::View:
  void Layout(PassKey key) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  gfx::Size GetMinimumSize() const override;
  bool OnMouseWheel(const ui::MouseWheelEvent& event) override;
  void OnThemeChanged() override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;

  // gfx::AnimationDelegate: the tabs slide to their new places.
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void OnSplitTabChanged(const SplitTabChange& change) override;
  void TabGroupedStateChanged(TabStripModel* tab_strip_model,
                              std::optional<tab_groups::TabGroupId> old_group,
                              std::optional<tab_groups::TabGroupId> new_group,
                              tabs::TabInterface* tab,
                              int index) override;
  void OnTabGroupChanged(const TabGroupChange& change) override;

  // Sets, for the pills that head them. Each edits through the sidebar, which
  // owns the rules, and then rebuilds this strip.
  void ToggleSetCollapsed(const tab_groups::TabGroupId& id);
  void RenameSet(const tab_groups::TabGroupId& id, const std::u16string& name);
  void ChangeSetIcon(const tab_groups::TabGroupId& id,
                     const std::u16string& key);
  // A name is being typed: no rebuild until it is done, or it would delete the
  // field.
  void SetRenameActive(bool active);

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
  // Where an item was before the last rebuild, so it can slide from there; and
  // whether it is new (it grows in instead).
  struct AnimInfo {
    gfx::Rect from;
    bool has_from = false;
    bool entering = false;
    // Among the items that entered together; a set opening staggers them.
    int rank = 0;
  };
  // A closed tab (or set) kept on screen while it shrinks away.
  struct Ghost {
    raw_ptr<views::View> view;
    gfx::Rect from;
    // The surviving item it followed, so the slide keeps it in its place while
    // it shrinks.
    std::string after_key;
  };

  void ClearGhosts();

  // LIVE REORDER while a tab or a set is held. The strip shows its items in
  // `order_` (a permutation of their indices; empty means as built): the held
  // one follows the pointer, the others slide into the places the drop would
  // leave them, so the result is on screen before the pointer lets go.
  using Unit = std::pair<size_t, size_t>;  // [begin, end) item range
  size_t ItemIndex(const views::View* view) const;
  // The items a held tab may be reordered among: its set's tabs, or the loose
  // ones. [begin, end) in item indices.
  std::pair<size_t, size_t> TabGroupRange(size_t item) const;
  // That range as things that move whole: a tab, or a split pair with its seam.
  std::vector<Unit> UnitsIn(size_t begin, size_t end) const;
  // Whether the wider group gap sits between two neighbours on screen.
  bool GapBetween(size_t before, size_t after) const;
  void BeginDragPreview();
  void SetPreviewOrder(std::vector<size_t> order);
  // Records where every item is now and restarts the slide, so whatever the
  // next layout changes is eased rather than jumped to.
  void SlideFromCurrent(bool include_held);
  std::vector<size_t> order_;
  // Rings the set a dragged tab would join (or none).
  void SetDropHighlight(const std::optional<tab_groups::TabGroupId>& set);
  void ClearLayoutAnimation();

  void ScheduleRebuild();
  void Rebuild();
  void OpenSetDropdown(const tab_groups::TabGroupId& id);
  void UpdateSetDropdown();
  void PositionSetDropdown();
  void RemoveSetDropdownNow();
  void UpdatePillsOpen();
  views::View* PillFor(const tab_groups::TabGroupId& id) const;
  // Keeps the active tab inside the visible span after a rebuild or resize.
  void ScrollActiveIntoView();
  int ContentWidth() const;

  raw_ptr<BrowserView> browser_view_;
  raw_ptr<TabStripModel> tab_strip_model_;

  // Children in visual order; the new-tab button is last.
  std::vector<raw_ptr<views::View>> items_;
  // Parallel to items_: true where a wider gap precedes the item (before the
  // first set, between sets, before the loose tabs).
  std::vector<bool> gap_before_;
  // [begin, end) item ranges of each set: its pill and its tabs.
  std::vector<std::pair<size_t, size_t>> set_spans_;
  raw_ptr<views::View> new_tab_button_ = nullptr;
  // The first unpinned item, so layout knows where the flexible run starts.
  size_t first_flexible_ = 0;

  // The slide between two layouts (a tab opened or closed, a set made, opened
  // or closed), ~220ms.
  gfx::SlideAnimation layout_anim_{this};
  std::vector<AnimInfo> anim_info_;
  AnimInfo chip_info_;
  int entering_count_ = 0;
  std::vector<Ghost> ghosts_;
  // Where each item belongs when the animation is over (the end of the slide).
  std::vector<gfx::Rect> targets_;
  // Set by a drag that ended, so the settle is animated too.
  bool force_animation_ = false;
  // Set while a rebuild restarts the slide, so stopping the old one does not
  // tidy away what the rebuild is setting up.
  bool ignore_anim_end_ = false;

  int scroll_offset_ = 0;
  // The open set and its dropdown (a child of the BrowserView, above the page).
  std::optional<tab_groups::TabGroupId> dropdown_set_;
  raw_ptr<views::View> dropdown_ = nullptr;
  std::vector<std::pair<tab_groups::TabGroupId,
                        std::vector<base::WeakPtr<content::WebContents>>>>
      set_tabs_;
  bool one_strip_ = false;
  int top_inset_ = kTopInsetAlone;
  bool rebuild_pending_ = false;
  bool dragging_ = false;
  bool rebuild_deferred_ = false;
  bool rename_active_ = false;
  raw_ptr<views::View> drag_view_ = nullptr;
  int drag_x_ = 0;
  int drag_pointer_x_ = 0;
  // While a pill is dragged: its set's items, which move with it.
  std::pair<size_t, size_t> drag_span_ = {0, 0};

  // The tab active at the last rebuild, to spot a CHANGE of active tab.
  base::WeakPtr<content::WebContents> last_active_;

  // Context menu state. The tab is held by identity for the same reason the
  // actions are.
  base::WeakPtr<content::WebContents> menu_contents_;
  // The set a pill's menu is for, and the sets the "Add to set" submenu lists.
  std::optional<tab_groups::TabGroupId> menu_set_;
  std::vector<tab_groups::TabGroupId> menu_set_ids_;
  std::unique_ptr<ui::SimpleMenuModel> set_submenu_model_;
  std::vector<int> menu_workspace_ids_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> move_menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;

  base::CallbackListSubscription workspace_subscription_;
  base::WeakPtrFactory<ZephyrusTabStrip> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_TAB_STRIP_H_
