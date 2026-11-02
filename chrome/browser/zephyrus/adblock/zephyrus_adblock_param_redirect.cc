// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_param_redirect.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/http/http_util.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/http_request_headers_update_params.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace zephyrus_adblock {

// static
void ZephyrusAdblockParamRedirect::Start(
    mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
    int32_t request_id,
    uint32_t options,
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
    const GURL& cleaned_url,
    RestartCallback restart) {
  // Deletes itself, see DeleteSelf().
  auto* redirect = new ZephyrusAdblockParamRedirect(
      std::move(loader_receiver), request_id, options, request,
      std::move(client), traffic_annotation, std::move(restart));
  redirect->SendRedirect(cleaned_url);
}

ZephyrusAdblockParamRedirect::ZephyrusAdblockParamRedirect(
    mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
    int32_t request_id,
    uint32_t options,
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
    RestartCallback restart)
    : request_id_(request_id),
      options_(options),
      request_(request),
      traffic_annotation_(traffic_annotation),
      restart_(std::move(restart)) {
  receiver_.Bind(std::move(loader_receiver));
  receiver_.set_disconnect_handler(
      base::BindOnce(&ZephyrusAdblockParamRedirect::DeleteSelf,
                     base::Unretained(this)));
  client_.Bind(std::move(client));
  client_.set_disconnect_handler(
      base::BindOnce(&ZephyrusAdblockParamRedirect::DeleteSelf,
                     base::Unretained(this)));
}

ZephyrusAdblockParamRedirect::~ZephyrusAdblockParamRedirect() = default;

void ZephyrusAdblockParamRedirect::DeleteSelf() {
  delete this;
}

void ZephyrusAdblockParamRedirect::SendRedirect(const GURL& cleaned_url) {
  cleaned_url_ = cleaned_url;
  constexpr int kStatus = net::HTTP_TEMPORARY_REDIRECT;
  net::RedirectInfo redirect_info = net::RedirectInfo::ComputeRedirectInfo(
      request_.method, request_.url, request_.site_for_cookies,
      request_.update_first_party_url_on_redirect
          ? net::RedirectInfo::FirstPartyURLPolicy::UPDATE_URL_ON_REDIRECT
          : net::RedirectInfo::FirstPartyURLPolicy::NEVER_CHANGE_URL,
      request_.referrer_policy, request_.referrer.spec(),
      request_.request_initiator, kStatus, cleaned_url_,
      /*referrer_policy_header=*/std::nullopt,
      /*insecure_scheme_was_upgraded=*/false, /*copy_fragment=*/false,
      /*is_signed_exchange_fallback_redirect=*/false);

  auto head = network::mojom::URLResponseHead::New();
  const std::string headers = base::StringPrintf(
      "HTTP/1.1 %i Internal Redirect\n"
      "Location: %s\n"
      "Non-Authoritative-Reason: Zephyrus tracking parameter removal\n\n",
      kStatus, cleaned_url_.spec().c_str());
  head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
      net::HttpUtil::AssembleRawHeaders(headers));
  head->encoded_data_length = 0;
  // The redirect comes from the browser itself, not from the site, so the
  // renderer's checks on a server's redirect do not apply.
  head->bypass_redirect_checks = true;
  redirect_info_ = redirect_info;
  client_->OnReceiveRedirect(redirect_info, std::move(head));
}

void ZephyrusAdblockParamRedirect::FollowRedirect(
    network::HttpRequestHeadersUpdateParams headers_update_params,
    const std::optional<GURL>& new_url) {
  // The cleaned address is what was asked for; the real request for it starts
  // now. Whatever the client asked to change about the headers is applied.
  // Everything a redirect changes about a request, as the network stack would
  // have done it: the address, the cookie site, the referrer and, for a page
  // load, the isolation info. For a link that skips to another site this is
  // what keeps the destination's cookies and cache its own and the referrer
  // sanitised; for a same-site rewrite it changes nothing.
  request_.UpdateOnRedirect(redirect_info_);
  if (new_url) {
    request_.url = *new_url;
  }
  for (const std::string& header : headers_update_params.removed_headers) {
    request_.headers.RemoveHeader(header);
  }
  request_.headers.MergeFrom(headers_update_params.modified_headers);
  request_.cors_exempt_headers.MergeFrom(
      headers_update_params.modified_cors_exempt_headers);

  mojo::PendingReceiver<network::mojom::URLLoader> receiver =
      receiver_.Unbind();
  mojo::PendingRemote<network::mojom::URLLoaderClient> client =
      client_.Unbind();
  RestartCallback restart = std::move(restart_);
  const int32_t request_id = request_id_;
  const uint32_t options = options_;
  const network::ResourceRequest request = request_;
  const net::MutableNetworkTrafficAnnotationTag annotation = traffic_annotation_;
  DeleteSelf();
  std::move(restart).Run(std::move(receiver), request_id, options, request,
                         std::move(client), annotation);
}

void ZephyrusAdblockParamRedirect::SetPriority(net::RequestPriority priority,
                                               int32_t intra_priority_value) {
  // Nothing is loading yet; the real request starts at its own priority.
}

}  // namespace zephyrus_adblock
