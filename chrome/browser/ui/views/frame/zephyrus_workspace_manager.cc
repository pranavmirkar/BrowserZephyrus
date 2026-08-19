// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/json/json_writer.h"
#include "base/task/sequenced_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/resource_coordinator/lifecycle_unit_state.mojom.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "components/tabs/public/tab_interface.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace {
// Profile pref holding the serialized workspace state (list + current + the
// workspace id of each tab in tab-strip order). Registered in browser_prefs.cc.
constexpr char kZephyrusWorkspacesPref[] = "zephyrus.workspaces";

// Distinct, pleasant accent colors auto-assigned to workspaces by index.
constexpr SkColor kWorkspacePalette[] = {
    SkColorSetRGB(0x4f, 0x8c, 0xff),  // blue
    SkColorSetRGB(0x9c, 0x6c, 0xff),  // purple
    SkColorSetRGB(0xff, 0x6c, 0x9c),  // pink
    SkColorSetRGB(0xff, 0x8a, 0x4f),  // orange
    SkColorSetRGB(0x35, 0xc7, 0x8a),  // green
    SkColorSetRGB(0xff, 0xc7, 0x4f),  // amber
    SkColorSetRGB(0x4f, 0xc7, 0xff),  // cyan
    SkColorSetRGB(0xff, 0x5c, 0x5c),  // red
};
}  // namespace

// static
SkColor ZephyrusWorkspaceManager::DefaultColorForIndex(size_t index) {
  return kWorkspacePalette[index % std::size(kWorkspacePalette)];
}

// ---------------------------------------------------------------------------
// ZephyrusWorkspaceStore (profile-wide)

namespace {
// Key for attaching the store to the Profile. Its address is the identity.
constexpr char kZephyrusWorkspaceStoreKey[] = "zephyrus_workspace_store";
}  // namespace

// static
ZephyrusWorkspaceStore* ZephyrusWorkspaceStore::GetForProfile(
    Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  auto* store = static_cast<ZephyrusWorkspaceStore*>(
      profile->GetUserData(kZephyrusWorkspaceStoreKey));
  if (!store) {
    auto owned = std::make_unique<ZephyrusWorkspaceStore>(profile);
    store = owned.get();
    profile->SetUserData(kZephyrusWorkspaceStoreKey, std::move(owned));
  }
  return store;
}

ZephyrusWorkspaceStore::ZephyrusWorkspaceStore(Profile* profile)
    : profile_(profile) {
  // A Private Workspace store starts blank, every session. Its profile is
  // off-the-record, whose PrefService is an overlay that reads THROUGH to the
  // regular profile — so calling LoadState() here would pull the regular
  // profile's workspaces, its pending tab tags, and its visit index into the
  // private session. That both hid the private window's own tab (it got tagged
  // with an inherited workspace id that wasn't the window's current one) and
  // read regular browsing data into a session that is supposed to know none of
  // it. Skip the load: a single fresh default workspace, nothing inherited.
  const bool is_private = profile_ && profile_->IsOffTheRecord();
  // Restore the saved workspace list (and the per-tab order used to re-tag
  // restored tabs). Falls back to a single default workspace on first run.
  if (is_private || !LoadState()) {
    workspaces_.push_back({next_workspace_id_, u"Workspace 1",
                           ZephyrusWorkspaceManager::DefaultColorForIndex(0),
                           u""});
    saved_current_workspace_id_ = next_workspace_id_;
    ++next_workspace_id_;
  }
}

ZephyrusWorkspaceStore::~ZephyrusWorkspaceStore() = default;

const ZephyrusWorkspace* ZephyrusWorkspaceStore::Get(int workspace_id) const {
  for (const ZephyrusWorkspace& workspace : workspaces_) {
    if (workspace.id == workspace_id) {
      return &workspace;
    }
  }
  return nullptr;
}

int ZephyrusWorkspaceStore::default_workspace_id() const {
  if (Get(saved_current_workspace_id_)) {
    return saved_current_workspace_id_;
  }
  return workspaces_.empty() ? 0 : workspaces_.front().id;
}

int ZephyrusWorkspaceStore::GetWorkspaceForContents(
    content::WebContents* contents) const {
  const auto it = contents_to_workspace_.find(contents);
  return it == contents_to_workspace_.end() ? 0 : it->second;
}

void ZephyrusWorkspaceStore::SetWorkspaceForContents(
    content::WebContents* contents,
    int workspace_id) {
  contents_to_workspace_[contents] = workspace_id;
}

void ZephyrusWorkspaceStore::EraseContents(content::WebContents* contents) {
  contents_to_workspace_.erase(contents);
  // Drop it from the muted set too. Left behind, the pointer dangles — and a
  // later tab allocated at the same address would look like one we muted, so
  // we'd "restore" audio the user never asked us to touch.
  muted_by_us_.erase(contents);
  // Don't leave a workspace pointing at a tab that no longer exists.
  for (auto it = workspace_active_.begin(); it != workspace_active_.end();) {
    it = (it->second == contents) ? workspace_active_.erase(it)
                                  : std::next(it);
  }
}

content::WebContents* ZephyrusWorkspaceStore::GetActiveContents(
    int workspace_id) const {
  const auto it = workspace_active_.find(workspace_id);
  return it == workspace_active_.end() ? nullptr : it->second;
}

void ZephyrusWorkspaceStore::SetActiveContents(
    int workspace_id,
    content::WebContents* contents) {
  if (contents) {
    workspace_active_[workspace_id] = contents;
  }
}

int ZephyrusWorkspaceStore::TakePendingActiveOrdinal(int workspace_id) {
  const auto it = pending_active_ordinal_.find(workspace_id);
  if (it == pending_active_ordinal_.end()) {
    return -1;
  }
  const int ordinal = it->second;
  pending_active_ordinal_.erase(it);
  return ordinal;
}

int ZephyrusWorkspaceStore::TakePendingRestoreId() {
  if (pending_restore_ids_.empty()) {
    return 0;
  }
  const int id = pending_restore_ids_.front();
  pending_restore_ids_.pop_front();
  return Get(id) ? id : 0;
}

base::CallbackListSubscription
ZephyrusWorkspaceStore::RegisterChangedCallback(
    base::RepeatingClosure callback) {
  return changed_callbacks_.Add(std::move(callback));
}

void ZephyrusWorkspaceStore::NotifyChanged() {
  changed_callbacks_.Notify();
}

bool ZephyrusWorkspaceStore::IsWorkspaceDisplayed(int workspace_id) const {
  for (const auto& [window, shown] : window_current_workspace_) {
    if (shown == workspace_id) {
      return true;
    }
  }
  return false;
}

void ZephyrusWorkspaceStore::RefreshBackgroundTimestamps() {
  const base::TimeTicks now = base::TimeTicks::Now();
  for (const ZephyrusWorkspace& workspace : workspaces_) {
    if (IsWorkspaceDisplayed(workspace.id)) {
      workspace_backgrounded_at_.erase(workspace.id);
    } else {
      // Start the clock only on the transition, so a workspace that has been
      // away a long time isn't given a fresh grace period on every tick.
      workspace_backgrounded_at_.try_emplace(workspace.id, now);
    }
  }
}

void ZephyrusWorkspaceStore::SetWindowWorkspace(const void* window,
                                                int workspace_id) {
  const auto it = window_current_workspace_.find(window);
  if (it != window_current_workspace_.end() && it->second == workspace_id) {
    return;
  }
  window_current_workspace_[window] = workspace_id;
  RefreshBackgroundTimestamps();
  // Silence the workspace we just left immediately; waiting for the timer would
  // leave it audible for up to a tick.
  ApplyResourcePolicy();
  if (!resource_timer_.IsRunning()) {
    resource_timer_.Start(FROM_HERE, base::Minutes(1), this,
                          &ZephyrusWorkspaceStore::ApplyResourcePolicy);
  }
}

void ZephyrusWorkspaceStore::RemoveWindow(const void* window) {
  window_current_workspace_.erase(window);
  RefreshBackgroundTimestamps();
}

void ZephyrusWorkspaceStore::UnmuteIfOurs(content::WebContents* contents) {
  const auto it = muted_by_us_.find(contents);
  if (it != muted_by_us_.end()) {
    contents->SetAudioMuted(false);
    muted_by_us_.erase(it);
  }
}

void ZephyrusWorkspaceStore::ApplyResourcePolicy() {
  // How long a workspace must be off screen before its tabs are discarded.
  // Discarding is lossy (the tab reloads), so it deliberately doesn't happen
  // the instant you switch away — that would make switching back feel slow.
  constexpr base::TimeDelta kDiscardGrace = base::Minutes(5);
  const base::TimeTicks now = base::TimeTicks::Now();

  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (!browser || browser->GetProfile() != profile_) {
          return true;
        }
        TabStripModel* model = browser->GetTabStripModel();
        if (!model || model->closing_all()) {
          return true;
        }
        for (int i = 0; i < model->count(); ++i) {
          content::WebContents* contents = model->GetWebContentsAt(i);
          if (!contents) {
            continue;
          }
          // Pinned tabs appear in every workspace, and the active tab is by
          // definition in use — neither is ever "background".
          const int workspace = GetWorkspaceForContents(contents);
          if (model->IsTabPinned(i) || i == model->active_index() ||
              IsWorkspaceDisplayed(workspace)) {
            UnmuteIfOurs(contents);
            continue;
          }
          // Deliberate background listening — music, a podcast, a lecture — is
          // a first-class use of another workspace, so a tab that is actually
          // playing is left completely alone: not muted, not discarded.
          if (contents->IsCurrentlyAudible()) {
            continue;
          }
          // Silent and off screen: mute pre-emptively. This changes nothing the
          // user can hear right now, but it means a tab that starts autoplaying
          // while hidden stays quiet instead of shouting from a workspace they
          // aren't even looking at.
          if (!contents->IsAudioMuted()) {
            contents->SetAudioMuted(true);
            muted_by_us_.insert(contents);
          }
          // And reclaim its memory once the workspace has been away a while.
          const auto since = workspace_backgrounded_at_.find(workspace);
          if (since != workspace_backgrounded_at_.end() &&
              now - since->second >= kDiscardGrace) {
            if (auto* lifecycle =
                    resource_coordinator::TabLifecycleUnitExternal::
                        FromWebContents(contents)) {
              lifecycle->DiscardTab(
                  mojom::LifecycleUnitDiscardReason::PROACTIVE);
            }
          }
        }
        return true;
      });
}

namespace {
// The canonical form a URL takes in the visit index — used identically at
// record and lookup time so the two always agree. Strips what should never be
// written to the Preferences file: embedded credentials (user:pass@host) and
// fragments. Returns empty for URLs not worth indexing.
std::string ZephyrusVisitKey(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return std::string();
  }
  GURL::Replacements strip;
  strip.ClearUsername();
  strip.ClearPassword();
  strip.ClearRef();
  const std::string spec = url.ReplaceComponents(strip).spec();
  // Very long URLs are disproportionately auth callbacks and signed links —
  // exactly the tokens that must not sit in a plaintext pref. Their loss to
  // suggestion scoping is no loss at all.
  constexpr size_t kMaxVisitSpecLength = 1024;
  return spec.size() > kMaxVisitSpecLength ? std::string() : spec;
}
}  // namespace

void ZephyrusWorkspaceStore::RecordVisit(int workspace_id, const GURL& url) {
  // Cap per workspace: this rides in a pref, and an unbounded index would grow
  // the Preferences file without limit.
  constexpr size_t kMaxVisitsPerWorkspace = 150;
  // Only ordinary web pages are worth scoping. chrome:// pages, the NTP and
  // blank entries aren't things the user "visited in a workspace".
  const std::string spec = ZephyrusVisitKey(url);
  if (workspace_id == 0 || spec.empty()) {
    return;
  }
  std::set<std::string>& lookup = visit_lookup_[workspace_id];
  if (!lookup.insert(spec).second) {
    return;  // Already known; leave its position alone.
  }
  std::deque<std::string>& order = visit_order_[workspace_id];
  order.push_back(spec);
  while (order.size() > kMaxVisitsPerWorkspace) {
    lookup.erase(order.front());
    order.pop_front();
  }
  SchedulePersist();
}

bool ZephyrusWorkspaceStore::WasVisitedInWorkspace(int workspace_id,
                                                   const GURL& url) const {
  const auto it = visit_lookup_.find(workspace_id);
  // Same canonical form as RecordVisit, or a URL with a #fragment would never
  // match its recorded fragment-less twin.
  return it != visit_lookup_.end() &&
         it->second.count(ZephyrusVisitKey(url)) > 0;
}

void ZephyrusWorkspaceStore::EraseWorkspaceState(int workspace_id) {
  workspace_active_.erase(workspace_id);
  pending_active_ordinal_.erase(workspace_id);
  workspace_backgrounded_at_.erase(workspace_id);
  visit_order_.erase(workspace_id);
  visit_lookup_.erase(workspace_id);
}

bool ZephyrusWorkspaceStore::HasVisitData(int workspace_id) const {
  const auto it = visit_lookup_.find(workspace_id);
  return it != visit_lookup_.end() && !it->second.empty();
}

int ZephyrusWorkspaceStore::CurrentWorkspaceForOmnibox() {
  // The omnibox asks through a PROFILE-scoped client, which has no window or
  // page context, while the displayed workspace is per-window. Resolve it from
  // the most recently activated window on this profile — that's the one being
  // typed in. Reading its active tab's workspace (rather than the window's
  // current id) also keeps this correct if the two ever diverge.
  //
  // Limitation: with two windows showing different workspaces, a query issued
  // in the non-active one is scoped to the active one's workspace.
  int workspace = 0;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (!browser || browser->GetProfile() != profile_) {
          return true;
        }
        TabStripModel* model = browser->GetTabStripModel();
        if (!model || model->closing_all()) {
          return true;
        }
        if (content::WebContents* active = model->GetActiveWebContents()) {
          workspace = GetWorkspaceForContents(active);
          return false;  // Most recently activated wins.
        }
        return true;
      },
      BrowserCollection::Order::kActivation);
  return workspace;
}

bool ZephyrusWorkspaceStore::HasBackgroundAudio() {
  bool found = false;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (found || !browser || browser->GetProfile() != profile_) {
          return true;
        }
        TabStripModel* model = browser->GetTabStripModel();
        if (!model || model->closing_all()) {
          return true;
        }
        for (int i = 0; i < model->count(); ++i) {
          content::WebContents* contents = model->GetWebContentsAt(i);
          if (!contents || !contents->IsCurrentlyAudible()) {
            continue;
          }
          // Pinned tabs show in every workspace, and a workspace on screen in
          // some window isn't hidden — neither counts as unseen audio.
          if (model->IsTabPinned(i) ||
              IsWorkspaceDisplayed(GetWorkspaceForContents(contents))) {
            continue;
          }
          found = true;
          return false;
        }
        return true;
      });
  return found;
}

void ZephyrusWorkspaceStore::SchedulePersist() {
  // Coalesce bursts of tab events (session restore, "close other tabs", drag
  // reordering) into a single serialize+write instead of one per event.
  persist_debounce_timer_.Start(FROM_HERE, base::Milliseconds(300), this,
                                &ZephyrusWorkspaceStore::Persist);
}

void ZephyrusWorkspaceStore::Persist() {
  // A Private Workspace persists nothing — it is ephemeral by definition, and
  // its OTR pref write would only land in an in-memory overlay that dies with
  // the session anyway. Skipping it also keeps the private session from ever
  // touching the workspace pref at all.
  if (!profile_ || profile_->IsOffTheRecord()) {
    return;
  }
  PrefService* prefs = profile_->GetPrefs();
  if (!prefs) {
    return;
  }
  prefs->SetString(kZephyrusWorkspacesPref, SerializeState());
}

std::string ZephyrusWorkspaceStore::SerializeState() const {
  base::DictValue dict;
  dict.Set("next", next_workspace_id_);
  dict.Set("current", saved_current_workspace_id_);
  base::ListValue list;
  for (const ZephyrusWorkspace& workspace : workspaces_) {
    base::DictValue item;
    item.Set("id", workspace.id);
    item.Set("name", base::UTF16ToUTF8(workspace.name));
    // SkColor is a uint32; store as a signed int (round-trips the bits).
    item.Set("color", static_cast<int>(workspace.color));
    item.Set("emoji", base::UTF16ToUTF8(workspace.emoji));
    // Where this workspace was last parked, as an ordinal among its OWN tabs
    // (a raw strip index wouldn't survive other workspaces' tabs moving).
    if (content::WebContents* active = GetActiveContents(workspace.id)) {
      int ordinal = 0;
      bool found = false;
      GlobalBrowserCollection::GetInstance()->ForEach(
          [&](BrowserWindowInterface* browser) {
            if (found || !browser || browser->GetProfile() != profile_) {
              return true;
            }
            TabStripModel* model = browser->GetTabStripModel();
            if (!model || model->closing_all()) {
              return true;
            }
            // Count within EACH window, not cumulatively across them: the
            // ordinal is resolved on restore by walking a single window's tabs,
            // so a running total would exceed anything that window can match
            // and silently fall back to the workspace's first tab.
            ordinal = 0;
            for (int i = 0; i < model->count(); ++i) {
              content::WebContents* contents = model->GetWebContentsAt(i);
              if (GetWorkspaceForContents(contents) != workspace.id) {
                continue;
              }
              if (contents == active) {
                found = true;
                return false;
              }
              ++ordinal;
            }
            return true;
          });
      if (found) {
        item.Set("active", ordinal);
      }
    }
    list.Append(base::Value(std::move(item)));
  }
  dict.Set("list", std::move(list));
  // Per-tab workspace ids, in tab-strip order, across EVERY window on this
  // profile. Writing only one window's tabs is what used to let a second
  // window overwrite the first's mapping.
  base::ListValue tabs;
  bool any_window = false;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (!browser || browser->GetProfile() != profile_) {
          return true;
        }
        TabStripModel* model = browser->GetTabStripModel();
        // A window in teardown is emptied one tab at a time; skip it so the
        // saved order isn't truncated to a shrinking tab set.
        if (!model || model->closing_all()) {
          return true;
        }
        any_window = true;
        for (int i = 0; i < model->count(); ++i) {
          tabs.Append(GetWorkspaceForContents(model->GetWebContentsAt(i)));
        }
        return true;
      });
  if (any_window) {
    dict.Set("tabs", std::move(tabs));
  }
  // Per-workspace visit index, keyed by workspace id as a string.
  base::DictValue visits;
  for (const auto& [workspace_id, order] : visit_order_) {
    base::ListValue urls;
    for (const std::string& spec : order) {
      urls.Append(spec);
    }
    visits.Set(base::NumberToString(workspace_id), std::move(urls));
  }
  dict.Set("visits", std::move(visits));
  return base::WriteJson(dict).value_or(std::string());
}

bool ZephyrusWorkspaceStore::LoadState() {
  if (!profile_) {
    return false;
  }
  PrefService* prefs = profile_->GetPrefs();
  if (!prefs) {
    return false;
  }
  const std::string& serialized = prefs->GetString(kZephyrusWorkspacesPref);
  std::optional<base::DictValue> value =
      base::JSONReader::ReadDict(serialized, base::JSON_PARSE_RFC);
  if (!value) {
    return false;
  }
  const base::DictValue& dict = *value;
  const base::ListValue* list = dict.FindList("list");
  if (!list || list->empty()) {
    return false;
  }
  workspaces_.clear();
  for (const base::Value& entry : *list) {
    if (!entry.is_dict()) {
      continue;
    }
    const base::DictValue& item = entry.GetDict();
    const int id = item.FindInt("id").value_or(0);
    if (id == 0) {
      continue;
    }
    const std::string* name = item.FindString("name");
    ZephyrusWorkspace workspace;
    workspace.id = id;
    workspace.name = name ? base::UTF8ToUTF16(*name) : u"Workspace";
    if (std::optional<int> color = item.FindInt("color");
        color && static_cast<SkColor>(*color) != SK_ColorTRANSPARENT) {
      workspace.color = static_cast<SkColor>(*color);
    } else {
      workspace.color =
          ZephyrusWorkspaceManager::DefaultColorForIndex(workspaces_.size());
    }
    if (const std::string* emoji = item.FindString("emoji")) {
      workspace.emoji = base::UTF8ToUTF16(*emoji);
    }
    if (std::optional<int> active = item.FindInt("active");
        active && *active >= 0) {
      pending_active_ordinal_[id] = *active;
    }
    workspaces_.push_back(std::move(workspace));
  }
  if (workspaces_.empty()) {
    return false;
  }
  // Validate against corrupt/hand-edited prefs: |next| must be beyond every
  // persisted id (or AllocateWorkspaceId would mint duplicates).
  int max_id = 0;
  for (const ZephyrusWorkspace& workspace : workspaces_) {
    max_id = std::max(max_id, workspace.id);
  }
  next_workspace_id_ =
      std::max(dict.FindInt("next").value_or(max_id + 1), max_id + 1);
  saved_current_workspace_id_ =
      dict.FindInt("current").value_or(workspaces_.front().id);

  // Rebuild the visit index. Both structures are filled together so lookup and
  // eviction order stay in step.
  visit_order_.clear();
  visit_lookup_.clear();
  if (const base::DictValue* visits = dict.FindDict("visits")) {
    for (const auto [key, url_list] : *visits) {
      int workspace_id = 0;
      if (!base::StringToInt(key, &workspace_id) || !url_list.is_list()) {
        continue;
      }
      for (const base::Value& visit : url_list.GetList()) {
        if (const std::string* spec = visit.GetIfString()) {
          visit_order_[workspace_id].push_back(*spec);
          visit_lookup_[workspace_id].insert(*spec);
        }
      }
    }
  }

  // Queue the saved per-tab order so restored tabs get re-tagged in sequence --
  // but only when the session is actually being restored, otherwise a fresh
  // startup tab would wrongly consume a queued id and land off-workspace.
  pending_restore_ids_.clear();
  const SessionStartupPref startup =
      SessionStartupPref::GetStartupPref(profile_);
  const bool will_restore = startup.type == SessionStartupPref::LAST ||
                            startup.type == SessionStartupPref::LAST_AND_URLS;
  if (will_restore) {
    if (const base::ListValue* tabs = dict.FindList("tabs")) {
      for (const base::Value& tab : *tabs) {
        pending_restore_ids_.push_back(tab.is_int() ? tab.GetInt() : 0);
      }
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// ZephyrusWorkspaceManager (per-window)

ZephyrusWorkspaceManager::ZephyrusWorkspaceManager(Browser* browser)
    : browser_(browser),
      tab_strip_model_(browser->tab_strip_model()),
      store_(ZephyrusWorkspaceStore::GetForProfile(browser->profile())) {
  // GetForProfile returns null only for a null profile; bail rather than
  // dereference, since every method below assumes the store exists.
  if (!store_) {
    return;
  }
  current_workspace_id_ = store_->default_workspace_id();
  // Assign any tabs that already exist (none during a normal restore, since
  // restored tabs arrive later via OnTabStripModelChanged).
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    content::WebContents* contents = tab_strip_model_->GetWebContentsAt(i);
    if (store_->GetWorkspaceForContents(contents) == 0) {
      store_->SetWorkspaceForContents(
          contents, WorkspaceIdForInsertedContents(/*opener=*/nullptr));
    }
  }
  // Any window can change the shared state, so mirror the store's
  // notifications out to this window's observers.
  store_changed_subscription_ = store_->RegisterChangedCallback(
      base::BindRepeating(&ZephyrusWorkspaceManager::NotifyLocalObservers,
                          base::Unretained(this)));
  tab_strip_model_->AddObserver(this);
}

ZephyrusWorkspaceManager::~ZephyrusWorkspaceManager() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
  // This window is gone, so its workspace may no longer be displayed anywhere.
  if (store_) {
    store_->RemoveWindow(this);
  }
}

const std::vector<ZephyrusWorkspace>& ZephyrusWorkspaceManager::workspaces()
    const {
  return store_->workspaces();
}

bool ZephyrusWorkspaceManager::HasBackgroundAudio() {
  return store_->HasBackgroundAudio();
}

const std::u16string& ZephyrusWorkspaceManager::current_workspace_name() const {
  for (const Workspace& workspace : store_->workspaces()) {
    if (workspace.id == current_workspace_id_) {
      return workspace.name;
    }
  }
  static const std::u16string kEmpty;
  return kEmpty;
}

void ZephyrusWorkspaceManager::SwitchToWorkspace(int workspace_id) {
  if (workspace_id == current_workspace_id_) {
    return;
  }
  bool exists = false;
  for (const Workspace& workspace : store_->workspaces()) {
    if (workspace.id == workspace_id) {
      exists = true;
      break;
    }
  }
  if (!exists) {
    return;
  }

  // Remember where we're leaving from before the current workspace changes.
  store_->SetActiveContents(current_workspace_id_,
                            tab_strip_model_->GetActiveWebContents());
  current_workspace_id_ = workspace_id;

  // Prefer the tab this workspace was last on, so switching back resumes where
  // the user left off rather than jumping to its first tab.
  int target_index = -1;
  if (content::WebContents* remembered = store_->GetActiveContents(workspace_id);
      remembered && GetWorkspaceForContents(remembered) == workspace_id) {
    const int index = tab_strip_model_->GetIndexOfWebContents(remembered);
    if (index != TabStripModel::kNoTab) {
      target_index = index;
    }
  }
  // Nothing remembered in this session — fall back to the position saved in
  // prefs, counted among this workspace's own tabs.
  if (target_index < 0) {
    if (const int ordinal = store_->TakePendingActiveOrdinal(workspace_id);
        ordinal >= 0) {
      int seen = 0;
      for (int i = 0; i < tab_strip_model_->count(); ++i) {
        if (GetWorkspaceForContents(tab_strip_model_->GetWebContentsAt(i)) !=
            workspace_id) {
          continue;
        }
        if (seen++ == ordinal) {
          target_index = i;
          break;
        }
      }
    }
  }
  // Otherwise just take the workspace's first tab.
  for (int i = 0; target_index < 0 && i < tab_strip_model_->count(); ++i) {
    if (GetWorkspaceForContents(tab_strip_model_->GetWebContentsAt(i)) ==
        workspace_id) {
      target_index = i;
    }
  }
  if (target_index >= 0) {
    tab_strip_model_->ActivateTabAt(target_index);
  } else {
    AddTabForWorkspace(workspace_id);
  }
  NotifyChanged();
  PersistState();
}

int ZephyrusWorkspaceManager::AddWorkspace() {
  const int id = store_->AllocateWorkspaceId();
  const size_t position = store_->workspaces().size();
  store_->workspaces().push_back(
      {id, u"Workspace " + base::NumberToString16(position + 1),
       DefaultColorForIndex(position), u""});
  current_workspace_id_ = id;
  // Force the tag: the outgoing workspace's tab is still active here, so opener
  // inheritance would otherwise file this tab back into that workspace.
  AddTabForWorkspace(id);
  NotifyChanged();
  PersistState();
  return id;
}

void ZephyrusWorkspaceManager::RenameWorkspace(int workspace_id,
                                               const std::u16string& name) {
  for (Workspace& workspace : store_->workspaces()) {
    if (workspace.id == workspace_id) {
      workspace.name = name;
      NotifyChanged();
      PersistState();
      return;
    }
  }
}

void ZephyrusWorkspaceManager::DeleteWorkspace(int workspace_id) {
  // Never delete the last remaining workspace.
  std::vector<Workspace>& all = store_->workspaces();
  if (all.size() <= 1) {
    return;
  }
  auto it = std::find_if(
      all.begin(), all.end(),
      [workspace_id](const Workspace& w) { return w.id == workspace_id; });
  if (it == all.end()) {
    return;
  }
  // Neighbor to switch to if we're deleting the workspace being displayed.
  const size_t index = static_cast<size_t>(it - all.begin());
  const int fallback_id = index > 0 ? all[index - 1].id : all[index + 1].id;

  // Deleting a workspace takes its tabs with it — they are NOT scattered into a
  // neighboring workspace, which would silently mix unrelated tabs into a
  // workspace the user never put them in. This matches Chrome's own tab groups,
  // where deleting a group closes its tabs.
  std::vector<content::WebContents*> doomed;
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    content::WebContents* contents = tab_strip_model_->GetWebContentsAt(i);
    if (contents && GetWorkspaceForContents(contents) == workspace_id) {
      doomed.push_back(contents);
    }
  }

  const bool deleting_current = current_workspace_id_ == workspace_id;
  all.erase(it);
  // Its visit index, saved-active-tab, and background bookkeeping go with it —
  // the PersistState() below then rewrites prefs without the dead workspace's
  // URL trail.
  store_->EraseWorkspaceState(workspace_id);
  if (deleting_current) {
    current_workspace_id_ = fallback_id;
  }

  // If the workspace owned every tab in the window, closing them all would take
  // the window down with it. Give the surviving workspace a tab first.
  if (static_cast<int>(doomed.size()) >= tab_strip_model_->count()) {
    AddTabForWorkspace(fallback_id);
  }

  // Close by looked-up index each time: indices shift as tabs are removed.
  for (content::WebContents* contents : doomed) {
    const int close_index = tab_strip_model_->GetIndexOfWebContents(contents);
    if (close_index != TabStripModel::kNoTab) {
      tab_strip_model_->CloseWebContentsAt(close_index, CLOSE_USER_GESTURE);
    }
  }

  if (deleting_current) {
    // Land on a tab that belongs to the workspace we switched to.
    for (int i = 0; i < tab_strip_model_->count(); ++i) {
      if (GetWorkspaceForContents(tab_strip_model_->GetWebContentsAt(i)) ==
          fallback_id) {
        tab_strip_model_->ActivateTabAt(i);
        break;
      }
    }
  }
  NotifyChanged();
  PersistState();
}

void ZephyrusWorkspaceManager::SetWorkspaceColor(int workspace_id,
                                                 SkColor color) {
  for (Workspace& workspace : store_->workspaces()) {
    if (workspace.id == workspace_id) {
      workspace.color = color;
      NotifyChanged();
      PersistState();
      return;
    }
  }
}

void ZephyrusWorkspaceManager::SetWorkspaceEmoji(int workspace_id,
                                                 const std::u16string& emoji) {
  for (Workspace& workspace : store_->workspaces()) {
    if (workspace.id == workspace_id) {
      workspace.emoji = emoji;
      NotifyChanged();
      PersistState();
      return;
    }
  }
}

const ZephyrusWorkspaceManager::Workspace*
ZephyrusWorkspaceManager::GetWorkspace(int workspace_id) const {
  for (const Workspace& workspace : store_->workspaces()) {
    if (workspace.id == workspace_id) {
      return &workspace;
    }
  }
  return nullptr;
}

void ZephyrusWorkspaceManager::MoveContentsToWorkspace(
    content::WebContents* contents,
    int workspace_id) {
  if (!contents || !GetWorkspace(workspace_id)) {
    return;
  }
  const int previous = GetWorkspaceForContents(contents);
  if (previous == workspace_id) {
    return;
  }
  store_->SetWorkspaceForContents(contents, workspace_id);
  // Moving a tab is filing, not navigating: stay in the workspace the user is
  // looking at. If the moved tab was the visible one, activate another tab that
  // still belongs here, or open a fresh one if that emptied the workspace.
  //
  // Deliberately NOT EnsureActiveTabInWorkspace(): its empty-workspace fallback
  // follows the active tab into its new workspace, which is right when a tab is
  // closed (so the window can still close on the last tab) but wrong here — it
  // would drag the user along with the tab they just filed away.
  if (contents == tab_strip_model_->GetActiveWebContents()) {
    int target_index = -1;
    for (int i = 0; i < tab_strip_model_->count(); ++i) {
      if (GetWorkspaceForContents(tab_strip_model_->GetWebContentsAt(i)) ==
          current_workspace_id_) {
        target_index = i;
        break;
      }
    }
    if (target_index >= 0) {
      tab_strip_model_->ActivateTabAt(target_index);
    } else {
      // The tab we just filed away is still active, so force the tag rather
      // than letting it be inherited straight back into the target workspace.
      AddTabForWorkspace(current_workspace_id_);
    }
  }
  NotifyChanged();
  PersistState();
}

void ZephyrusWorkspaceManager::SwitchToWorkspaceByIndex(size_t index) {
  if (index < store_->workspaces().size()) {
    SwitchToWorkspace(store_->workspaces()[index].id);
  }
}

void ZephyrusWorkspaceManager::SwitchToAdjacentWorkspace(int direction) {
  const size_t count = store_->workspaces().size();
  if (count <= 1) {
    return;
  }
  size_t current = 0;
  for (size_t i = 0; i < count; ++i) {
    if (store_->workspaces()[i].id == current_workspace_id_) {
      current = i;
      break;
    }
  }
  const size_t next = (current + count + (direction >= 0 ? 1 : -1)) % count;
  SwitchToWorkspace(store_->workspaces()[next].id);
}

int ZephyrusWorkspaceManager::GetWorkspaceForContents(
    content::WebContents* contents) const {
  return store_->GetWorkspaceForContents(contents);
}

bool ZephyrusWorkspaceManager::IsContentsPinned(
    content::WebContents* contents) const {
  const int index = tab_strip_model_->GetIndexOfWebContents(contents);
  return index != TabStripModel::kNoTab && tab_strip_model_->IsTabPinned(index);
}

bool ZephyrusWorkspaceManager::IsContentsInCurrentWorkspace(
    content::WebContents* contents) const {
  // A pinned tab is a global favourite — it belongs to every workspace, so it
  // stays visible (and doesn't count as "foreign") wherever the user is.
  if (IsContentsPinned(contents)) {
    return true;
  }
  const int workspace = GetWorkspaceForContents(contents);
  if (workspace == current_workspace_id_) {
    return true;
  }
  // A tab with no workspace, or one pointing at a workspace that no longer
  // exists, matches nothing and would be filtered out of every sidebar —
  // present in the strip but invisible and unreachable. Show it here so it
  // can't be lost in the gap before AdoptUntrackedTabs() re-tags it.
  return !store_->Get(workspace);
}

void ZephyrusWorkspaceManager::AdoptUntrackedTabs() {
  bool adopted = false;
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    content::WebContents* contents = tab_strip_model_->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    // Adopt tabs with no workspace AND tabs pointing at a workspace that no
    // longer exists — e.g. a tab that survived its workspace's deletion because
    // a beforeunload prompt deferred the close and the user cancelled. Either
    // way the tab matches no workspace, so without this it is present in the
    // strip yet invisible in every sidebar.
    if (!store_->Get(GetWorkspaceForContents(contents))) {
      store_->SetWorkspaceForContents(contents, current_workspace_id_);
      adopted = true;
    }
  }
  if (adopted) {
    NotifyChanged();
    SchedulePersistState();
  }
}

void ZephyrusWorkspaceManager::SelectAdjacentTabInWorkspace(int direction) {
  const int count = tab_strip_model_->count();
  const int active = tab_strip_model_->active_index();
  if (count == 0 || active < 0) {
    return;
  }
  for (int step = 1; step <= count; ++step) {
    const int index = ((active + direction * step) % count + count) % count;
    // Cycles through this workspace's tabs plus the global pinned tabs, which
    // is exactly what the sidebar shows.
    if (IsContentsInCurrentWorkspace(
            tab_strip_model_->GetWebContentsAt(index))) {
      tab_strip_model_->ActivateTabAt(index);
      return;
    }
  }
  // No other tab in this workspace; stay put.
}

void ZephyrusWorkspaceManager::EnsureActiveTabInWorkspace() {
  const int count = tab_strip_model_->count();
  if (count == 0) {
    return;
  }
  content::WebContents* active = tab_strip_model_->GetActiveWebContents();
  // Pinned tabs count as being in this workspace, so sitting on a global pin
  // never bounces the user to a different tab.
  if (!active || IsContentsInCurrentWorkspace(active)) {
    return;
  }
  // The active tab is in another workspace; re-select a current-workspace tab.
  for (int i = 0; i < count; ++i) {
    if (GetWorkspaceForContents(tab_strip_model_->GetWebContentsAt(i)) ==
        current_workspace_id_) {
      tab_strip_model_->ActivateTabAt(i);
      return;
    }
  }
  // The current workspace has no tabs left. Stay in it and give it a fresh tab
  // rather than following the active tab into another workspace — emptying a
  // workspace by closing its last tab should leave you in that (now empty)
  // workspace, exactly like moving its last tab away does.
  //
  // Note this is reached only when other tabs still exist (count == 0 returned
  // above), so it cannot interfere with closing the final tab closing the
  // window. Posted asynchronously because this runs inside a tab-strip
  // observer callback, where mutating the strip re-entrantly is unsafe.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&ZephyrusWorkspaceManager::OpenTabForEmptyCurrentWorkspace,
                     weak_factory_.GetWeakPtr()));
}

void ZephyrusWorkspaceManager::OpenTabForEmptyCurrentWorkspace() {
  // Re-check: the strip may have changed again before this task ran.
  if (tab_strip_model_->closing_all()) {
    return;
  }
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    if (GetWorkspaceForContents(tab_strip_model_->GetWebContentsAt(i)) ==
        current_workspace_id_) {
      return;  // It got tabs back; nothing to do.
    }
  }
  AddTabForWorkspace(current_workspace_id_);
}

base::CallbackListSubscription
ZephyrusWorkspaceManager::RegisterChangedCallback(
    base::RepeatingClosure callback) {
  return changed_callbacks_.Add(std::move(callback));
}

void ZephyrusWorkspaceManager::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  switch (change.type()) {
    case TabStripModelChange::kInserted:
      for (const auto& contents_with_index : change.GetInsert()->contents) {
        // Resolve the opener so the new tab inherits its workspace. Look the
        // index up fresh rather than using `contents_with_index.index`: those
        // are the indices at insertion time and must not be used for queries
        // while still processing the batch.
        content::WebContents* opener = nullptr;
        const int index = tab_strip_model_->GetIndexOfWebContents(
            contents_with_index.contents);
        if (index != TabStripModel::kNoTab) {
          if (tabs::TabInterface* opener_tab =
                  tab_strip_model_->GetOpenerOfTabAt(index)) {
            opener = opener_tab->GetContents();
          }
        }
        store_->SetWorkspaceForContents(
            contents_with_index.contents,
            WorkspaceIdForInsertedContents(opener));
      }
      // Catch anything that slipped in without a tag (e.g. a tab adopted from
      // another window) so it can't become invisible in every sidebar.
      AdoptUntrackedTabs();
      SchedulePersistState();
      NotifyChanged();
      break;
    case TabStripModelChange::kRemoved:
      for (const auto& removed_tab : change.GetRemove()->contents) {
        // A tab being dragged into another window is removed here and inserted
        // there. Keep its workspace tag in that case, so dragging a tab between
        // windows doesn't silently re-file it into whatever workspace the
        // destination window happens to be showing.
        if (removed_tab.tab_detach_reason ==
            tabs::TabInterface::DetachReason::kInsertIntoOtherWindow) {
          continue;
        }
        store_->EraseContents(removed_tab.contents);
      }
      // A removal can hand activation to a tab from another workspace (e.g. an
      // async close after a beforeunload prompt, where the command handler's
      // EnsureActiveTabInWorkspace ran too early). Re-check here, at the moment
      // the strip actually changed — but never during full teardown.
      if (!tab_strip_model_->closing_all()) {
        EnsureActiveTabInWorkspace();
      }
      SchedulePersistState();
      NotifyChanged();
      break;
    case TabStripModelChange::kReplaced: {
      const TabStripModelChange::Replace* replace = change.GetReplace();
      const int workspace =
          store_->GetWorkspaceForContents(replace->old_contents);
      if (workspace != 0) {
        store_->EraseContents(replace->old_contents);
        store_->SetWorkspaceForContents(replace->new_contents, workspace);
      }
      SchedulePersistState();
      break;
    }
    case TabStripModelChange::kMoved:
      // Tab order changed; re-persist so the saved per-tab order stays in sync.
      SchedulePersistState();
      break;
    case TabStripModelChange::kSelectionOnly:
      break;
  }
  // Keep each workspace's remembered tab current as the user browses, so
  // switching away and back lands on the tab they were actually using.
  if (selection.active_tab_changed()) {
    if (content::WebContents* active =
            tab_strip_model_->GetActiveWebContents()) {
      const int workspace = GetWorkspaceForContents(active);
      if (workspace != 0) {
        store_->SetActiveContents(workspace, active);
      }
    }
  }
}

void ZephyrusWorkspaceManager::OnTabChangedAt(tabs::TabInterface* tab,
                                              int index,
                                              TabChangeType change_type) {
  // Fires as a tab commits a navigation, which is where the visit index learns
  // which workspace a URL was opened in. kLoadingOnly / kAttentionOnly /
  // title-only changes fire constantly during a page load; each would allocate
  // a URL spec and probe the index for nothing, so only full changes proceed.
  if (change_type != TabChangeType::kAll) {
    return;
  }
  if (index < 0 || index >= tab_strip_model_->count()) {
    return;
  }
  content::WebContents* contents = tab_strip_model_->GetWebContentsAt(index);
  if (!contents) {
    return;
  }
  // Never index a tab from another profile. With in-window Private Workspace
  // tabs (PW-6), an off-the-record tab can sit in a regular window's strip —
  // and this store persists to the REGULAR profile's prefs, so recording that
  // tab would write private URLs to disk. The private window's own manager
  // records into the OTR store (in-memory, dies with the session), so scoping
  // still works inside Private Workspace itself.
  if (contents->GetBrowserContext() != browser_->profile()) {
    return;
  }
  store_->RecordVisit(GetWorkspaceForContents(contents),
                      contents->GetLastCommittedURL());
}

void ZephyrusWorkspaceManager::NotifyChanged() {
  // Every path that changes this window's workspace ends here, so this is the
  // single place the store learns what each window is displaying.
  store_->SetWindowWorkspace(this, current_workspace_id_);
  // Fan out through the store rather than notifying only this window. The
  // workspace list and the tab->workspace map are shared, so a rename, delete
  // or move made here must refresh EVERY window's sidebar and pill — otherwise
  // the other window keeps rendering stale state until something else nudges
  // it. The store calls NotifyLocalObservers() on each manager, which does not
  // re-enter here, so this terminates.
  store_->NotifyChanged();
}

void ZephyrusWorkspaceManager::NotifyLocalObservers() {
  changed_callbacks_.Notify();
}

void ZephyrusWorkspaceManager::AddTabForWorkspace(int workspace_id) {
  // Zephyrus has no new-tab page, and a window with no tabs is now a valid
  // state that shows the empty backdrop. So when the window is *already*
  // empty — at startup, or after the user closed the last tab — never
  // manufacture a blank tab just to keep a workspace populated; that is
  // exactly the page we are trying to abolish. Mid-session behaviour, where
  // other tabs exist, is unchanged.
  if (tab_strip_model_->count() == 0) {
    return;
  }

  pending_forced_workspace_id_ = workspace_id;
  tab_strip_model_->delegate()->AddTabAt(GURL(), /*index=*/-1,
                                         /*foreground=*/true);
  // Cleared by the insertion, but reset defensively in case no tab arrived.
  pending_forced_workspace_id_.reset();
}

int ZephyrusWorkspaceManager::WorkspaceIdForInsertedContents(
    content::WebContents* opener) {
  // We opened this tab FOR a specific workspace, so that wins over everything
  // else. In particular it must beat opener inheritance: a tab created for an
  // empty workspace is opened while the outgoing workspace's tab is still the
  // active one, and Chromium records that tab as the opener.
  if (pending_forced_workspace_id_.has_value()) {
    const int forced = *pending_forced_workspace_id_;
    pending_forced_workspace_id_.reset();
    if (GetWorkspace(forced)) {
      return forced;
    }
  }
  // While a saved session is being restored, re-tag tabs in their saved order:
  // exactly one queued id is consumed per inserted tab to stay aligned.
  //
  // Gate on SessionRestore::IsRestoring, not just a non-empty queue. Restore
  // can leave leftover ids (fewer tabs restored than saved — across windows,
  // or with restore-on-demand), and without this gate the first tab the USER
  // opens afterwards would consume a stale id and land in the wrong workspace:
  // opened in dev, invisible in dev, showing up in another workspace. Once
  // restore is done, drain the queue so no later tab can inherit from it.
  if (store_->has_pending_restore()) {
    if (browser_ && SessionRestore::IsRestoring(browser_->profile())) {
      if (const int id = store_->TakePendingRestoreId(); id != 0) {
        return id;
      }
      // Saved id no longer maps to a workspace; fall through below.
    } else {
      store_->ClearPendingRestore();
    }
  }
  // A tab opened by another tab (window.open, target=_blank, "open link in new
  // tab") belongs with its opener. Without this, a page running in a background
  // workspace would drop its new tab into whichever workspace is on screen.
  if (opener) {
    const int opener_workspace = GetWorkspaceForContents(opener);
    if (opener_workspace != 0) {
      return opener_workspace;
    }
  }
  return current_workspace_id_;
}

// Persistence lives on the profile-wide store so a single writer owns the pref
// with a complete view of every window's tabs.
void ZephyrusWorkspaceManager::SchedulePersistState() {
  store_->SchedulePersist();
}

void ZephyrusWorkspaceManager::PersistState() {
  store_->Persist();
}
