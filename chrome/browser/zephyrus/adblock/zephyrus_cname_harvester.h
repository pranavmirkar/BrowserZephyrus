// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_CNAME_HARVESTER_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_CNAME_HARVESTER_H_

#include <string>

#include "base/memory/scoped_refptr.h"
#include "chrome/browser/zephyrus/privacy/privacy_cname_cache.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/mojom/url_loader.mojom.h"

namespace zephyrus_adblock {

// Reads the canonical name off ONE response and records it, then gets out of
// the way (§9.1).
//
// **Attached only on a cache miss.** The canonical name is visible nowhere but
// URLResponseHead::dns_aliases, so learning it costs one interception — but it
// changes about once per host per session, so paying that cost per request
// would be absurd. PrivacyCnameCache::NeedsResolution() gates this, and once a
// host is known (cloaked or not) nothing is attached to its requests again.
// The steady state is no interception at all.
//
// An earlier design put an interceptor on every unblocked request to observe
// completion. That routed responses which would go straight from the network
// service to the renderer through the BROWSER process, and woke it on every
// OnTransferSizeUpdated. Completion now comes from
// PrivacyTabHelper::ResourceLoadComplete instead, and this narrower object is
// all that remains — on a small and shrinking fraction of requests.
//
// Does not classify, block, or record any privacy event. It writes one string
// into a cache; everything downstream reads that cache later.
class ZephyrusCnameHarvester : public network::mojom::URLLoaderClient {
 public:
  // Returns a client handle to hand to the loader in place of `real_client`.
  // Self-owned; dies with either pipe.
  static mojo::PendingRemote<network::mojom::URLLoaderClient> Create(
      mojo::PendingRemote<network::mojom::URLLoaderClient> real_client,
      scoped_refptr<zephyrus_privacy::PrivacyCnameCache> cache,
      std::string request_host);

  ZephyrusCnameHarvester(const ZephyrusCnameHarvester&) = delete;
  ZephyrusCnameHarvester& operator=(const ZephyrusCnameHarvester&) = delete;
  ~ZephyrusCnameHarvester() override;

  // network::mojom::URLLoaderClient: all forward unchanged; only
  // OnReceiveResponse does any work of its own.
  void OnReceiveEarlyHints(network::mojom::EarlyHintsPtr early_hints) override;
  void OnReceiveResponse(
      network::mojom::URLResponseHeadPtr head,
      mojo::ScopedDataPipeConsumerHandle body,
      std::optional<mojo_base::BigBuffer> cached_metadata) override;
  void OnReceiveRedirect(const net::RedirectInfo& redirect_info,
                         network::mojom::URLResponseHeadPtr head) override;
  void OnUploadProgress(int64_t current_position,
                        int64_t total_size,
                        OnUploadProgressCallback callback) override;
  void OnTransferSizeUpdated(int32_t transfer_size_diff) override;
  void OnComplete(const network::URLLoaderCompletionStatus& status) override;

 private:
  ZephyrusCnameHarvester(
      mojo::PendingRemote<network::mojom::URLLoaderClient> real_client,
      scoped_refptr<zephyrus_privacy::PrivacyCnameCache> cache,
      std::string request_host);

  void OnDisconnect();

  mojo::Receiver<network::mojom::URLLoaderClient> receiver_{this};
  mojo::Remote<network::mojom::URLLoaderClient> real_client_;
  scoped_refptr<zephyrus_privacy::PrivacyCnameCache> cache_;
  const std::string request_host_;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_CNAME_HARVESTER_H_
