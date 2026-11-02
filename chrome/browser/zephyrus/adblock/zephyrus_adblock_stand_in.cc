// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_stand_in.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/origin.h"

namespace zephyrus_adblock {

namespace {

// What the network stack's CORS layer would have called this response.
network::mojom::FetchResponseType TaintingFor(
    const network::ResourceRequest& request) {
  const bool same_origin =
      request.request_initiator &&
      request.request_initiator->IsSameOriginWith(request.url);
  switch (request.mode) {
    case network::mojom::RequestMode::kNavigate:
    case network::mojom::RequestMode::kSameOrigin:
      return network::mojom::FetchResponseType::kBasic;
    case network::mojom::RequestMode::kNoCors:
      return same_origin ? network::mojom::FetchResponseType::kBasic
                         : network::mojom::FetchResponseType::kOpaque;
    case network::mojom::RequestMode::kCors:
    case network::mojom::RequestMode::kCorsWithForcedPreflight:
      return same_origin ? network::mojom::FetchResponseType::kBasic
                         : network::mojom::FetchResponseType::kCors;
  }
  return network::mojom::FetchResponseType::kBasic;
}

}  // namespace

// static
void ZephyrusAdblockStandIn::Start(
    mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const network::ResourceRequest& request,
    const RedirectResource& resource,
    ResourceType type) {
  // Deletes itself; see DeleteSelf().
  auto* loader = new ZephyrusAdblockStandIn(std::move(loader_receiver),
                                            std::move(client));
  loader->Respond(request, resource, type);
}

ZephyrusAdblockStandIn::ZephyrusAdblockStandIn(
    mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client)
    : receiver_(this, std::move(loader_receiver)), client_(std::move(client)) {
  receiver_.set_disconnect_handler(
      base::BindOnce(&ZephyrusAdblockStandIn::DeleteSelf,
                     base::Unretained(this)));
  client_.set_disconnect_handler(
      base::BindOnce(&ZephyrusAdblockStandIn::DeleteSelf,
                     base::Unretained(this)));
}

ZephyrusAdblockStandIn::~ZephyrusAdblockStandIn() = default;

void ZephyrusAdblockStandIn::DeleteSelf() {
  delete this;
}

void ZephyrusAdblockStandIn::Respond(const network::ResourceRequest& request,
                                     const RedirectResource& resource,
                                     ResourceType type) {
  const std::string_view body = RedirectResourceBody(resource);
  const std::string mime(RedirectResourceMime(resource, type));

  std::string headers = base::StringPrintf(
      "HTTP/1.1 200 OK\n"
      "Content-Type: %s\n"
      "Content-Length: %zu\n"
      "Cache-Control: no-store\n"
      "X-Content-Type-Options: nosniff\n",
      mime.c_str(), body.size());
  const network::mojom::FetchResponseType tainting = TaintingFor(request);
  if (tainting == network::mojom::FetchResponseType::kCors &&
      request.request_initiator) {
    // What lets the page read the answer to a CORS request. With credentials the
    // origin must be named, not `*`.
    headers += base::StringPrintf(
        "Access-Control-Allow-Origin: %s\n"
        "Vary: Origin\n",
        request.request_initiator->Serialize().c_str());
    if (request.credentials_mode == network::mojom::CredentialsMode::kInclude) {
      headers += "Access-Control-Allow-Credentials: true\n";
    }
  }
  headers += "\n";

  auto head = network::mojom::URLResponseHead::New();
  head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
      net::HttpUtil::AssembleRawHeaders(headers));
  head->mime_type = mime;
  head->content_length = static_cast<int64_t>(body.size());
  head->encoded_data_length = 0;
  head->response_type = tainting;
  head->request_time = base::Time::Now();
  head->response_time = base::Time::Now();
  head->request_start = base::TimeTicks::Now();
  head->response_start = base::TimeTicks::Now();

  mojo::ScopedDataPipeProducerHandle producer;
  mojo::ScopedDataPipeConsumerHandle consumer;
  if (mojo::CreateDataPipe(std::max<size_t>(body.size(), 1), producer,
                           consumer) != MOJO_RESULT_OK) {
    client_->OnComplete(network::URLLoaderCompletionStatus(net::ERR_FAILED));
    return;
  }
  if (!body.empty()) {
    size_t written = 0;
    const MojoResult result = producer->WriteData(
        base::as_byte_span(body), MOJO_WRITE_DATA_FLAG_NONE, written);
    if (result != MOJO_RESULT_OK || written != body.size()) {
      client_->OnComplete(network::URLLoaderCompletionStatus(net::ERR_FAILED));
      return;
    }
  }
  producer.reset();  // the end of the body

  client_->OnReceiveResponse(std::move(head), std::move(consumer),
                             std::nullopt);
  network::URLLoaderCompletionStatus status(net::OK);
  status.encoded_data_length = static_cast<int64_t>(body.size());
  status.encoded_body_length = static_cast<int64_t>(body.size());
  status.decoded_body_length = static_cast<int64_t>(body.size());
  client_->OnComplete(status);
}

}  // namespace zephyrus_adblock
