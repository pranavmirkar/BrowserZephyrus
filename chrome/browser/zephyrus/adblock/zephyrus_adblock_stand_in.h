// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_STAND_IN_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_STAND_IN_H_

#include <stdint.h>

#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_redirect_resources.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_loader.mojom.h"

namespace zephyrus_adblock {

// Answers a request the lists block with a harmless stand-in (see
// adblock_redirect_resources.h) instead of an error, without contacting the
// network.
//
// The response is given under the ORIGINAL address, so the page sees the file it
// asked for and its Content-Security-Policy, which allowed that file, still
// holds. It is never cached (`no-store`): whether a stand-in is served depends
// on the lists and the allowlist, which can change. For a cross-origin request
// that the page made with CORS, the answer carries the headers that would let
// the page read it, and `response_type` says what the network stack would have
// said, so a `fetch()` of a stand-in resolves as one of the real file would.
//
// Owns itself: it lives until the client has taken the response and let go.
class ZephyrusAdblockStandIn : public network::mojom::URLLoader {
 public:
  // Sends the response to `client`. `type` is the kind of request (it picks the
  // content type of the `empty` stand-in).
  static void Start(
      mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const network::ResourceRequest& request,
      const RedirectResource& resource,
      ResourceType type);

 private:
  ZephyrusAdblockStandIn(
      mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client);
  ~ZephyrusAdblockStandIn() override;

  void Respond(const network::ResourceRequest& request,
               const RedirectResource& resource,
               ResourceType type);
  void DeleteSelf();

  // network::mojom::URLLoader:
  void FollowRedirect(
      network::HttpRequestHeadersUpdateParams headers_update_params,
      const std::optional<GURL>& new_url) override {}
  void SetPriority(net::RequestPriority priority,
                   int32_t intra_priority_value) override {}

  mojo::Receiver<network::mojom::URLLoader> receiver_{this};
  mojo::Remote<network::mojom::URLLoaderClient> client_;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_STAND_IN_H_
