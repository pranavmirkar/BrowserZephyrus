// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_workspace_partition.h"

#include <optional>
#include <string_view>

#include "base/functional/callback_helpers.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "url/gurl.h"

namespace zephyrus {

const char kWorkspacePartitionDomain[] = "zephyrus-workspace";
const char kWorkspaceExtraDataKey[] = "zephyrus_workspace";

namespace {

constexpr std::string_view kPrefix = "ws";

// The config for `partition_name`, or nullopt for the default partition (an
// empty or invalid name). Internal: callers want a SiteInstance or a wipe, not
// a config, and an exported function nothing calls is how dead API accumulates.
std::optional<content::StoragePartitionConfig> WorkspacePartitionConfig(
    content::BrowserContext* context,
    const std::string& partition_name) {
  // Empty and invalid both mean "the default partition". They are not the same
  // thing, but they call for the same behaviour, and the alternative -- failing
  // a navigation because a pref was edited -- is worse than serving it from the
  // shared jar.
  if (!context || !IsValidWorkspacePartitionName(partition_name)) {
    return std::nullopt;
  }
  return content::StoragePartitionConfig::Create(
      context, kWorkspacePartitionDomain, partition_name,
      // NOT in-memory: a workspace must still be logged in tomorrow.
      /*in_memory=*/false);
}

}  // namespace

std::string WorkspacePartitionName(int workspace_id) {
  // The first workspace and anything unnumbered stay on the DEFAULT partition,
  // which is where every existing user's cookies already are. Giving workspace
  // one its own partition would sign everybody out on update, in exchange for
  // nothing they asked for.
  if (workspace_id <= 1) {
    return std::string();
  }
  return base::StrCat({kPrefix, base::NumberToString(workspace_id)});
}

bool IsValidWorkspacePartitionName(const std::string& name) {
  // Shape: ws<digits>. Whitelisted rather than scanned for separators -- this
  // string reaches the filesystem as a directory name, and a blacklist is only
  // as good as the last bypass someone thought of.
  if (name.size() < kPrefix.size() + 1 || name.size() > 32) {
    return false;
  }
  if (!std::string_view(name).starts_with(kPrefix)) {
    return false;
  }
  for (char c : std::string_view(name).substr(kPrefix.size())) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

scoped_refptr<content::SiteInstance> SiteInstanceForWorkspace(
    content::BrowserContext* context,
    const std::string& partition_name,
    const GURL& url) {
  const std::optional<content::StoragePartitionConfig> config =
      WorkspacePartitionConfig(context, partition_name);
  if (!config.has_value()) {
    return nullptr;
  }
  // "Preserved across navigations" is the property that matters: without it the
  // tab would fall back to the default partition on the first cross-site
  // navigation, and the workspace's isolation would quietly end mid-session.
  return content::SiteInstance::CreateForFixedStoragePartition(context, url,
                                                               *config);
}

Profile* ActiveProfile(Browser* browser) {
  if (!browser) {
    return nullptr;
  }
  TabStripModel* model = browser->tab_strip_model();
  content::WebContents* contents =
      model ? model->GetActiveWebContents() : nullptr;
  if (!contents) {
    return browser->profile();
  }
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  return profile ? profile : browser->profile();
}

void ClearWorkspacePartition(content::BrowserContext* context,
                             const std::string& partition_name) {
  const std::optional<content::StoragePartitionConfig> config =
      WorkspacePartitionConfig(context, partition_name);
  if (!config.has_value()) {
    return;
  }
  content::StoragePartition* partition =
      context->GetStoragePartition(*config, /*can_create=*/false);
  if (!partition) {
    return;
  }
  // Everything, for all time. A deleted workspace leaves no logins behind: the
  // data is unreachable from the UI the moment the workspace is gone, so
  // keeping it would mean cookies nobody can see and nobody will ever clear.
  partition->ClearData(
      content::StoragePartition::REMOVE_DATA_MASK_ALL,
      /*filter_builder=*/nullptr,
      content::StoragePartition::StorageKeyPolicyMatcherFunction(),
      /*cookie_deletion_filter=*/nullptr,
      /*perform_storage_cleanup=*/true, base::Time(), base::Time::Max(),
      base::DoNothing());
}

}  // namespace zephyrus
