// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_throttle.h"

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service_factory.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "url/gurl.h"

namespace zephyrus_adblock {

namespace {

ResourceType MapDestination(network::mojom::RequestDestination destination) {
  using RD = network::mojom::RequestDestination;
  switch (destination) {
    case RD::kScript:
    case RD::kServiceWorker:
    case RD::kSharedWorker:
    case RD::kWorker:
      return kTypeScript;
    case RD::kImage:
      return kTypeImage;
    case RD::kStyle:
    case RD::kXslt:
      return kTypeStylesheet;
    case RD::kFont:
      return kTypeFont;
    case RD::kObject:
    case RD::kEmbed:
      return kTypeObject;
    case RD::kFrame:
    case RD::kIframe:
    case RD::kFencedframe:
      return kTypeSubdocument;
    case RD::kAudio:
    case RD::kVideo:
    case RD::kTrack:
      return kTypeMedia;
    case RD::kEmpty:
      // fetch()/XHR requests carry an empty destination.
      return kTypeXhr;
    default:
      return kTypeOther;
  }
}

}  // namespace

// static
std::unique_ptr<ZephyrusAdblockThrottle> ZephyrusAdblockThrottle::MaybeCreate(
    content::BrowserContext* browser_context,
    const WebContentsGetter& wc_getter) {
  ZephyrusAdblockService* service =
      ZephyrusAdblockServiceFactory::GetForBrowserContext(browser_context);
  if (!service || !service->enabled()) {
    return nullptr;
  }
  return std::make_unique<ZephyrusAdblockThrottle>(service->GetWeakPtr(),
                                                   wc_getter);
}

ZephyrusAdblockThrottle::ZephyrusAdblockThrottle(
    base::WeakPtr<ZephyrusAdblockService> service,
    WebContentsGetter wc_getter)
    : service_(std::move(service)), wc_getter_(std::move(wc_getter)) {}

ZephyrusAdblockThrottle::~ZephyrusAdblockThrottle() = default;

void ZephyrusAdblockThrottle::WillStartRequest(
    network::ResourceRequest* request,
    bool* defer) {
  if (!service_) {
    return;
  }
  const GURL initiator =
      request->request_initiator ? request->request_initiator->GetURL()
                                 : GURL();
  if (!service_->ShouldBlockRequest(request->url, initiator,
                                    MapDestination(request->destination))) {
    return;
  }
  // Attribute the block to the originating tab (for the per-page count).
  if (wc_getter_) {
    if (content::WebContents* web_contents = wc_getter_.Run()) {
      ZephyrusAdblockTabHelper::CreateForWebContents(web_contents);
      ZephyrusAdblockTabHelper::FromWebContents(web_contents)
          ->IncrementBlocked();
    }
  }
  delegate_->CancelWithError(net::ERR_BLOCKED_BY_CLIENT, "ZephyrusAdblock");
}

}  // namespace zephyrus_adblock
