// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_proxying_url_loader_factory.h"

#include "base/functional/bind.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service_factory.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_tab_helper.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
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
      return kTypeXhr;
    default:
      return kTypeOther;
  }
}

}  // namespace

ZephyrusAdblockProxyingURLLoaderFactory::ZephyrusAdblockProxyingURLLoaderFactory(
    mojo::PendingReceiver<network::mojom::URLLoaderFactory> loader_receiver,
    mojo::PendingRemote<network::mojom::URLLoaderFactory> target_factory_remote,
    base::WeakPtr<ZephyrusAdblockService> service,
    content::GlobalRenderFrameHostId frame_id,
    base::SelfDeletingPassKey pass_key)
    : network::SelfDeletingURLLoaderFactory(std::move(loader_receiver),
                                            pass_key),
      service_(std::move(service)),
      frame_id_(frame_id) {
  target_factory_.Bind(std::move(target_factory_remote));
  target_factory_.set_disconnect_handler(base::BindOnce(
      &ZephyrusAdblockProxyingURLLoaderFactory::OnTargetFactoryError,
      base::Unretained(this)));
}

ZephyrusAdblockProxyingURLLoaderFactory::
    ~ZephyrusAdblockProxyingURLLoaderFactory() = default;

// static
void ZephyrusAdblockProxyingURLLoaderFactory::MaybeProxyRequest(
    content::BrowserContext* browser_context,
    content::RenderFrameHost* frame,
    network::URLLoaderFactoryBuilder& factory_builder) {
  ZephyrusAdblockService* service =
      ZephyrusAdblockServiceFactory::GetForBrowserContext(browser_context);
  if (!service || !service->enabled()) {
    return;
  }

  const content::GlobalRenderFrameHostId frame_id =
      frame ? frame->GetGlobalId() : content::GlobalRenderFrameHostId();

  auto [receiver, remote] = factory_builder.Append();
  base::MakeSelfDeleting<ZephyrusAdblockProxyingURLLoaderFactory>(
      std::move(receiver), std::move(remote), service->GetWeakPtr(), frame_id);
}

void ZephyrusAdblockProxyingURLLoaderFactory::CreateLoaderAndStart(
    mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
    int32_t request_id,
    uint32_t options,
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) {
  const GURL initiator =
      request.request_initiator ? request.request_initiator->GetURL() : GURL();
  if (service_ &&
      service_->ShouldBlockRequest(request.url, initiator,
                                   MapDestination(request.destination))) {
    // Attribute the block to the originating tab for the per-page counter.
    if (content::RenderFrameHost* rfh =
            content::RenderFrameHost::FromID(frame_id_)) {
      if (content::WebContents* web_contents =
              content::WebContents::FromRenderFrameHost(rfh)) {
        ZephyrusAdblockTabHelper::CreateForWebContents(web_contents);
        ZephyrusAdblockTabHelper::FromWebContents(web_contents)
            ->IncrementBlocked();
      }
    }
    // Fail the request without contacting the network.
    mojo::Remote<network::mojom::URLLoaderClient> client_remote(
        std::move(client));
    client_remote->OnComplete(
        network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
    return;
  }

  target_factory_->CreateLoaderAndStart(std::move(loader_receiver), request_id,
                                        options, request, std::move(client),
                                        traffic_annotation);
}

void ZephyrusAdblockProxyingURLLoaderFactory::OnTargetFactoryError() {
  DisconnectReceiversAndDestroy();
}

}  // namespace zephyrus_adblock
