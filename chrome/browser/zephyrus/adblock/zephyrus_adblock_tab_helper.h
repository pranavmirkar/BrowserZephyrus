// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_TAB_HELPER_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_TAB_HELPER_H_

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
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

  void IncrementBlocked() { ++blocked_this_page_; }
  int blocked_this_page() const { return blocked_this_page_; }

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

 private:
  friend class content::WebContentsUserData<ZephyrusAdblockTabHelper>;

  explicit ZephyrusAdblockTabHelper(content::WebContents* contents);

  int blocked_this_page_ = 0;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_TAB_HELPER_H_
