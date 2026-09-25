// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_TAB_STRIP_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_TAB_STRIP_H_

#include <memory>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/split_tabs/split_tab_id.h"
#include "ui/base/metadata/metadata_header_macros.h"
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
                         public TabStripModelObserver,
                         public views::ContextMenuController,
                         public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(ZephyrusTabStrip, views::View)

 public:
  // 28dp segments with 4dp below them: compact, since the page gives this
  // height up on every window. Above them, SetTopInset() decides: nothing
  // under the title bar (the bar's own padding is the gap), 4dp when the bar
  // is hidden and the tabs would otherwise touch the window edge.
  static constexpr int kBandHeight = 32;
  static constexpr int kTopInsetAlone = 4;

  void SetTopInset(int inset);

  explicit ZephyrusTabStrip(BrowserView* browser_view);
  ZephyrusTabStrip(const ZephyrusTabStrip&) = delete;
  ZephyrusTabStrip& operator=(const ZephyrusTabStrip&) = delete;
  ~ZephyrusTabStrip() override;

  // Actions, by tab identity rather than index (an index goes stale the
  // moment the strip reorders before the next rebuild).
  void ActivateTab(base::WeakPtr<content::WebContents> contents);
  void CloseTab(base::WeakPtr<content::WebContents> contents);
  void BreakSplit(split_tabs::SplitTabId split_id);
  void NewTab();

  // Drag to reorder: the dragged view follows the pointer, and the model moves
  // once, on release.
  void OnTabDragged(views::View* tab, int x_in_strip);
  void OnTabDragEnded(views::View* tab,
                      base::WeakPtr<content::WebContents> contents);

  // views::View:
  void Layout(PassKey key) override;
  bool OnMouseWheel(const ui::MouseWheelEvent& event) override;
  void OnThemeChanged() override;
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      int index,
                      TabChangeType change_type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void OnSplitTabChanged(const SplitTabChange& change) override;

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
  void ScheduleRebuild();
  void Rebuild();
  // Keeps the active tab inside the visible span after a rebuild or resize.
  void ScrollActiveIntoView();
  int ContentWidth() const;

  raw_ptr<BrowserView> browser_view_;
  raw_ptr<TabStripModel> tab_strip_model_;

  // Children in visual order; the new-tab button is last.
  std::vector<raw_ptr<views::View>> items_;
  raw_ptr<views::View> new_tab_button_ = nullptr;
  // The first unpinned item, so layout knows where the flexible run starts.
  size_t first_flexible_ = 0;

  int scroll_offset_ = 0;
  int top_inset_ = kTopInsetAlone;
  bool rebuild_pending_ = false;
  bool dragging_ = false;
  bool rebuild_deferred_ = false;
  raw_ptr<views::View> drag_view_ = nullptr;
  int drag_x_ = 0;

  // The tab active at the last rebuild, to spot a CHANGE of active tab.
  base::WeakPtr<content::WebContents> last_active_;

  // Context menu state. The tab is held by identity for the same reason the
  // actions are.
  base::WeakPtr<content::WebContents> menu_contents_;
  std::vector<int> menu_workspace_ids_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> move_menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;

  base::CallbackListSubscription workspace_subscription_;
  base::WeakPtrFactory<ZephyrusTabStrip> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_TAB_STRIP_H_
