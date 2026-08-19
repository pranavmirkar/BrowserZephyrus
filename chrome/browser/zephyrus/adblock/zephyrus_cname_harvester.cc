// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/zephyrus_cname_harvester.h"

#include <utility>

#include "base/functional/bind.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace zephyrus_adblock {

// static
mojo::PendingRemote<network::mojom::URLLoaderClient>
ZephyrusCnameHarvester::Create(
    mojo::PendingRemote<network::mojom::URLLoaderClient> real_client,
    scoped_refptr<zephyrus_privacy::PrivacyCnameCache> cache,
    std::string request_host) {
  auto* harvester = new ZephyrusCnameHarvester(
      std::move(real_client), std::move(cache), std::move(request_host));
  auto remote = harvester->receiver_.BindNewPipeAndPassRemote();
  // AFTER binding: mojo::Receiver::set_disconnect_handler DCHECKs is_bound().
  harvester->receiver_.set_disconnect_handler(base::BindOnce(
      &ZephyrusCnameHarvester::OnDisconnect, base::Unretained(harvester)));
  return remote;
}

ZephyrusCnameHarvester::ZephyrusCnameHarvester(
    mojo::PendingRemote<network::mojom::URLLoaderClient> real_client,
    scoped_refptr<zephyrus_privacy::PrivacyCnameCache> cache,
    std::string request_host)
    : real_client_(std::move(real_client)),
      cache_(std::move(cache)),
      request_host_(std::move(request_host)) {
  real_client_.set_disconnect_handler(base::BindOnce(
      &ZephyrusCnameHarvester::OnDisconnect, base::Unretained(this)));
}

ZephyrusCnameHarvester::~ZephyrusCnameHarvester() = default;

void ZephyrusCnameHarvester::OnDisconnect() {
  delete this;
}

void ZephyrusCnameHarvester::OnReceiveResponse(
    network::mojom::URLResponseHeadPtr head,
    mojo::ScopedDataPipeConsumerHandle body,
    std::optional<mojo_base::BigBuffer> cached_metadata) {
  if (cache_ && head) {
    // Recorded even when it comes back empty — "this host is not cloaked" is
    // the answer for almost every host, and remembering it is what keeps the
    // harvester off that host's future requests.
    cache_->Record(request_host_,
                   zephyrus_privacy::PrivacyCnameCache::CanonicalFromAliases(
                       request_host_, head->dns_aliases));
  }
  real_client_->OnReceiveResponse(std::move(head), std::move(body),
                                  std::move(cached_metadata));
}

void ZephyrusCnameHarvester::OnReceiveEarlyHints(
    network::mojom::EarlyHintsPtr early_hints) {
  real_client_->OnReceiveEarlyHints(std::move(early_hints));
}

void ZephyrusCnameHarvester::OnReceiveRedirect(
    const net::RedirectInfo& redirect_info,
    network::mojom::URLResponseHeadPtr head) {
  // A redirect is a new request to a new host, and gets its own harvester from
  // the factory if that host is unknown.
  real_client_->OnReceiveRedirect(redirect_info, std::move(head));
}

void ZephyrusCnameHarvester::OnUploadProgress(
    int64_t current_position,
    int64_t total_size,
    OnUploadProgressCallback callback) {
  real_client_->OnUploadProgress(current_position, total_size,
                                 std::move(callback));
}

void ZephyrusCnameHarvester::OnTransferSizeUpdated(int32_t transfer_size_diff) {
  real_client_->OnTransferSizeUpdated(transfer_size_diff);
}

void ZephyrusCnameHarvester::OnComplete(
    const network::URLLoaderCompletionStatus& status) {
  real_client_->OnComplete(status);
}

}  // namespace zephyrus_adblock
