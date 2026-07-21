// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_PROXYING_URL_LOADER_FACTORY_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_PROXYING_URL_LOADER_FACTORY_H_

#include "base/memory/self_deleting.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/global_routing_id.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/self_deleting_url_loader_factory.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"

namespace content {
class BrowserContext;
class RenderFrameHost;
}  // namespace content

namespace zephyrus_adblock {

class ZephyrusAdblockService;

// A browser-process URLLoaderFactory proxy that blocks requests matching the
// ad-block engine. Inserted for every factory (navigation AND subresource) via
// ChromeContentBrowserClient::WillCreateURLLoaderFactory, so it sees every
// request while the engine + stats stay in a single process (RAM-friendly).
class ZephyrusAdblockProxyingURLLoaderFactory
    : public network::SelfDeletingURLLoaderFactory {
 public:
  ZephyrusAdblockProxyingURLLoaderFactory(
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> loader_receiver,
      mojo::PendingRemote<network::mojom::URLLoaderFactory>
          target_factory_remote,
      base::WeakPtr<ZephyrusAdblockService> service,
      content::GlobalRenderFrameHostId frame_id,
      base::SelfDeletingPassKey pass_key);
  ZephyrusAdblockProxyingURLLoaderFactory(
      const ZephyrusAdblockProxyingURLLoaderFactory&) = delete;
  ZephyrusAdblockProxyingURLLoaderFactory& operator=(
      const ZephyrusAdblockProxyingURLLoaderFactory&) = delete;

  // Inserts the proxy into `factory_builder` when ad-blocking is enabled.
  static void MaybeProxyRequest(
      content::BrowserContext* browser_context,
      content::RenderFrameHost* frame,
      network::URLLoaderFactoryBuilder& factory_builder);

  // network::mojom::URLLoaderFactory:
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation)
      override;

 private:
  ~ZephyrusAdblockProxyingURLLoaderFactory() override;

  void OnTargetFactoryError();

  mojo::Remote<network::mojom::URLLoaderFactory> target_factory_;
  base::WeakPtr<ZephyrusAdblockService> service_;
  content::GlobalRenderFrameHostId frame_id_;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_PROXYING_URL_LOADER_FACTORY_H_
