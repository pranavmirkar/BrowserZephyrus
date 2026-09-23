// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_TAB_HELPER_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_TAB_HELPER_H_

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class NavigationHandle;
class Page;
}

namespace zephyrus_adblock {

// Per-tab counter of requests blocked on the currently-displayed page. Reset on
// each primary-page navigation, so it reflects "blocked on this page" like the
// uBlock Origin badge.
class ZephyrusAdblockTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<ZephyrusAdblockTabHelper> {
 public:
  ~ZephyrusAdblockTabHelper() override;

  ZephyrusAdblockTabHelper(const ZephyrusAdblockTabHelper&) = delete;
  ZephyrusAdblockTabHelper& operator=(const ZephyrusAdblockTabHelper&) = delete;

  void IncrementBlocked() {
    ++blocked_this_page_;
    changed_callbacks_.Notify();
  }
  int blocked_this_page() const { return blocked_this_page_; }

  // Fires on every change to the count, including the reset on navigation, so
  // the toolbar badge can follow the tab without polling. Blocked requests
  // arrive in bursts during a page load — subscribers are expected to coalesce
  // rather than repaint per call.
  base::CallbackListSubscription AddChangedCallback(
      base::RepeatingClosure callback) {
    return changed_callbacks_.Add(std::move(callback));
  }

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;
  // Pushes the committing document's start-of-document payload -- scriptlets,
  // hiding CSS and fingerprint seed -- ahead of the commit. See
  // DocumentStartPayload for why this replaced two synchronous fetches.
  void ReadyToCommitNavigation(
      content::NavigationHandle* navigation_handle) override;

 private:
  friend class content::WebContentsUserData<ZephyrusAdblockTabHelper>;

  explicit ZephyrusAdblockTabHelper(content::WebContents* contents);

  int blocked_this_page_ = 0;
  base::RepeatingClosureList changed_callbacks_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_TAB_HELPER_H_
