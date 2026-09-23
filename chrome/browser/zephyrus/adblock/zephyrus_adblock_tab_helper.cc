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
#include "chrome/browser/zephyrus/adblock/mojom/zephyrus_adblock.mojom.h"
#include "chrome/browser/zephyrus/privacy/zephyrus_fingerprint_seed_host.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
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

void ZephyrusAdblockTabHelper::ReadyToCommitNavigation(
    content::NavigationHandle* navigation_handle) {
  // A same-document navigation keeps its document, and the payload it was
  // pushed; nothing new starts.
  if (navigation_handle->IsSameDocument()) {
    return;
  }
  content::RenderFrameHost* rfh = navigation_handle->GetRenderFrameHost();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    return;
  }

  // The URL the document will have. The renderer applies the payload only to
  // a document with exactly this URL (fragment removed), so a navigation that
  // is superseded after this point cannot leave its payload on the next one.
  const GURL url = navigation_handle->GetURL().GetWithoutRef();
  auto payload = mojom::DocumentStartPayload::New();
  payload->url = url.spec();

  // Scriptlets and hiding CSS only for real web pages; an error page is
  // Chrome's own document and must not be patched.
  if (url.SchemeIsHTTPOrHTTPS() && !navigation_handle->IsErrorPage()) {
    if (ZephyrusAdblockService* service =
            ZephyrusAdblockServiceFactory::GetForBrowserContext(
                rfh->GetBrowserContext())) {
      payload->script = service->GetScriptletInjection(url);
      payload->hide_css = service->GetCosmeticCss(url);
      payload->style_css = service->GetCosmeticStyleCss(url);
    }
  }

  // The seed rides along because it has the same problem -- a page can read
  // a canvas in its first inline script -- and one message is cheaper than
  // two. Sent for every scheme: randomization is not a web-only concern.
  const zephyrus_privacy::NavigationSeed seed =
      zephyrus_privacy::SeedForNavigation(navigation_handle);
  payload->fingerprint_known = seed.known;
  payload->fingerprint_seed = seed.seed;
  payload->fingerprint_surfaces = seed.surfaces;

  mojo::AssociatedRemote<mojom::DocumentStartAgent> agent;
  rfh->GetRemoteAssociatedInterfaces()->GetInterface(&agent);
  agent->SetDocumentStartPayload(std::move(payload));
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(ZephyrusAdblockTabHelper);

}  // namespace zephyrus_adblock
