// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/model_transport.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/zephyrus/agent/dev_model_client.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace zephyrus::agent {
namespace {

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("zephyrus_agent_cloud_model", R"(
        semantics {
          sender: "Zephyrus Agent"
          description:
            "Sends one step of an agent task to the cloud AI model the user "
            "connected (Anthropic, an OpenAI-compatible service, or Google "
            "Gemini) and reads back the action the model proposes."
          trigger:
            "Only while the user is running an agent task, in a Workspace "
            "where the user has turned cloud models on."
          data:
            "The user's task text and the browser's description of the active "
            "page in that Workspace: its URL, title, visible text and "
            "interactive elements. Fields the browser marks sensitive, such as "
            "passwords and card numbers, are withheld. The user's API key is "
            "sent to the provider as that provider requires."
          destination: OTHER
          destination_other: "The model provider the user configured."
          internal { contacts { email: "pranavmirkar@gmail.com" } }
          user_data { type: WEB_CONTENT type: USER_CONTENT }
          last_reviewed: "2026-09-28"
        }
        policy {
          cookies_allowed: NO
          setting:
            "Off until the user adds a model in the agent settings and turns "
            "cloud models on for a Workspace."
          policy_exception_justification:
            "No enterprise policy yet; the feature is off by default."
        })");

// Status the kernel reads as a refusal (not retried) and as a network failure
// (retried). See mojom::ModelTransport.
constexpr int kRefused = 0;
constexpr int kNetworkFailure = -1;

std::string ErrorBody(const std::string& message) {
  base::DictValue error;
  error.Set("message", message);
  base::DictValue body;
  body.Set("error", std::move(error));
  return base::WriteJson(body).value_or("{}");
}

// The only headers the kernel may ask for. Everything that could carry a
// credential is added below, by the browser, and is not on this list.
bool IsAllowedHeader(const std::string& name) {
  const std::string lowered = base::ToLowerASCII(name);
  return lowered == "content-type" || lowered == "anthropic-version";
}

bool IsModelIdChar(char c) {
  return base::IsAsciiAlphaNumeric(c) || c == '.' || c == '-' || c == '_' ||
         c == ':';
}

// The prompt inside a provider request body, for the trace: the part a person
// debugging a run needs, without the tool listing repeated on every step.
std::string PromptFromBody(const std::string& body) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return body;
  }
  const base::DictValue& dict = parsed->GetDict();
  // Anthropic: messages[0].content. OpenAI: the last message's content.
  // Gemini: contents[0].parts[0].text.
  if (const base::ListValue* messages = dict.FindList("messages")) {
    if (!messages->empty() && messages->back().is_dict()) {
      const base::DictValue& last = messages->back().GetDict();
      if (const std::string* text = last.FindString("content")) {
        return *text;
      }
      // [image, text]: the text, and a note that a picture went too, never
      // the picture's bytes.
      if (const base::ListValue* parts = last.FindList("content")) {
        for (const base::Value& part : *parts) {
          if (part.is_dict()) {
            if (const std::string* text = part.GetDict().FindString("text")) {
              return "[with screenshot]\n" + *text;
            }
          }
        }
      }
    }
  }
  if (const base::ListValue* contents = dict.FindList("contents")) {
    if (!contents->empty() && contents->front().is_dict()) {
      if (const base::ListValue* parts =
              contents->front().GetDict().FindList("parts")) {
        for (const base::Value& part : *parts) {
          if (part.is_dict()) {
            if (const std::string* text = part.GetDict().FindString("text")) {
              return (parts->size() > 1 ? "[with screenshot]\n" : "") + *text;
            }
          }
        }
      }
    }
  }
  return body;
}

}  // namespace

// static
std::unique_ptr<ModelTransport> ModelTransport::Create(
    const std::string& kind,
    const GURL& base_url,
    std::string api_key,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory) {
  if (kind != "anthropic" && kind != "openai" && kind != "gemini") {
    return nullptr;
  }
  if (!url_loader_factory || !base_url.is_valid() || base_url.has_query() ||
      base_url.has_ref() || base_url.has_username() ||
      base_url.has_password()) {
    return nullptr;
  }
  // Page content and a key go over this. https everywhere; plain http only to
  // this machine, which is where a local OpenAI-compatible server lives.
  const bool local = net::IsLocalhost(base_url);
  if (!base_url.SchemeIs("https") && !(base_url.SchemeIs("http") && local)) {
    return nullptr;
  }
  if (api_key.empty() && !(kind == "openai" && local)) {
    return nullptr;
  }
  // A key with a line break could smuggle a second header.
  if (api_key.find_first_of("\r\n") != std::string::npos) {
    return nullptr;
  }
  return base::WrapUnique(new ModelTransport(
      kind, base_url, std::move(api_key), std::move(url_loader_factory)));
}

ModelTransport::ModelTransport(
    std::string kind,
    const GURL& base_url,
    std::string api_key,
    scoped_refptr<network::SharedURLLoaderFactory> factory)
    : kind_(std::move(kind)),
      base_url_(base_url),
      api_key_(std::move(api_key)),
      url_loader_factory_(std::move(factory)) {}

ModelTransport::~ModelTransport() = default;

mojo::PendingRemote<mojom::ModelTransport>
ModelTransport::BindNewPipeAndPassRemote() {
  mojo::PendingRemote<mojom::ModelTransport> remote;
  receivers_.Add(this, remote.InitWithNewPipeAndPassReceiver());
  return remote;
}

bool ModelTransport::IsAllowedPath(const std::string& path) const {
  if (kind_ == "anthropic") {
    return path == "/v1/messages";
  }
  if (kind_ == "openai") {
    return path == "/chat/completions";
  }
  // gemini: /v1beta/models/<model id>:generateContent, and nothing that could
  // step out of that path.
  constexpr char kPrefix[] = "/v1beta/models/";
  constexpr char kSuffix[] = ":generateContent";
  if (!base::StartsWith(path, kPrefix) || !base::EndsWith(path, kSuffix)) {
    return false;
  }
  const std::string model = path.substr(
      sizeof(kPrefix) - 1, path.size() - (sizeof(kPrefix) - 1) -
                               (sizeof(kSuffix) - 1));
  if (model.empty() || model.find("..") != std::string::npos) {
    return false;
  }
  for (char c : model) {
    if (!IsModelIdChar(c)) {
      return false;
    }
  }
  return true;
}

void ModelTransport::Send(
    const std::string& path,
    const base::flat_map<std::string, std::string>& headers,
    const std::string& body,
    SendCallback callback) {
  if (!IsAllowedPath(path)) {
    std::move(callback).Run(
        kRefused, ErrorBody("refused: that is not this provider's endpoint"));
    return;
  }
  if (body.size() > kMaxRequestBytes) {
    std::move(callback).Run(kRefused,
                            ErrorBody("refused: the request is too large"));
    return;
  }

  auto request = std::make_unique<network::ResourceRequest>();
  // Appended to the user's base URL, so a base of https://host/v1 and a path of
  // /chat/completions reach https://host/v1/chat/completions. The path has
  // already been checked to be exactly one of the known shapes.
  std::string base = base_url_.spec();
  if (base.ends_with('/')) {
    base.pop_back();
  }
  request->url = GURL(base + path);
  if (!request->url.is_valid() ||
      request->url.host() != base_url_.host() ||
      request->url.scheme() != base_url_.scheme()) {
    std::move(callback).Run(kRefused, ErrorBody("refused: bad address"));
    return;
  }
  request->method = "POST";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE;

  for (const auto& [name, value] : headers) {
    if (!IsAllowedHeader(name) ||
        value.find_first_of("\r\n") != std::string::npos) {
      std::move(callback).Run(
          kRefused, ErrorBody("refused: a header that is not allowed"));
      return;
    }
    request->headers.SetHeader(name, value);
  }

  // Authentication, added last and only here.
  if (!api_key_.empty()) {
    if (kind_ == "anthropic") {
      request->headers.SetHeader("x-api-key", api_key_);
    } else if (kind_ == "gemini") {
      request->headers.SetHeader("x-goog-api-key", api_key_);
    } else {
      request->headers.SetHeader("Authorization", "Bearer " + api_key_);
    }
  }

  // The key is never in the trace: it is only in the request headers, and
  // only the prompt from the body is written.
  AgentTrace(base::StrCat({"\n================ PROMPT (", kind_,
                           ") ================\n", PromptFromBody(body),
                           "\n"}));

  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  // A thinking model can take minutes over one step; a timeout here reads to
  // the loop as a network failure and is retried.
  loader->SetTimeoutDuration(base::Seconds(300));
  // A provider's error reply is the useful part of an error: the kernel words
  // it for the user. Without this the body of a 401 is thrown away.
  loader->SetAllowHttpErrorResults(true);
  loader->AttachStringForUpload(body, "application/json");

  network::SimpleURLLoader* raw = loader.get();
  raw->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&ModelTransport::OnDone, weak_factory_.GetWeakPtr(),
                     std::move(loader), std::move(callback)),
      kMaxReplyBytes);
}

void ModelTransport::OnDone(std::unique_ptr<network::SimpleURLLoader> loader,
                            SendCallback callback,
                            std::optional<std::string> body) {
  int status = kNetworkFailure;
  if (loader->ResponseInfo() && loader->ResponseInfo()->headers) {
    status = loader->ResponseInfo()->headers->response_code();
  }
  if (!body) {
    // No body: either the network failed, or the reply was over the cap.
    if (loader->NetError() == net::ERR_INSUFFICIENT_RESOURCES) {
      std::move(callback).Run(
          kRefused, ErrorBody("refused: the model's reply was too large"));
      return;
    }
    std::move(callback).Run(
        status > 0 ? status : kNetworkFailure,
        ErrorBody("network error: " + net::ErrorToShortString(loader->NetError())));
    return;
  }
  AgentTrace(base::StrCat({"---------------- REPLY HTTP ",
                           base::NumberToString(status),
                           " ----------------\n", *body, "\n"}));
  std::move(callback).Run(status > 0 ? status : kNetworkFailure,
                          std::move(*body));
}

}  // namespace zephyrus::agent
