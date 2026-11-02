// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_PARAM_REDIRECT_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_PARAM_REDIRECT_H_

#include <stdint.h>

#include "base/functional/callback.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "url/gurl.h"

namespace zephyrus_adblock {

// Answers a request with a redirect to the same address minus its tracking
// parameters, without contacting the network, and then starts the real request
// for the cleaned address.
//
// To everything above it this is an ordinary 307: the page's history keeps one
// entry, the referrer and cookie rules are recomputed for the new URL, and a
// navigation, an image and an XHR all behave as they would after a server's own
// redirect. It is the same technique Chromium's extension webRequest proxy uses
// for an internal redirect.
//
// Owns itself: it lives until the client follows the redirect (when it hands the
// request on and goes away) or either end of the pipe closes.
class ZephyrusAdblockParamRedirect : public network::mojom::URLLoader {
 public:
  // Starts the real request: the proxying factory's StartLoader, bound to the
  // factory, so a factory that has gone away drops the request.
  using RestartCallback = base::OnceCallback<void(
      mojo::PendingReceiver<network::mojom::URLLoader>,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest&,
      mojo::PendingRemote<network::mojom::URLLoaderClient>,
      const net::MutableNetworkTrafficAnnotationTag&)>;

  // Sends the redirect to `client` and keeps the rest of the request until it is
  // followed. `cleaned_url` is `request.url` without its tracking parameters.
  static void Start(
      mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
      const GURL& cleaned_url,
      RestartCallback restart);

 private:
  ZephyrusAdblockParamRedirect(
      mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
      RestartCallback restart);
  ~ZephyrusAdblockParamRedirect() override;

  void SendRedirect(const GURL& cleaned_url);
  void DeleteSelf();

  // network::mojom::URLLoader:
  void FollowRedirect(
      network::HttpRequestHeadersUpdateParams headers_update_params,
      const std::optional<GURL>& new_url) override;
  void SetPriority(net::RequestPriority priority,
                   int32_t intra_priority_value) override;

  mojo::Receiver<network::mojom::URLLoader> receiver_{this};
  mojo::Remote<network::mojom::URLLoaderClient> client_;
  const int32_t request_id_;
  const uint32_t options_;
  network::ResourceRequest request_;
  net::MutableNetworkTrafficAnnotationTag traffic_annotation_;
  RestartCallback restart_;
  GURL cleaned_url_;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_PARAM_REDIRECT_H_
