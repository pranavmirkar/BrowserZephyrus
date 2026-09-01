// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_PROXYING_URL_LOADER_FACTORY_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_PROXYING_URL_LOADER_FACTORY_H_

#include <stdint.h>

#include <optional>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "base/memory/self_deleting.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/zephyrus/privacy/domain_string_table.h"
#include "chrome/browser/zephyrus/privacy/privacy_cname_cache.h"
#include "chrome/browser/zephyrus/privacy/privacy_event_sink.h"
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
      scoped_refptr<zephyrus_privacy::PrivacyEventSink> privacy_sink,
      scoped_refptr<zephyrus_privacy::DomainStringTable> privacy_strings,
      scoped_refptr<zephyrus_privacy::PrivacyCnameCache> privacy_cname_cache,
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

  // Feeds the Privacy Intelligence pipeline. No-op when the sink is null,
  // which is the case whenever the feature is off or the profile is
  // off-the-record. Never changes a block decision — the dependency is
  // one-directional (spec 3.1).
  void RecordPrivacyEvent(const GURL& url, bool blocked);

  // eTLD+1 of the TOP-LEVEL document, hashed, resolved once and cached.
  //
  // Resolving per request would mean RenderFrameHost::FromID plus a
  // WebContents walk on the UI thread for all 200-400 requests of a page,
  // which is the single most damaging thing this feature could do to browsing.
  // A factory is created per document commit, so the top-level site is fixed
  // for its lifetime.
  //
  // KNOWN RISK: if a factory is ever reused across a cross-document
  // navigation, events would be attributed to the previous site. A browser
  // test covering navigation attribution is required before any UI depends on
  // this (spec 12.2).
  uint32_t ResolveSiteId();

  mojo::Remote<network::mojom::URLLoaderFactory> target_factory_;
  base::WeakPtr<ZephyrusAdblockService> service_;
  content::GlobalRenderFrameHostId frame_id_;

  scoped_refptr<zephyrus_privacy::PrivacyEventSink> privacy_sink_;
  // The text channel beside the 16-byte events: RawEvent carries only
  // hashes, so the names have to travel separately (§8.5).
  scoped_refptr<zephyrus_privacy::DomainStringTable> privacy_strings_;
  // §9.1. Consulted per request to decide whether the canonical name still
  // needs harvesting; a hit means no interception at all.
  scoped_refptr<zephyrus_privacy::PrivacyCnameCache> privacy_cname_cache_;
  // Kept alongside the memoized id so the name can be re-published on every
  // request without recomputing the eTLD+1. The string table sweeps names it
  // has not seen since the previous flush, so publishing once at first
  // sighting would let an active site's name expire underneath it.
  std::string site_etld1_;
  std::optional<uint32_t> site_id_;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_PROXYING_URL_LOADER_FACTORY_H_
