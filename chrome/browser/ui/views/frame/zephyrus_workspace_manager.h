// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_MANAGER_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_MANAGER_H_

#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "third_party/skia/include/core/SkColor.h"

class Browser;
class Profile;
class TabStripModel;

namespace content {
class WebContents;
}  // namespace content

// A single workspace's identity. Declared at namespace scope so both the
// profile-wide store and the per-window manager can name it.
struct ZephyrusWorkspace {
  int id = 0;
  std::u16string name;
  // Accent color shown on the title-bar button, menu, and sidebar. Opaque.
  SkColor color = SK_ColorTRANSPARENT;
  // Optional leading emoji/glyph (may be empty).
  std::u16string emoji;
};

// Zephyrus: the workspace state that belongs to the PROFILE rather than to any
// one window — the workspace list and the tab->workspace mapping, plus the
// pref that persists them.
//
// Previously every window owned a private copy of this and wrote the whole
// thing (including its own tab order) to one shared pref key, so two windows
// silently clobbered each other and workspace assignments came back scrambled
// after a restart. One store per profile means one writer with a complete view,
// and a tab dragged between windows keeps its workspace because both windows
// read the same map.
class ZephyrusWorkspaceStore : public base::SupportsUserData::Data {
 public:
  // Creates the store on first use and ties its lifetime to `profile`.
  static ZephyrusWorkspaceStore* GetForProfile(Profile* profile);

  explicit ZephyrusWorkspaceStore(Profile* profile);
  ZephyrusWorkspaceStore(const ZephyrusWorkspaceStore&) = delete;
  ZephyrusWorkspaceStore& operator=(const ZephyrusWorkspaceStore&) = delete;
  ~ZephyrusWorkspaceStore() override;

  const std::vector<ZephyrusWorkspace>& workspaces() const {
    return workspaces_;
  }
  std::vector<ZephyrusWorkspace>& workspaces() { return workspaces_; }
  const ZephyrusWorkspace* Get(int workspace_id) const;
  int AllocateWorkspaceId() { return next_workspace_id_++; }
  // The workspace a window should show before the user has picked one.
  int default_workspace_id() const;

  int GetWorkspaceForContents(content::WebContents* contents) const;
  void SetWorkspaceForContents(content::WebContents* contents, int workspace_id);
  void EraseContents(content::WebContents* contents);

  // Remembers the tab each workspace was last on, so switching back returns you
  // where you left off instead of dumping you on the workspace's first tab.
  content::WebContents* GetActiveContents(int workspace_id) const;
  void SetActiveContents(int workspace_id, content::WebContents* contents);
  // Position of a workspace's saved active tab among that workspace's own tabs,
  // read from prefs. Consumed once, when the workspace is first shown after
  // startup (the tabs don't exist yet at load time).
  int TakePendingActiveOrdinal(int workspace_id);

  // Per-workspace visit index (P4). Chromium's history DB is profile-wide and
  // has no workspace concept, and adding a column would mean a DB migration to
  // carry in the fork — so scoping is done with this side index instead: which
  // URLs were opened in which workspace. Bounded per workspace, oldest evicted.
  void RecordVisit(int workspace_id, const GURL& url);
  bool WasVisitedInWorkspace(int workspace_id, const GURL& url) const;
  // True once a workspace has any recorded visits. Until then, scoping must be
  // skipped entirely rather than hiding every suggestion.
  bool HasVisitData(int workspace_id) const;
  // The workspace an omnibox query should be scoped to. See the .cc for why
  // this is resolved from the most recently activated window.
  int CurrentWorkspaceForOmnibox();

  bool has_pending_restore() const { return !pending_restore_ids_.empty(); }
  // Consumes the next restored id, or 0 if none / it no longer exists.
  int TakePendingRestoreId();
  // Drops the whole restore queue. Called once session restore is no longer in
  // progress so a later user-opened tab can't inherit a stale saved id.
  void ClearPendingRestore() { pending_restore_ids_.clear(); }

  // Drops every per-workspace record for a deleted workspace. Without this the
  // visit index keeps serializing the dead workspace's URLs into prefs forever
  // — deleting a workspace must also delete its trail.
  void EraseWorkspaceState(int workspace_id);

  void SchedulePersist();
  void Persist();

  // Resource isolation. A workspace counts as background only when NO window on
  // this profile is showing it — with one store per profile, a second window
  // may well have it on screen. Background tabs are muted at once, and
  // discarded once the workspace has been away for kDiscardGrace. Pinned tabs
  // are exempt: they're visible in every workspace, so they're never background.
  void SetWindowWorkspace(const void* window, int workspace_id);
  void RemoveWindow(const void* window);
  bool IsWorkspaceDisplayed(int workspace_id) const;
  void ApplyResourcePolicy();
  // True when something the user can't see is playing: an audible tab whose
  // workspace isn't on screen in any window. Pinned tabs don't count — they're
  // visible everywhere. Drives the title bar's audio indicator.
  bool HasBackgroundAudio();

  // Fires for any change to the shared state, in every window.
  base::CallbackListSubscription RegisterChangedCallback(
      base::RepeatingClosure callback);
  void NotifyChanged();

 private:
  bool LoadState();
  std::string SerializeState() const;

  raw_ptr<Profile> profile_;
  std::vector<ZephyrusWorkspace> workspaces_;
  int next_workspace_id_ = 1;
  int saved_current_workspace_id_ = 0;
  std::map<content::WebContents*, int> contents_to_workspace_;
  // The tab each workspace was last showing.
  std::map<int, raw_ptr<content::WebContents>> workspace_active_;
  // Saved active-tab positions from prefs, resolved lazily on first switch.
  std::map<int, int> pending_active_ordinal_;
  // Per-tab workspace ids loaded from prefs, consumed in order as the session's
  // tabs are restored. Empty once restore finishes (or for a fresh window).
  std::deque<int> pending_restore_ids_;
  // Only unmute what we muted, so a tab the user silenced stays silenced.
  void UnmuteIfOurs(content::WebContents* contents);
  void RefreshBackgroundTimestamps();

  base::OneShotTimer persist_debounce_timer_;
  base::RepeatingClosureList changed_callbacks_;

  // Which workspace each window is showing, keyed by the window's manager.
  std::map<const void*, int> window_current_workspace_;
  // When each workspace stopped being shown anywhere (absent = on screen).
  std::map<int, base::TimeTicks> workspace_backgrounded_at_;
  std::set<content::WebContents*> muted_by_us_;
  base::RepeatingTimer resource_timer_;

  // Visit index: per workspace, a recency queue for eviction plus a set for
  // lookup, kept in step. Both are keyed by URL spec.
  std::map<int, std::deque<std::string>> visit_order_;
  std::map<int, std::set<std::string>> visit_lookup_;
};

// Zephyrus: Arc-style workspaces. All tabs live in the window's single
// TabStripModel, but each tab is tagged with a workspace id. The UI (sidebar,
// title-bar dropdown) shows only the current workspace's tabs, and switching
// workspaces activates one of the target workspace's tabs. This gives a
// one-window, swap-the-tabs experience without a second TabStripModel.
class ZephyrusWorkspaceManager : public TabStripModelObserver {
 public:
  // The shared struct, kept under the old name so call sites still compile.
  using Workspace = ZephyrusWorkspace;

  // Palette used to auto-assign a distinct color to each new workspace.
  static SkColor DefaultColorForIndex(size_t index);

  explicit ZephyrusWorkspaceManager(Browser* browser);
  ZephyrusWorkspaceManager(const ZephyrusWorkspaceManager&) = delete;
  ZephyrusWorkspaceManager& operator=(const ZephyrusWorkspaceManager&) = delete;
  ~ZephyrusWorkspaceManager() override;

  Browser* browser() const { return browser_; }
  base::WeakPtr<ZephyrusWorkspaceManager> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  const std::vector<Workspace>& workspaces() const;
  int current_workspace_id() const { return current_workspace_id_; }
  const std::u16string& current_workspace_name() const;

  // Switches the active workspace, activating one of its tabs (or opening a new
  // tab if it has none).
  void SwitchToWorkspace(int workspace_id);

  // Switches to the workspace at `index` in the list (no-op if out of range).
  void SwitchToWorkspaceByIndex(size_t index);
  // Switches to the next (+1) / previous (-1) workspace, wrapping around.
  void SwitchToAdjacentWorkspace(int direction);

  // Creates a new workspace (with an empty new tab) and switches to it. Returns
  // its id.
  int AddWorkspace();

  // Renames an existing workspace.
  void RenameWorkspace(int workspace_id, const std::u16string& name);

  // Deletes a workspace, moving its tabs to a neighboring workspace. No-op if
  // it's the only workspace.
  void DeleteWorkspace(int workspace_id);

  // Sets a workspace's accent color / leading emoji.
  void SetWorkspaceColor(int workspace_id, SkColor color);
  void SetWorkspaceEmoji(int workspace_id, const std::u16string& emoji);

  // Returns the workspace with `id`, or nullptr.
  const Workspace* GetWorkspace(int workspace_id) const;

  // Moves `contents` into `workspace_id` (re-tags the tab). Keeps the current
  // workspace's active-tab invariant when the moved tab was showing.
  void MoveContentsToWorkspace(content::WebContents* contents, int workspace_id);

  // The workspace a tab belongs to (0 if untracked).
  int GetWorkspaceForContents(content::WebContents* contents) const;
  bool IsContentsInCurrentWorkspace(content::WebContents* contents) const;

  // Gives any untracked tab (workspace id 0) a real workspace, so a tab can
  // never end up in the strip yet hidden from every sidebar.
  void AdoptUntrackedTabs();

  // True when an off-screen workspace is playing audio (see the store).
  bool HasBackgroundAudio();

  // Pinned tabs are global favourites: they show in EVERY workspace rather than
  // only the one they were created in.
  bool IsContentsPinned(content::WebContents* contents) const;

  // Workspace-isolated tab navigation (for Ctrl+Tab / Ctrl+Shift+Tab). Moves to
  // the next/previous tab within the current workspace, wrapping around.
  // `direction` is +1 (next) or -1 (previous).
  void SelectAdjacentTabInWorkspace(int direction);

  // Keeps the active tab inside the current workspace (e.g. after Ctrl+W
  // auto-selects an adjacent tab that may belong to another workspace). If the
  // current workspace has no tabs left, follows the active tab's workspace.
  void EnsureActiveTabInWorkspace();

  // Notified when the workspace list or current workspace changes.
  base::CallbackListSubscription RegisterChangedCallback(
      base::RepeatingClosure callback);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  // Feeds the per-workspace visit index as tabs navigate.
  void OnTabChangedAt(tabs::TabInterface* tab,
                      int index,
                      TabChangeType change_type) override;

 private:
  // Records this window's workspace, then fans out through the store so every
  // window refreshes — the workspace list and tab mapping are shared state.
  void NotifyChanged();
  // Refreshes only this window's observers. The store calls this on each
  // manager during fan-out; it must not call back into the store.
  void NotifyLocalObservers();
  // Both now live on the profile-wide store; these just forward.
  void PersistState();
  void SchedulePersistState();
  // Workspace id to assign to a newly inserted tab, in priority order:
  // `pending_forced_workspace_id_` (we deliberately opened this tab FOR a
  // workspace), else the next pending restored id, else the `opener`'s
  // workspace (a tab opened by another tab belongs with it), else the current
  // workspace. `opener` may be null.
  int WorkspaceIdForInsertedContents(content::WebContents* opener);

  // Opens a new tab that must land in `workspace_id`, regardless of whichever
  // tab Chromium happens to record as its opener. Without this, a tab created
  // for an empty workspace inherits the still-active tab of the workspace we
  // just left and is filed there instead.
  void AddTabForWorkspace(int workspace_id);

  // Posted when the current workspace loses its last tab: gives it a fresh tab
  // so the user stays put instead of being pulled into another workspace.
  void OpenTabForEmptyCurrentWorkspace();

  raw_ptr<Browser> browser_;
  raw_ptr<TabStripModel> tab_strip_model_;
  // Shared with every other window on this profile. Outlives this manager.
  raw_ptr<ZephyrusWorkspaceStore> store_;
  // Which workspace THIS window is showing. Deliberately per-window: two
  // windows on one profile can display different workspaces.
  int current_workspace_id_ = 0;
  // Set only while a deliberately workspace-targeted tab is being opened; the
  // next inserted tab consumes it and ignores opener inheritance.
  std::optional<int> pending_forced_workspace_id_;
  // Re-broadcasts store changes to this window's observers.
  base::CallbackListSubscription store_changed_subscription_;
  base::RepeatingClosureList changed_callbacks_;
  base::WeakPtrFactory<ZephyrusWorkspaceManager> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_MANAGER_H_
