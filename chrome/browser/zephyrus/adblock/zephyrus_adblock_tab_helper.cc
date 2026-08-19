// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_tab_helper.h"

#include <string>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "base/json/string_escape.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service_factory.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace zephyrus_adblock {

ZephyrusAdblockTabHelper::ZephyrusAdblockTabHelper(
    content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<ZephyrusAdblockTabHelper>(*contents) {}

ZephyrusAdblockTabHelper::~ZephyrusAdblockTabHelper() = default;

void ZephyrusAdblockTabHelper::PrimaryPageChanged(content::Page& page) {
  // New document showing; reset the per-page block count.
  blocked_this_page_ = 0;
  changed_callbacks_.Notify();
}

// NOTE: cosmetic (element-hiding) CSS is now injected in the renderer's MAIN
// world by ScriptletAgent (via the ScriptletHost mojo payload) alongside the
// scriptlets — the isolated-world browser injection previously done here proved
// unreliable at document-commit time. This observer is retained for per-page
// block accounting (PrimaryPageChanged) and future hooks.

WEB_CONTENTS_USER_DATA_KEY_IMPL(ZephyrusAdblockTabHelper);

}  // namespace zephyrus_adblock
