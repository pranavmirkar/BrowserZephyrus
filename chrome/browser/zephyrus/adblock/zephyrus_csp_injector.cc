// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/zephyrus_csp_injector.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/parsed_headers.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace zephyrus_adblock {

// static
mojo::PendingRemote<network::mojom::URLLoaderClient>
ZephyrusCspInjector::Create(
    mojo::PendingRemote<network::mojom::URLLoaderClient> real_client,
    GURL url,
    std::vector<std::string> policies) {
  auto* injector = new ZephyrusCspInjector(std::move(real_client),
                                           std::move(url), std::move(policies));
  mojo::PendingRemote<network::mojom::URLLoaderClient> remote =
      injector->receiver_.BindNewPipeAndPassRemote();
  // After binding: set_disconnect_handler needs a bound receiver.
  injector->receiver_.set_disconnect_handler(base::BindOnce(
      &ZephyrusCspInjector::OnDisconnect, base::Unretained(injector)));
  return remote;
}

ZephyrusCspInjector::ZephyrusCspInjector(
    mojo::PendingRemote<network::mojom::URLLoaderClient> real_client,
    GURL url,
    std::vector<std::string> policies)
    : real_client_(std::move(real_client)),
      url_(std::move(url)),
      policies_(std::move(policies)) {
  real_client_.set_disconnect_handler(base::BindOnce(
      &ZephyrusCspInjector::OnDisconnect, base::Unretained(this)));
}

ZephyrusCspInjector::~ZephyrusCspInjector() = default;

void ZephyrusCspInjector::OnDisconnect() {
  if (disconnected_) {
    return;
  }
  disconnected_ = true;
  // Never `delete this` from inside a mojo callback of this object.
  base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                                this);
}

void ZephyrusCspInjector::OnReceiveResponse(
    network::mojom::URLResponseHeadPtr head,
    mojo::ScopedDataPipeConsumerHandle body,
    std::optional<mojo_base::BigBuffer> cached_metadata) {
  if (head && head->headers && !disconnected_) {
    for (const std::string& policy : policies_) {
      head->headers->AddHeader("Content-Security-Policy", policy);
    }
    // The browser reads the policy from the parsed form.
    head->parsed_headers =
        network::PopulateParsedHeaders(head->headers.get(), url_);
  }
  if (real_client_) {
    real_client_->OnReceiveResponse(std::move(head), std::move(body),
                                    std::move(cached_metadata));
  }
}

void ZephyrusCspInjector::OnReceiveEarlyHints(
    network::mojom::EarlyHintsPtr early_hints) {
  if (real_client_) {
    real_client_->OnReceiveEarlyHints(std::move(early_hints));
  }
}

void ZephyrusCspInjector::OnReceiveRedirect(
    const net::RedirectInfo& redirect_info,
    network::mojom::URLResponseHeadPtr head) {
  // The redirect target is not looked at again (the loader follows it without
  // asking the factory), so the first address's policies must not follow it.
  policies_.clear();
  if (real_client_) {
    real_client_->OnReceiveRedirect(redirect_info, std::move(head));
  }
}

void ZephyrusCspInjector::OnUploadProgress(int64_t current_position,
                                           int64_t total_size,
                                           OnUploadProgressCallback callback) {
  if (real_client_) {
    real_client_->OnUploadProgress(current_position, total_size,
                                   std::move(callback));
  }
}

void ZephyrusCspInjector::OnTransferSizeUpdated(int32_t transfer_size_diff) {
  if (real_client_) {
    real_client_->OnTransferSizeUpdated(transfer_size_diff);
  }
}

void ZephyrusCspInjector::OnComplete(
    const network::URLLoaderCompletionStatus& status) {
  if (real_client_) {
    real_client_->OnComplete(status);
  }
}

}  // namespace zephyrus_adblock
