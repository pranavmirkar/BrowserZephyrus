// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_PARTITION_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_PARTITION_H_

#include <string>

#include "base/memory/scoped_refptr.h"

class Browser;
class GURL;
class Profile;

namespace content {
class BrowserContext;
class SiteInstance;
class WebContents;
}  // namespace content

// Zephyrus: how a workspace gets its own cookies.
//
// A workspace is a StoragePartition inside the window's ONE profile -- not a
// profile of its own. Cookies, localStorage, IndexedDB and service workers are
// separate per workspace; extensions, bookmarks, history, the adblock engine
// and its allowlist are shared, because there is only one profile to hold them.
//
// WHY NOT A PROFILE PER WORKSPACE
// -------------------------------
// It was built that way first and abandoned, for three measured reasons:
//
//   1. Extensions would SILENTLY not run. An extension installed in the primary
//      profile is not in another profile's registry, so it never injects into
//      that profile's tabs. No crash, no error -- just an extension that stops
//      working in most of the browser.
//   2. The adblocker duplicates. ZephyrusAdblockServiceFactory is
//      `WithRegular(kOwnInstance)`, so every profile parses its own filter
//      lists and keeps its own allowlist: allow a site in one workspace, still
//      blocked in the next.
//   3. Memory goes UP. The expensive object was always the Profile.
//
// A spike settled it against a real build rather than by argument: logged into
// a site then opened it in a fixed partition -- logged out, so cookies are
// separate; Dark Reader still themed the page, so extensions still run; the
// shield counter still counted, so blocking still works.
//
// CAUTION: `SiteInstance::CreateForFixedStoragePartition()` has roughly one
// production caller in the tree. This is a lightly-trodden path. Prefer a spike
// over reasoning when extending it.
namespace zephyrus {

// All workspace partitions share this domain, so they are recognisable on disk
// and can be cleared as a group.
extern const char kWorkspacePartitionDomain[];

// Key under which a closed tab's workspace id is stored in the tab-restore
// record. Written when a tab is closed, read when it is reopened, because the
// StoragePartition has to be chosen before the WebContents exists.
extern const char kWorkspaceExtraDataKey[];

// The partition name for a workspace, or EMPTY for the default partition.
//
// Empty is not a failure: it is what workspace ids <= 0 and pre-isolation
// workspaces get, and it means "the shared cookie jar the browser has always
// used". Existing workspaces keep it so nobody is logged out by an update.
std::string WorkspacePartitionName(int workspace_id);

// True if `name` is a name this code could have generated. Partition names come
// from prefs, which is a user-writable file, and end up naming a directory.
bool IsValidWorkspacePartitionName(const std::string& name);

// A SiteInstance that pins a new tab to `partition_name` across navigations,
// or null for the default partition.
//
// Only needed where a tab is created with NO opener. A tab opened FROM another
// tab inherits its opener's SiteInstance, and so its partition, for free.
// The workspace partition `contents` is ACTUALLY in, read from its live
// SiteInstance rather than from whatever workspace it is tagged with.
//
// The two can disagree, and that disagreement is the bug this exists to catch:
// a tab's partition is fixed when its WebContents is created, so any code that
// re-tags a tab moves the label without moving the cookies.
//
// Returns the empty string for the default partition AND for anything outside
// our partition domain (a guest view, an extension) -- both mean "not in a
// workspace partition", which is what callers act on.
std::string PartitionNameOfContents(content::WebContents* contents);

scoped_refptr<content::SiteInstance> SiteInstanceForWorkspace(
    content::BrowserContext* context,
    const std::string& partition_name,
    const GURL& url);

// The profile the user is actually acting on in `browser`: the ACTIVE TAB's.
//
// With workspaces on partitions there is one profile per window, so today this
// equals `browser->profile()`. It is not redundant: Private Workspace puts an
// off-the-record profile in play, and PW-6 (in-window private tabs) would put
// one in the same strip as regular tabs. Code that acts on the CURRENT PAGE --
// the adblock allowlist, the privacy record, search classification -- should
// ask the tab rather than the window, so that it is already right when that
// lands instead of quietly writing a private page's data to the regular jar.
//
// Falls back to the window's profile when there is no active tab.
Profile* ActiveProfile(Browser* browser);

// Erases everything a workspace stored: cookies, storage, caches. Called when a
// workspace is deleted -- otherwise its logins stay on disk, reachable by
// nothing and cleaned up by nobody. A no-op for the default partition, which
// holds the user's ordinary browsing data.
void ClearWorkspacePartition(content::BrowserContext* context,
                             const std::string& partition_name);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_PARTITION_H_
