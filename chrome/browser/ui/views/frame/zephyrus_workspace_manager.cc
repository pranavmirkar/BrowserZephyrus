// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"

#include "chrome/browser/ui/views/frame/zephyrus_workspace_image.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_partition.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_icons.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/webui/cr_components/most_visited/zephyrus_most_visited_filter.h"
#include "chrome/common/pref_names.h"

#include <algorithm>

#include <array>

#include "base/functional/bind.h"
#include "base/containers/span.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
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
#include "chrome/browser/history/history_service_factory.h"
#include "components/keyed_service/core/service_access_type.h"
#include "crypto/hash.h"
#include "chrome/browser/resource_coordinator/lifecycle_unit_state.mojom.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "content/public/common/referrer.h"
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

// Workspaces are no longer identified by HUE.
//
// The eight-colour palette that used to live here (blue, purple, pink, orange,
// green, amber, cyan, red) cannot survive a one-accent language: eight accents
// is the opposite of one, and the whole value of the single red is how rarely
// it appears. Identity moved to a DOT INDEX instead -- workspace n is drawn as
// n dots on the same grid the rest of the interface is built on -- which says
// the same thing using position and count rather than colour, and unlike a
// colour table it keeps working past the eighth workspace.
//
// The stored per-workspace colour is kept in the model rather than deleted:
// existing profiles have values in their prefs, and dropping the field would
// discard them on first launch. It now always resolves to the ink, so anything
// still painting with it stays monochrome instead of drawing a stale hue.
//
// See ZephyrusWorkspaceManager::DotsForIndex for the count.
}  // namespace

// static
SkColor ZephyrusWorkspaceManager::DefaultColorForIndex(size_t index) {
  // One ink for every workspace. The index is expressed as dots, not hue.
  return zephyrus::Ink();
}

// static
int ZephyrusWorkspaceManager::DotsForIndex(size_t index) {
  // 1-based, and capped so a very long workspace list does not draw a row of
  // dots wider than the pill holding it. Past the cap the name carries the
  // distinction, which it has to anyway at that count.
  constexpr int kMaxDots = 6;
  return static_cast<int>(index % kMaxDots) + 1;
}

// ---------------------------------------------------------------------------
// ZephyrusWorkspaceStore (profile-wide)

namespace {
// Key for attaching the store to the Profile. Its address is the identity.
constexpr char kZephyrusWorkspaceStoreKey[] = "zephyrus_workspace_store";

// The profile prefs that together are how a workspace looks: the theme seed and
// its variant, grayscale, light/dark, and the New Tab Page wallpaper. Exactly
// what Customize Chrome edits, minus extension themes (installed, not chosen)
// and an uploaded photo, whose FILE is one per profile.
constexpr const char* kAppearancePrefs[] = {
    prefs::kUserColor,
    prefs::kBrowserColorVariant,
    prefs::kGrayscaleThemeEnabled,
    prefs::kBrowserColorScheme,
    prefs::kNtpCustomBackgroundDict,
    prefs::kNtpCustomBackgroundLocalToDevice,
};

bool IsAppearancePref(std::string_view path) {
  for (const char* candidate : kAppearancePrefs) {
    if (path == candidate) {
      return true;
    }
  }
  return false;
}

ZephyrusWorkspaceLook LookFromDict(const base::DictValue& look) {
  ZephyrusWorkspaceLook result;
  if (std::optional<int> color = look.FindInt(prefs::kUserColor);
      color && static_cast<SkColor>(*color) != SK_ColorTRANSPARENT) {
    result.seed = static_cast<SkColor>(*color);
  }
  // Unset means Zephyrus's default, which is dark; kSystem (0) is coerced to
  // dark by ThemeService::GetBrowserColorScheme for the same reason.
  const std::optional<int> scheme = look.FindInt(prefs::kBrowserColorScheme);
  result.dark = !(scheme && *scheme == 1);
  result.grayscale = look.FindBool(prefs::kGrayscaleThemeEnabled).value_or(false);
  return result;
}

// The New Tab Page's most-visited tiles come from the profile's history, which
// every workspace shares -- so a fresh workspace's NTP greeted you with the
// sites you use in all the others. A history-derived tile is shown only if the
// site was opened in the tile's own workspace. A workspace on the shared
// cookie jar (the first one, and any created without separate sign-ins) keeps
// every tile, as before: it is the shared space by design.
bool KeepTileInWorkspace(content::WebContents* contents, const GURL& url) {
  if (!contents) {
    return true;
  }
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  if (!profile || profile->IsOffTheRecord()) {
    return true;
  }
  // Looked up, never created: a profile with no workspace store has no
  // workspaces to scope anything to.
  auto* store = static_cast<ZephyrusWorkspaceStore*>(
      profile->GetUserData(kZephyrusWorkspaceStoreKey));
  if (!store) {
    return true;
  }
  const int workspace = store->GetWorkspaceForContents(contents);
  if (workspace == 0 || store->PartitionNameForWorkspace(workspace).empty()) {
    return true;
  }
  return store->WasVisitedInWorkspace(workspace, url) ||
         store->WasVisitedInWorkspace(workspace, url.GetWithEmptyPath());
}
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
  // One filter for the process; it finds the right store per tab.
  static bool tile_filter_registered = false;
  if (!tile_filter_registered) {
    tile_filter_registered = true;
    zephyrus::SetMostVisitedTileFilter(
        base::BindRepeating(&KeepTileInWorkspace));
  }
  // A Private Workspace store starts blank, every session. Its profile is
  // off-the-record, whose PrefService is an overlay that reads THROUGH to the
  // regular profile — so calling LoadState() here would pull the regular
  // profile's workspaces, its pending tab tags, and its visit index into the
  // private session. That both hid the private window's own tab (it got tagged
  // with an inherited workspace id that wasn't the window's current one) and
  // read regular browsing data into a session that is supposed to know none of
  // it. Skip the load: a single fresh default workspace, nothing inherited.
  const bool is_private = profile_ && profile_->IsOffTheRecord();
  // A fresh salt, replaced by the stored one when there is state to load.
  visit_salt_ = base::HexEncode(base::RandBytesAsVector(16));
  // Private Workspace keeps no history database to follow, and its index dies
  // with the session anyway.
  if (profile_ && !is_private) {
    if (history::HistoryService* history = HistoryServiceFactory::GetForProfile(
            profile_, ServiceAccessType::EXPLICIT_ACCESS)) {
      history_observation_.Observe(history);
    }
  }
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

int ZephyrusWorkspaceStore::PeekPendingRestoreId() const {
  return pending_restore_ids_.empty() ? 0 : pending_restore_ids_.front();
}

std::string ZephyrusWorkspaceStore::PartitionNameForWorkspace(
    int workspace_id) const {
  if (workspace_id == 0) {
    return std::string();
  }
  for (const ZephyrusWorkspace& workspace : workspaces_) {
    if (workspace.id == workspace_id) {
      return workspace.partition_name;
    }
  }
  // The workspace is gone. The default partition is the only safe answer, and
  // the tab is about to be re-filed anyway.
  return std::string();
}

std::string ZephyrusWorkspaceStore::PartitionNameForPendingRestore() const {
  return PartitionNameForWorkspace(PeekPendingRestoreId());
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

gfx::ImageSkia ZephyrusWorkspaceStore::GetImage(const std::string& name,
                                                int size_dip) {
  if (name.empty() || size_dip <= 0) {
    return gfx::ImageSkia();
  }
  // Cell size changed (DPI, or a design change): everything cached is the wrong
  // size, so start over rather than draw one stale entry among fresh ones.
  if (size_dip != image_size_dip_) {
    image_size_dip_ = size_dip;
    image_cache_.clear();
    // Loads already in flight will land at the OLD size. Let them: they will be
    // stored, then immediately superseded by a re-request at the new size. The
    // alternative -- tracking a size per in-flight load -- costs more than
    // redrawing one icon slightly wrong for a few milliseconds after a monitor
    // change.
  }
  if (auto it = image_cache_.find(name); it != image_cache_.end()) {
    return it->second;
  }
  // Already being read. Returning empty here is not a failure: the change
  // notification when it lands is what brings it on screen.
  if (image_pending_.contains(name)) {
    return gfx::ImageSkia();
  }
  image_pending_.insert(name);
  zephyrus::LoadWorkspaceImage(
      profile_, name, size_dip,
      base::BindOnce(&ZephyrusWorkspaceStore::OnImageLoaded,
                     weak_factory_.GetWeakPtr(), name));
  return gfx::ImageSkia();
}

void ZephyrusWorkspaceStore::OnImageLoaded(const std::string& name,
                                           const gfx::ImageSkia& image) {
  image_pending_.erase(name);
  // Cached EVEN WHEN EMPTY. A workspace whose file has been deleted has to be
  // remembered as "nothing there", or every repaint starts another read of a
  // file that is not coming back.
  image_cache_[name] = image;
  if (!image.isNull()) {
    NotifyChanged();
  }
}

void ZephyrusWorkspaceStore::ForgetImage(const std::string& name) {
  image_cache_.erase(name);
  image_pending_.erase(name);
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
  // The workspace to reopen on next launch. This was loaded from prefs and
  // never written again, so every restart landed on the first workspace --
  // and then EnsureActiveTabInWorkspace pulled the restored active tab away
  // from wherever the user had actually been.
  if (Get(workspace_id)) {
    saved_current_workspace_id_ = workspace_id;
  }
  RefreshBackgroundTimestamps();

  // OFF the switch path.
  //
  // This called ApplyResourcePolicy() inline, and that walks every window x
  // every tab, muting and discarding as it goes. Because SetWindowWorkspace()
  // runs BEFORE observers are notified, the whole walk sat between the click
  // and the workspace indicator updating -- the tab itself had already
  // activated, so switching looked fast while the highlight visibly lagged
  // behind it.
  //
  // A zero-delay one-shot runs it on the very next turn of the message loop:
  // still effectively immediate for muting (the original reason it was inline),
  // but after the UI has painted. OneShotTimer cancels on destruction, so this
  // is safe without a weak pointer.
  resource_kick_timer_.Start(FROM_HERE, base::TimeDelta(), this,
                             &ZephyrusWorkspaceStore::ApplyResourcePolicy);
  if (!resource_timer_.IsRunning()) {
    resource_timer_.Start(FROM_HERE, base::Minutes(1), this,
                          &ZephyrusWorkspaceStore::ApplyResourcePolicy);
  }
}

std::string ZephyrusWorkspaceStore::PartitionNameForWindow(
    const void* window) const {
  const auto it = window_current_workspace_.find(window);
  if (it == window_current_workspace_.end()) {
    return std::string();
  }
  for (const ZephyrusWorkspace& workspace : workspaces_) {
    if (workspace.id == it->second) {
      return workspace.partition_name;
    }
  }
  return std::string();
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
  //
  // Discarding is lossy: the tab RELOADS when you come back, which is a full
  // network round trip and page load standing between a click and the content.
  //
  // This was 5 minutes, and that is shorter than the interval at which people
  // actually rotate between workspaces -- so the common case was not "reclaim
  // memory from something abandoned", it was "reload the page every single time
  // the user switches back". The original comment already predicted the
  // failure ("that would make switching back feel slow"); the number was just
  // set well inside the range where it happens.
  //
  // Two hours is genuinely "you have not touched this since this morning".
  // A workspace you use during a work session now stays resident.
  //
  // The RIGHT trigger is memory pressure rather than elapsed time -- a
  // workspace idle for hours on a machine with free memory costs nothing to
  // keep. That needs a pressure signal plumbed in here; until then a long grace
  // is the conservative approximation.
  constexpr base::TimeDelta kDiscardGrace = base::Hours(2);
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
          // The active tab is by definition in use, and a workspace on screen
          // in some window is not hidden. Pinning no longer exempts anything:
          // a pinned tab now belongs to one workspace, so when that workspace
          // is away the tab is as background as its neighbours.
          const int workspace = GetWorkspaceForContents(contents);
          if (i == model->active_index() || IsWorkspaceDisplayed(workspace)) {
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
// The canonical form of a URL for the visit index, before hashing.
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

std::string ZephyrusWorkspaceStore::VisitKey(const GURL& url) const {
  const std::string spec = ZephyrusVisitKey(url);
  if (spec.empty()) {
    return std::string();
  }
  // 128 bits of a salted SHA-256: collision-free for 150 entries a workspace
  // by a wide margin, and half the size of the full digest in the pref.
  const std::array<uint8_t, crypto::hash::kSha256Size> digest =
      crypto::hash::Sha256(base::StrCat({visit_salt_, "\n", spec}));
  return base::HexEncode(base::span(digest).first<16>());
}

void ZephyrusWorkspaceStore::RecordVisit(int workspace_id, const GURL& url) {
  // Cap per workspace: this rides in a pref, and an unbounded index would grow
  // the Preferences file without limit. Doubled when origins joined the index,
  // so the page history it held before keeps the same depth.
  constexpr size_t kMaxVisitsPerWorkspace = 300;
  if (workspace_id == 0) {
    return;
  }
  bool changed = false;
  // The page itself, for omnibox scoping, and its ORIGIN, for the New Tab
  // Page: a most-visited tile names a site's front page, which is rarely the
  // exact page that was read.
  for (const GURL& visited : {url, url.GetWithEmptyPath()}) {
    // Only ordinary web pages are worth scoping. chrome:// pages, the NTP and
    // blank entries aren't things the user "visited in a workspace".
    std::string spec = VisitKey(visited);
    if (spec.empty()) {
      continue;
    }
    std::set<std::string>& lookup = visit_lookup_[workspace_id];
    if (!lookup.insert(spec).second) {
      continue;  // Already known; leave its position alone.
    }
    std::deque<std::string>& order = visit_order_[workspace_id];
    order.push_back(std::move(spec));
    while (order.size() > kMaxVisitsPerWorkspace) {
      lookup.erase(order.front());
      order.pop_front();
    }
    changed = true;
  }
  if (changed) {
    SchedulePersist();
  }
}

bool ZephyrusWorkspaceStore::WasVisitedInWorkspace(int workspace_id,
                                                   const GURL& url) const {
  const auto it = visit_lookup_.find(workspace_id);
  // Same canonical form as RecordVisit, or a URL with a #fragment would never
  // match its recorded fragment-less twin.
  return it != visit_lookup_.end() && it->second.count(VisitKey(url)) > 0;
}

void ZephyrusWorkspaceStore::OnHistoryDeletions(
    history::HistoryService* history_service,
    const history::DeletionInfo& deletion_info) {
  if (visit_lookup_.empty()) {
    return;
  }
  if (deletion_info.IsAllHistory()) {
    visit_order_.clear();
    visit_lookup_.clear();
    SchedulePersist();
    return;
  }
  // Specific URLs -- chosen by the user, or aged out by history's own
  // expiry. deleted_rows() lists the URLs with no visits left, which is the
  // set that must no longer be known here.
  bool changed = false;
  for (const history::URLRow& row : deletion_info.deleted_rows()) {
    const std::string key = VisitKey(row.url());
    if (key.empty()) {
      continue;
    }
    for (auto& [workspace_id, lookup] : visit_lookup_) {
      if (lookup.erase(key)) {
        std::deque<std::string>& order = visit_order_[workspace_id];
        std::erase(order, key);
        changed = true;
      }
    }
  }
  if (changed) {
    SchedulePersist();
  }
}

void ZephyrusWorkspaceStore::HistoryServiceBeingDeleted(
    history::HistoryService* history_service) {
  // Keyed services shut down before profile user data is destroyed, so this
  // store outlives the service it watches.
  history_observation_.Reset();
}

void ZephyrusWorkspaceStore::EraseWorkspaceState(int workspace_id) {
  appearance_.erase(workspace_id);
  if (mirrored_workspace_id_ == workspace_id) {
    // Nothing to save the live theme into any more; the next workspace shown
    // simply applies its own.
    mirrored_workspace_id_ = 0;
  }
  workspace_active_.erase(workspace_id);
  pending_active_ordinal_.erase(workspace_id);
  workspace_backgrounded_at_.erase(workspace_id);
  visit_order_.erase(workspace_id);
  visit_lookup_.erase(workspace_id);
}

base::DictValue ZephyrusWorkspaceStore::CaptureAppearance() const {
  base::DictValue look;
  PrefService* prefs = profile_ ? profile_->GetPrefs() : nullptr;
  if (!prefs) {
    return look;
  }
  for (const char* path : kAppearancePrefs) {
    const PrefService::Preference* pref = prefs->FindPreference(path);
    if (!pref) {
      continue;
    }
    // None records "at its default", so applying it CLEARS the pref rather than
    // pinning today's default value into the workspace forever.
    look.Set(path, pref->IsDefaultValue() ? base::Value()
                                          : pref->GetValue()->Clone());
  }
  return look;
}

void ZephyrusWorkspaceStore::ApplyAppearance(const base::DictValue& look) {
  PrefService* prefs = profile_ ? profile_->GetPrefs() : nullptr;
  if (!prefs) {
    return;
  }
  for (const char* path : kAppearancePrefs) {
    const PrefService::Preference* pref = prefs->FindPreference(path);
    // A policy owns a managed pref; writing it would do nothing but warn.
    if (!pref || pref->IsManaged()) {
      continue;
    }
    const base::Value* want = look.Find(path);
    if (!want || want->is_none()) {
      if (!pref->IsDefaultValue()) {
        prefs->ClearPref(path);
      }
      continue;
    }
    // The record comes from the Preferences file, which the user can edit, and
    // PrefService treats a value of the wrong type as a fatal error rather than
    // a bad input. Checked here, before it can reach it.
    if (want->type() != pref->GetType()) {
      continue;
    }
    if (*pref->GetValue() != *want) {
      prefs->Set(path, want->Clone());
    }
  }
}

void ZephyrusWorkspaceStore::CaptureMirrored() {
  if (!profile_ || profile_->IsOffTheRecord() ||
      !Get(mirrored_workspace_id_)) {
    return;
  }
  appearance_[mirrored_workspace_id_] = CaptureAppearance();
}

bool ZephyrusWorkspaceStore::MirrorAppearance(int workspace_id) {
  // Private Workspace has no theme of its own to keep: it is always the
  // incognito palette, and it must not write the regular profile's prefs.
  if (!profile_ || profile_->IsOffTheRecord() || !Get(workspace_id) ||
      workspace_id == mirrored_workspace_id_) {
    return false;
  }
  // Save what the outgoing workspace ended up looking like -- including any
  // change made in Customize Chrome while it was on screen -- before the prefs
  // are rewritten for the incoming one.
  CaptureMirrored();
  mirrored_workspace_id_ = workspace_id;
  const auto it = appearance_.find(workspace_id);
  if (it == appearance_.end()) {
    // First time on screen since per-workspace appearance existed: it keeps the
    // look it has always had, and from here on it has its own.
    appearance_[workspace_id] = CaptureAppearance();
  } else {
    ApplyAppearance(it->second);
  }
  SchedulePersist();
  return true;
}

std::optional<ZephyrusWorkspaceLook> ZephyrusWorkspaceStore::GetLook(
    int workspace_id) {
  if (workspace_id == mirrored_workspace_id_) {
    CaptureMirrored();  // The live prefs are the current truth for this one.
  }
  const auto it = appearance_.find(workspace_id);
  if (it == appearance_.end()) {
    return std::nullopt;
  }
  return LookFromDict(it->second);
}

ZephyrusWorkspaceLook ZephyrusWorkspaceStore::LookForEditing(
    int workspace_id) {
  if (std::optional<ZephyrusWorkspaceLook> look = GetLook(workspace_id)) {
    return *look;
  }
  return LookFromDict(CaptureAppearance());
}

void ZephyrusWorkspaceStore::SetLook(int workspace_id,
                                     std::optional<SkColor> seed,
                                     bool dark,
                                     bool clear_wallpaper) {
  if (!profile_ || profile_->IsOffTheRecord() || !Get(workspace_id)) {
    return;
  }
  if (workspace_id == mirrored_workspace_id_) {
    CaptureMirrored();
  }
  const auto it = appearance_.find(workspace_id);
  base::DictValue look =
      it != appearance_.end() ? it->second.Clone() : CaptureAppearance();
  // The same writes Customize Chrome makes: a seed (or none, for the default
  // palette), the default variant, colour rather than grayscale.
  look.Set(prefs::kUserColor, seed ? base::Value(static_cast<int>(*seed))
                                   : base::Value());
  look.Set(prefs::kBrowserColorVariant, base::Value());
  look.Set(prefs::kGrayscaleThemeEnabled, base::Value());
  look.Set(prefs::kBrowserColorScheme,
           base::Value(dark ? 2 /*kDark*/ : 1 /*kLight*/));
  if (clear_wallpaper) {
    look.Set(prefs::kNtpCustomBackgroundDict, base::Value());
    look.Set(prefs::kNtpCustomBackgroundLocalToDevice, base::Value());
  }
  if (workspace_id == mirrored_workspace_id_) {
    ApplyAppearance(look);
  }
  appearance_[workspace_id] = std::move(look);
  SchedulePersist();
}

int ZephyrusWorkspaceStore::WorkspaceForNewWindow() {
  int workspace = 0;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (!browser || browser->GetProfile() != profile_) {
          return true;
        }
        const auto it = window_current_workspace_.find(
            browser->GetBrowserForMigrationOnly());
        if (it == window_current_workspace_.end()) {
          return true;  // Itself, most likely: not registered yet.
        }
        workspace = it->second;
        return false;  // Most recently activated wins.
      },
      BrowserCollection::Order::kActivation);
  return Get(workspace) ? workspace : default_workspace_id();
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
          // A workspace on screen in some window isn't hidden, so its audio is
          // not unseen. Pinned tabs get no exemption now that pinning is scoped
          // to one workspace.
          if (IsWorkspaceDisplayed(GetWorkspaceForContents(contents))) {
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
  // The live theme belongs to the mirrored workspace; save it with everything
  // else so a Customize Chrome change survives a crash or a quit.
  CaptureMirrored();
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
    item.Set("image", workspace.image);
    item.Set("partition", workspace.partition_name);
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
  dict.Set("visit_salt", visit_salt_);
  base::DictValue looks;
  for (const auto& [workspace_id, look] : appearance_) {
    looks.Set(base::NumberToString(workspace_id), look.Clone());
  }
  dict.Set("appearance", std::move(looks));
  dict.Set("mirrored", mirrored_workspace_id_);
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
    // Prefs are a user-writable file, and this value is about to be joined onto
    // a directory path. Anything that is not the exact shape we generate is
    // dropped here rather than at the point of use, so a hand-edited pref can
    // never reach the filesystem code at all.
    if (const std::string* image = item.FindString("image");
        image && zephyrus::IsValidWorkspaceImageName(*image)) {
      workspace.image = *image;
    }
    // Absent on anything written before isolation, which is exactly right:
    // those workspaces keep sharing the default cookie jar.
    //
    // VALIDATED, like the image name above: this string comes from a
    // user-writable prefs file and ends up naming a directory on disk.
    if (const std::string* partition = item.FindString("partition");
        partition && zephyrus::IsValidWorkspacePartitionName(*partition)) {
      workspace.partition_name = *partition;
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

  // Per-workspace appearance. Only the known pref paths are kept, and only for
  // workspaces that exist: this dict is written back into real prefs later,
  // and the file it came from is user-editable.
  appearance_.clear();
  if (const base::DictValue* looks = dict.FindDict("appearance")) {
    for (const auto [id_key, look_value] : *looks) {
      int workspace_id = 0;
      if (!base::StringToInt(id_key, &workspace_id) || !Get(workspace_id) ||
          !look_value.is_dict()) {
        continue;
      }
      base::DictValue look;
      for (const auto [path, pref_value] : look_value.GetDict()) {
        if (IsAppearancePref(path)) {
          look.Set(path, pref_value.Clone());
        }
      }
      appearance_[workspace_id] = std::move(look);
    }
  }
  mirrored_workspace_id_ = dict.FindInt("mirrored").value_or(0);
  if (!Get(mirrored_workspace_id_)) {
    mirrored_workspace_id_ = 0;
  }
  saved_current_workspace_id_ =
      dict.FindInt("current").value_or(workspaces_.front().id);

  // Rebuild the visit index. Both structures are filled together so lookup and
  // eviction order stay in step.
  //
  // The salt first: every key below is derived from it.
  if (const std::string* salt = dict.FindString("visit_salt");
      salt && !salt->empty()) {
    visit_salt_ = *salt;
  }
  visit_order_.clear();
  visit_lookup_.clear();
  // Earlier builds stored the URLs themselves. Those are hashed on the way in,
  // and the store is rewritten at once so the plain URLs leave the file now
  // rather than whenever the next visit happens to be recorded.
  bool migrated = false;
  if (const base::DictValue* visits = dict.FindDict("visits")) {
    for (const auto [key, url_list] : *visits) {
      int workspace_id = 0;
      if (!base::StringToInt(key, &workspace_id) || !url_list.is_list()) {
        continue;
      }
      for (const base::Value& visit : url_list.GetList()) {
        const std::string* stored = visit.GetIfString();
        if (!stored) {
          continue;
        }
        std::string visit_key = *stored;
        if (visit_key.find("://") != std::string::npos) {
          visit_key = VisitKey(GURL(*stored));
          migrated = true;
          if (visit_key.empty()) {
            continue;
          }
        }
        if (visit_lookup_[workspace_id].insert(visit_key).second) {
          visit_order_[workspace_id].push_back(std::move(visit_key));
        }
      }
    }
  }
  if (migrated) {
    SchedulePersist();
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
  // A window being RESTORED gets its saved workspace; any other new window
  // (Ctrl+N, "Open link in new window", a popup) opens on the workspace of the
  // window it came from.
  current_workspace_id_ = SessionRestore::IsRestoring(browser->profile())
                              ? store_->default_workspace_id()
                              : store_->WorkspaceForNewWindow();
  // Registered NOW, before any tab exists. The navigator picks a new tab's
  // cookie jar by asking which workspace its window shows, and a window the
  // store has never heard of answers "the default jar" -- so a new window's
  // first tab used to open in the first workspace's session.
  store_->SetWindowWorkspace(browser_, current_workspace_id_);
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
    // Keyed by the BROWSER, not by this manager: browser_navigator.cc has a
    // browser and no way to reach the manager, and it needs to know which
    // workspace a new tab belongs to. The key is opaque either way.
    store_->RemoveWindow(browser_);
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

int ZephyrusWorkspaceManager::AddWorkspace(
    const ZephyrusWorkspaceOptions& options) {
  const int id = store_->AllocateWorkspaceId();
  const size_t position = store_->workspaces().size();
  // Bounded: it is drawn in a title-bar pill and a tooltip, and persisted.
  constexpr size_t kMaxNameLength = 40;
  std::u16string name(
      base::TrimWhitespace(options.name, base::TRIM_ALL).substr(
          0, kMaxNameLength));
  // Only a known icon key is accepted; anything else stays the number.
  std::u16string glyph = zephyrus::FindWorkspaceIcon(options.glyph)
                             ? options.glyph
                             : std::u16string();
  Workspace workspace{id, std::move(name), DefaultColorForIndex(position),
                      std::move(glyph)};
  // "Separate sign-ins" off means the shared jar -- the same one the first
  // workspace uses. See WorkspacePartitionName().
  workspace.partition_name = options.separate_sign_ins
                                 ? zephyrus::WorkspacePartitionName(id)
                                 : std::string();
  store_->workspaces().push_back(std::move(workspace));
  // Its own look, with a plain New Tab Page, so it is recognisable from the
  // first moment rather than a copy of the workspace it was made from.
  store_->SetLook(id, options.seed, options.dark, /*clear_wallpaper=*/true);
  current_workspace_id_ = id;
  // Theme BEFORE the first tab: the New Tab Page then paints in the new
  // workspace's colours instead of flashing the old ones.
  if (IsWindowActive()) {
    store_->MirrorAppearance(id);
  }
  AddTabForWorkspace(id);
  NotifyChanged();
  PersistState();
  return id;
}

void ZephyrusWorkspaceManager::SetWorkspaceLook(int workspace_id,
                                                std::optional<SkColor> seed,
                                                bool dark) {
  if (!GetWorkspace(workspace_id)) {
    return;
  }
  store_->SetLook(workspace_id, seed, dark, /*clear_wallpaper=*/false);
  NotifyChanged();
}

ZephyrusWorkspaceLook ZephyrusWorkspaceManager::GetWorkspaceLook(
    int workspace_id) {
  return store_->LookForEditing(workspace_id);
}

int ZephyrusWorkspaceManager::AddWorkspace() {
  const int id = store_->AllocateWorkspaceId();
  const size_t position = store_->workspaces().size();
  // NUMBERED BY DEFAULT, like a tiling WM's workspaces: the name IS the number.
  //
  // It was "Workspace 3", which is four times the width for no extra
  // information -- the pill sits in a title bar where space is the scarce
  // thing, and a bare numeral is instantly scannable in a way a sentence is
  // not. A user who wants a name or an icon sets one; until then the number
  // does the whole job.
  //
  // NO name. The number shown in the strip is derived from the workspace's
  // CURRENT position every time it is drawn, never stored.
  //
  // Storing it was wrong and visibly so: create 1/2/3, delete 1, and the
  // survivors keep the names "2" and "3" while sitting at positions 1 and 2 --
  // then the next new workspace takes position 2 and is also named "3". That is
  // the "3 2 3" strip. A derived number cannot disagree with the order it is
  // drawn in.
  //
  // An empty name means "unnamed, show the number"; a non-empty one is a name
  // the user chose and is shown as-is.
  // A NEW workspace gets its own StoragePartition -- its own cookies and site
  // data, so the same site can be logged into two accounts at once in one
  // window. Extensions, bookmarks, history and the adblock allowlist stay
  // shared, because there is still only one profile.
  //
  // The partition is only NAMED here. Nothing is created until a tab actually
  // uses it, so making a workspace stays instant and an abandoned one costs
  // nothing on disk.
  Workspace workspace{id, std::u16string(), DefaultColorForIndex(position),
                      u""};
  workspace.partition_name = zephyrus::WorkspacePartitionName(id);
  store_->workspaces().push_back(std::move(workspace));
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
  // Its photo goes with it. Nothing can name the file once the workspace is
  // gone, so leaving it behind would grow the profile directory by one image
  // per deleted workspace, forever.
  if (!it->image.empty()) {
    store_->ForgetImage(it->image);
    zephyrus::DeleteWorkspaceImage(browser_->profile(), it->image);
  }
  // Its stored data goes too -- the cookies and logins that were only ever
  // reachable through this workspace. Deleting the workspace while leaving them
  // on disk, unreachable and never cleaned up, is the worse of the two
  // surprises. A no-op for the default partition, which is the user's ordinary
  // browsing data.
  const std::string doomed_partition = it->partition_name;
  all.erase(it);
  // Its visit index, saved-active-tab, and background bookkeeping go with it —
  // the PersistState() below then rewrites prefs without the dead workspace's
  // URL trail.
  store_->EraseWorkspaceState(workspace_id);
  if (deleting_current) {
    current_workspace_id_ = fallback_id;
  }
  // Every OTHER window learns first, while its tabs are still there: one that
  // was showing this workspace moves off it (MoveOffDeletedWorkspace) before
  // its tabs close, so the emptied window does not open a tab for a workspace
  // that no longer exists.
  store_->NotifyChanged();

  // Then its tabs go from EVERY window, not just this one. Only this window's
  // used to close: the same workspace's tabs in a second window survived as
  // orphans, still running in the partition that is about to be wiped below.
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* other) {
        if (!other || other->GetProfile() != browser_->profile()) {
          return true;
        }
        TabStripModel* model = other->GetTabStripModel();
        if (!model || model == tab_strip_model_ || model->closing_all()) {
          return true;
        }
        std::vector<content::WebContents*> theirs;
        for (int i = 0; i < model->count(); ++i) {
          content::WebContents* contents = model->GetWebContentsAt(i);
          if (contents && GetWorkspaceForContents(contents) == workspace_id) {
            theirs.push_back(contents);
          }
        }
        for (content::WebContents* contents : theirs) {
          const int index = model->GetIndexOfWebContents(contents);
          if (index != TabStripModel::kNoTab) {
            model->CloseWebContentsAt(index, CLOSE_USER_GESTURE);
          }
        }
        return true;
      });

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

  // LAST, after the tabs are closed and the state is written: clearing a
  // partition while tabs are still using it would race them, and doing it
  // before the pref write would risk a crash in between leaving a workspace
  // whose data is already gone.
  zephyrus::ClearWorkspacePartition(browser_->profile(), doomed_partition);
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
      // One identity mark per workspace: choosing an emoji drops any photo.
      // See SetWorkspaceImage() for why they are exclusive.
      if (!workspace.image.empty()) {
        store_->ForgetImage(workspace.image);
        zephyrus::DeleteWorkspaceImage(browser_->profile(), workspace.image);
        workspace.image.clear();
      }
      NotifyChanged();
      PersistState();
      return;
    }
  }
}

void ZephyrusWorkspaceManager::SetWorkspaceImage(int workspace_id,
                                                 const std::string& image) {
  // Defence in depth: the pick path only ever produces a valid name, but this
  // is the seam a future caller would reach for, and the value ends up joined
  // onto a filesystem path.
  if (!image.empty() && !zephyrus::IsValidWorkspaceImageName(image)) {
    return;
  }
  for (Workspace& workspace : store_->workspaces()) {
    if (workspace.id != workspace_id) {
      continue;
    }
    if (workspace.image == image) {
      return;
    }
    // The outgoing file is unreferenced the moment this returns, and nothing
    // else can name it, so it goes now rather than accumulating in the profile.
    if (!workspace.image.empty()) {
      store_->ForgetImage(workspace.image);
      zephyrus::DeleteWorkspaceImage(browser_->profile(), workspace.image);
    }
    workspace.image = image;
    if (!image.empty()) {
      workspace.emoji.clear();
    }
    NotifyChanged();
    PersistState();
    return;
  }
}

gfx::ImageSkia ZephyrusWorkspaceManager::GetWorkspaceImage(int workspace_id,
                                                           int size_dip) {
  const Workspace* workspace = GetWorkspace(workspace_id);
  if (!workspace || workspace->image.empty()) {
    return gfx::ImageSkia();
  }
  return store_->GetImage(workspace->image, size_dip);
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

  // A TAB'S STORAGE PARTITION IS FIXED AT WebContents CREATION.
  //
  // Re-tagging alone was an isolation hole, and a silent one: the tab moved to
  // workspace 3 in the sidebar and kept browsing with workspace 2's cookies.
  // It reads as the opposite of a bug -- you arrive in a new workspace already
  // signed in -- which is exactly how it survived unnoticed.
  //
  // So when the two workspaces sit in different partitions the tab has to be
  // REBUILT in the target one. Same URL, same position, same pinned state, and
  // CopyStateFrom carries the back/forward history across, so the only thing
  // actually lost is live page state (form contents, scroll offset) -- which
  // cannot survive a cookie-jar change in any case.
  //
  // Posted, never synchronous. This is reached from the sidebar's context-menu
  // command, and closing the tab here would destroy the menu's own row while
  // Views is still inside the click that opened it. That is the callback UAF
  // this codebase has hit before.
  const std::string from_partition = store_->PartitionNameForWorkspace(previous);
  const std::string to_partition =
      store_->PartitionNameForWorkspace(workspace_id);
  if (from_partition != to_partition) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&ZephyrusWorkspaceManager::RebuildContentsInWorkspace,
                       weak_factory_.GetWeakPtr(), contents->GetWeakPtr(),
                       workspace_id));
    return;
  }

  store_->SetWorkspaceForContents(contents, workspace_id);

  // FOLLOW THE TAB into its new workspace.
  //
  // This previously stayed put, on the reasoning that moving a tab is filing
  // rather than navigating. In use that reads as nothing having happened: the
  // tab vanishes from the sidebar and the user is left looking at the workspace
  // they just emptied, with no feedback that the move worked. Going there shows
  // the result of the action.
  SwitchToWorkspace(workspace_id);
  const int moved_index = tab_strip_model_->GetIndexOfWebContents(contents);
  if (moved_index != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(moved_index);
  }

  NotifyChanged();
  PersistState();
}

void ZephyrusWorkspaceManager::RebuildContentsInWorkspace(
    base::WeakPtr<content::WebContents> contents_weak,
    int workspace_id) {
  // Everything below re-validates: this runs a task later, so the tab may have
  // been closed and the workspace deleted in between.
  content::WebContents* const contents = contents_weak.get();
  if (!contents || !tab_strip_model_ || !GetWorkspace(workspace_id)) {
    return;
  }
  const int index = tab_strip_model_->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab) {
    return;
  }

  Profile* const profile = browser_ ? browser_->profile() : nullptr;
  if (!profile) {
    return;
  }
  const GURL url = contents->GetLastCommittedURL();
  const std::string partition = store_->PartitionNameForWorkspace(workspace_id);

  scoped_refptr<content::SiteInstance> site_instance =
      zephyrus::SiteInstanceForWorkspace(profile, partition, url);
  // Null means the target is the DEFAULT partition, which needs no fixed
  // SiteInstance -- a plain new-tab SiteInstance already lands there.
  content::WebContents::CreateParams create_params(
      profile, site_instance ? site_instance
                             : tab_util::GetSiteInstanceForNewTab(profile, url));
  std::unique_ptr<content::WebContents> replacement =
      content::WebContents::Create(create_params);
  if (!replacement) {
    return;
  }

  // NO HISTORY COPY. The tab starts fresh at its current URL.
  //
  // CopyStateFrom was tried and CRASHED the browser on any tab with real
  // history -- which is every tab worth moving. Navigation entries carry the
  // SiteInstance they were committed in, and those are bound to the SOURCE
  // partition; cloning them into a WebContents built for a DIFFERENT partition
  // hands content a history that disagrees with the tab it belongs to, and it
  // CHECKs on the next navigation. The first dump had
  // RenderFrameHostManager::GetSiteInstanceForNavigation in it for exactly
  // this reason.
  //
  // Losing back/forward is the honest price of changing cookie jars: those
  // entries were fetched with another workspace's session, so replaying them
  // here would be wrong even if content allowed it.
  if (url.is_valid()) {
    replacement->GetController().LoadURL(url, content::Referrer(),
                                         ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                         std::string());
  }

  const bool was_pinned = tab_strip_model_->IsTabPinned(index);
  content::WebContents* const replacement_ptr = replacement.get();

  // FORCE the workspace across the insertion.
  //
  // Setting it on the store beforehand is NOT enough and was the bug: the
  // kInserted handler unconditionally re-tags every inserted tab with
  // WorkspaceIdForInsertedContents(), which infers the window's CURRENT
  // workspace -- precisely the one we are moving the tab out of. The symptom
  // was a tab that reloaded, logged out (it did get the target partition) and
  // then stayed exactly where it was.
  //
  // pending_forced_workspace_id_ is the channel that handler already honours
  // above every other source, which is why AddTabForWorkspace uses it too.
  pending_forced_workspace_id_ = workspace_id;

  int add_types = AddTabTypes::ADD_NONE;
  if (was_pinned) {
    add_types |= AddTabTypes::ADD_PINNED;
  }
  tab_strip_model_->InsertWebContentsAt(index + 1, std::move(replacement),
                                        add_types);
  // Consumed by the insertion; cleared defensively in case it never arrived.
  pending_forced_workspace_id_.reset();
  store_->SetWorkspaceForContents(replacement_ptr, workspace_id);

  // Follow the tab, exactly as the same-partition path does. Whether it was the
  // ACTIVE tab is deliberately not consulted: the user asked for this tab to go
  // there, so that is where they should end up either way.
  SwitchToWorkspace(workspace_id);
  const int new_index = tab_strip_model_->GetIndexOfWebContents(replacement_ptr);
  if (new_index != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(new_index);
  }

  // CLOSING THE ORIGINAL IS ITS OWN TASK, and that is not tidiness.
  //
  // Closing here CHECK-crashed the browser. CloseWebContentsAt fires its
  // observers synchronously, our own OnTabStripModelChanged runs inside that
  // notification, and anything it does that touches the strip re-enters a
  // TabStripModel that is mid-close -- which the model CHECKs against.
  // Confirmed from the dump: exception 0x80000003 with
  // SendDetachWebContentsNotifications / CloseWebContentses on the faulting
  // thread.
  //
  // A separate task turn means the insert's notifications are fully drained
  // before the close begins, so neither re-enters the other.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&ZephyrusWorkspaceManager::CloseReplacedContents,
                     weak_factory_.GetWeakPtr(), contents_weak));

  NotifyChanged();
  PersistState();
}

void ZephyrusWorkspaceManager::CloseReplacedContents(
    base::WeakPtr<content::WebContents> contents_weak) {
  content::WebContents* const contents = contents_weak.get();
  if (!contents || !tab_strip_model_ || tab_strip_model_->closing_all()) {
    return;
  }
  const int index = tab_strip_model_->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab) {
    return;  // Already gone -- the user closed it in the meantime.
  }
  store_->EraseContents(contents);
  tab_strip_model_->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
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

bool ZephyrusWorkspaceManager::IsContentsInCurrentWorkspace(
    content::WebContents* contents) const {
  // Pinning is PER-WORKSPACE. It used to make a tab a global favourite, visible
  // everywhere — that cannot survive workspaces being profiles, because a tab
  // is one WebContents with one profile and cannot be in several at once.
  //
  // Dropping the exemption is also what makes the window's core invariant hold
  // without exception: every visible tab belongs to the current workspace, so
  // the window has exactly one active profile at any instant. Window chrome
  // rebinds on workspace switch and needs no per-tab profile resolution.
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
  // Collected, not acted on in the loop: rebuilding a tab inserts and closes,
  // which would invalidate the indices this is walking.
  std::vector<content::WebContents*> needs_repartition;
  const std::string current_partition =
      store_->PartitionNameForWorkspace(current_workspace_id_);

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
      // ADOPTING IS ALSO A RE-TAG, so it has the same hole
      // MoveContentsToWorkspace had: the tab keeps the partition it was built
      // with. An orphan is usually orphaned BECAUSE its workspace was deleted,
      // and deleting a workspace wipes its partition -- so re-tagging alone
      // leaves the tab reading from a jar that no workspace owns and nothing
      // will ever clear again.
      //
      // Compare against the tab's LIVE partition rather than its old tag: the
      // tag is exactly what is unreliable here.
      if (zephyrus::PartitionNameOfContents(contents) != current_partition) {
        needs_repartition.push_back(contents);
        continue;
      }
      store_->SetWorkspaceForContents(contents, current_workspace_id_);
      adopted = true;
    }
  }

  // Posted for the same reason the move is: this closes tabs, and adoption runs
  // from tab-strip and workspace-deletion paths that are mid-notification.
  for (content::WebContents* contents : needs_repartition) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&ZephyrusWorkspaceManager::RebuildContentsInWorkspace,
                       weak_factory_.GetWeakPtr(), contents->GetWeakPtr(),
                       current_workspace_id_));
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
    case TabStripModelChange::kInserted: {
      content::WebContents* follow = nullptr;
      for (const auto& contents_with_index : change.GetInsert()->contents) {
        // A tab that ALREADY belongs to a workspace arrived from another
        // window (a drag, or "Move tab to new window"). It keeps that
        // workspace: its cookie jar was fixed when it was created, and
        // re-filing it here -- which this handler used to do unconditionally
        // -- put a tab still signed in to workspace 2 into whatever this
        // window was showing. The window follows it instead.
        const int existing =
            store_->GetWorkspaceForContents(contents_with_index.contents);
        if (existing != 0 && store_->Get(existing) &&
            !pending_forced_workspace_id_.has_value()) {
          if (existing != current_workspace_id_ &&
              contents_with_index.contents ==
                  tab_strip_model_->GetActiveWebContents()) {
            follow = contents_with_index.contents;
          }
          continue;
        }
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
      if (follow) {
        // Posted: switching activates tabs, and this is inside the strip's own
        // notification.
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(&ZephyrusWorkspaceManager::FollowTabIntoWorkspace,
                           weak_factory_.GetWeakPtr(), follow->GetWeakPtr()));
      }
      break;
    }
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
  store_->SetWindowWorkspace(browser_, current_workspace_id_);
  // The window the user is in decides whose theme is live.
  if (IsWindowActive()) {
    store_->MirrorAppearance(current_workspace_id_);
  }
  // Fan out through the store rather than notifying only this window. The
  // workspace list and the tab->workspace map are shared, so a rename, delete
  // or move made here must refresh EVERY window's sidebar and pill — otherwise
  // the other window keeps rendering stale state until something else nudges
  // it. The store calls NotifyLocalObservers() on each manager, which does not
  // re-enter here, so this terminates.
  store_->NotifyChanged();
}

void ZephyrusWorkspaceManager::NotifyLocalObservers() {
  if (store_ && !store_->Get(current_workspace_id_)) {
    MoveOffDeletedWorkspace();
  }
  UpdateWindowLook();
  changed_callbacks_.Notify();
}

bool ZephyrusWorkspaceManager::IsWindowActive() const {
  return browser_ && browser_->window() && browser_->window()->IsActive();
}

void ZephyrusWorkspaceManager::OnWindowActivated() {
  if (store_ && store_->MirrorAppearance(current_workspace_id_)) {
    // Every window re-decides whether it needs its own colours.
    store_->NotifyChanged();
  }
}

void ZephyrusWorkspaceManager::UpdateWindowLook() {
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!view || !view->browser_widget() || !store_) {
    return;
  }
  std::optional<BrowserWidget::ZephyrusLook> look;
  // Only a window showing a workspace OTHER than the mirrored one needs its own
  // colours; the mirrored one follows the live theme, which is also what lets a
  // Customize Chrome edit show up immediately.
  if (!browser_->profile()->IsOffTheRecord() &&
      current_workspace_id_ != store_->mirrored_workspace_id()) {
    if (std::optional<ZephyrusWorkspaceLook> recorded =
            store_->GetLook(current_workspace_id_)) {
      look = BrowserWidget::ZephyrusLook{recorded->seed,
                                         recorded->dark.value_or(true),
                                         recorded->grayscale};
    }
  }
  view->browser_widget()->SetZephyrusLook(look);
}

void ZephyrusWorkspaceManager::MoveOffDeletedWorkspace() {
  const int fallback = store_->default_workspace_id();
  if (!store_->Get(fallback)) {
    return;
  }
  current_workspace_id_ = fallback;
  store_->SetWindowWorkspace(browser_, fallback);
  // Tab work is posted: this runs inside a store notification.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&ZephyrusWorkspaceManager::EnsureActiveTabInWorkspace,
                     weak_factory_.GetWeakPtr()));
}

void ZephyrusWorkspaceManager::FollowTabIntoWorkspace(
    base::WeakPtr<content::WebContents> contents) {
  if (!contents || tab_strip_model_->closing_all()) {
    return;
  }
  const int workspace = GetWorkspaceForContents(contents.get());
  if (!GetWorkspace(workspace) || workspace == current_workspace_id_) {
    return;
  }
  SwitchToWorkspace(workspace);
  const int index = tab_strip_model_->GetIndexOfWebContents(contents.get());
  if (index != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(index);
  }
}

void ZephyrusWorkspaceManager::AddTabForWorkspace(int workspace_id) {
  // TELL THE STORE FIRST, before any tab is created.
  //
  // The new tab is built through Navigate(), which asks the store which
  // partition this window's current workspace uses. Callers reach here having
  // set `current_workspace_id_` but BEFORE NotifyChanged() -- which is where
  // the store normally learns it -- so without this the tab would be created
  // in the PREVIOUS workspace's cookie jar.
  //
  // That failure is silent and reads as the opposite of a bug: the user makes
  // a fresh workspace, opens a site, and is already logged in.
  //
  // SetWindowWorkspace() is idempotent, so the NotifyChanged() that follows in
  // every caller still costs nothing.
  store_->SetWindowWorkspace(browser_, workspace_id);

  // ALWAYS spawn, including when the window is otherwise empty.
  //
  // This used to return early on an empty window, on the reasoning that
  // "Zephyrus has no new-tab page" and the empty backdrop was the intended
  // state. The NTP is back, so that premise is gone -- and the early return
  // made the behaviour depend on something the user is not thinking about:
  // emptying a workspace gave you a new tab if ANOTHER workspace still had
  // tabs, and the backdrop if it did not.
  //
  // The visible consequence is that the empty backdrop no longer appears when
  // the last tab closes. Restoring it for that case alone is this early return,
  // put back.

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
  // SCHEDULE, do not write.
  //
  // This called Persist() directly, which serializes the whole workspace state
  // synchronously on the UI thread and writes a pref -- and SerializeState()
  // walks every window x every tab to compute each workspace's active-tab
  // ordinal. Doing that inside SwitchToWorkspace meant every click on a
  // workspace paid an O(workspaces x windows x tabs) walk plus a pref write
  // before the new tab could paint, which is exactly the "switching is slow"
  // symptom.
  //
  // SchedulePersist() already existed for this, with a 300ms debounce to
  // coalesce bursts -- the switch path simply was not using it. Nothing is
  // lost: the state is still written, just after the UI has moved.
  store_->SchedulePersist();
}
