// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_CSP_INJECTOR_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_CSP_INJECTOR_H_

#include <optional>
#include <string>
#include <vector>

#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "url/gurl.h"

namespace zephyrus_adblock {

// Adds Content-Security-Policy headers (`$csp=` rules, see AdblockCspFilter) to
// the response of ONE page or frame load, then passes everything on unchanged.
//
// Attached only to a request a rule matched -- about a hundred sites in the
// whole list -- so nothing else is routed through the browser process. The
// parsed form of the headers is rebuilt after the change, because the browser
// reads the policy from there and not from the header text.
//
// Self-owned; dies (a task later, never inside one of its own callbacks) when
// either pipe closes.
class ZephyrusCspInjector : public network::mojom::URLLoaderClient {
 public:
  static mojo::PendingRemote<network::mojom::URLLoaderClient> Create(
      mojo::PendingRemote<network::mojom::URLLoaderClient> real_client,
      GURL url,
      std::vector<std::string> policies);

  ZephyrusCspInjector(const ZephyrusCspInjector&) = delete;
  ZephyrusCspInjector& operator=(const ZephyrusCspInjector&) = delete;
  ~ZephyrusCspInjector() override;

  // network::mojom::URLLoaderClient:
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
  ZephyrusCspInjector(
      mojo::PendingRemote<network::mojom::URLLoaderClient> real_client,
      GURL url,
      std::vector<std::string> policies);

  void OnDisconnect();

  mojo::Receiver<network::mojom::URLLoaderClient> receiver_{this};
  mojo::Remote<network::mojom::URLLoaderClient> real_client_;
  const GURL url_;
  // Cleared at a redirect: the loader goes on to the next address without
  // asking the factory again, so the rules for the first one would otherwise
  // reach a page nobody matched.
  std::vector<std::string> policies_;
  bool disconnected_ = false;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_CSP_INJECTOR_H_
