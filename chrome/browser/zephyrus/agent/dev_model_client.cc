// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/dev_model_client.h"

#include <utility>

#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/ptr_util.h"
#include "base/values.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace zephyrus::agent {
namespace {

// A reply larger than this is not a tool call, it is a model that has started
// writing an essay. Capping it keeps a runaway generation from being copied
// into the browser's memory.
constexpr size_t kMaxResponseBytes = 64 * 1024;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("zephyrus_agent_dev_model", R"(
        semantics {
          sender: "Zephyrus Agent, development only"
          description:
            "Sends the agent's prompt, which includes a description of the "
            "page the user is looking at, to a model server running on the "
            "user's own machine, and reads back the tool call the model "
            "proposes. This exists so the agent can be developed and watched "
            "before an in-process model runtime exists."
          trigger:
            "Only when the browser was started with both "
            "--zephyrus-agent-model-endpoint and --zephyrus-agent-model, and "
            "the user starts an agent task."
          data:
            "The user's task text, and the browser's description of the "
            "active page: its URL, title, visible text and interactive "
            "elements."
          destination: LOCAL
          internal { contacts { email: "pranavmirkar@gmail.com" } }
          last_reviewed: "2026-09-02"
        }
        policy {
          cookies_allowed: NO
          setting:
            "Off unless the browser is started with the two development "
            "switches named above. There is no UI for it and it is not part "
            "of any shipping build's default behaviour."
          policy_exception_justification:
            "Development-only code path with no enterprise deployment."
        })");

}  // namespace

// static
std::unique_ptr<DevModelClient> DevModelClient::CreateIfConfigured(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory) {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch(kAgentModelEndpointSwitch) ||
      !command_line.HasSwitch(kAgentModelSwitch)) {
    return nullptr;
  }
  return Create(
      GURL(command_line.GetSwitchValueASCII(kAgentModelEndpointSwitch)),
      command_line.GetSwitchValueASCII(kAgentModelSwitch),
      std::move(url_loader_factory));
}

// static
std::unique_ptr<DevModelClient> DevModelClient::Create(
    const GURL& endpoint,
    std::string model,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory) {
  // The check that matters. The prompt carries the page the user is looking at,
  // and the endpoint comes from a command line, so a typo must not be able to
  // send it somewhere real.
  if (!endpoint.is_valid() || !endpoint.SchemeIsHTTPOrHTTPS() ||
      !net::IsLocalhost(endpoint)) {
    return nullptr;
  }
  if (model.empty() || !url_loader_factory) {
    return nullptr;
  }
  return base::WrapUnique(new DevModelClient(endpoint, std::move(model),
                                             std::move(url_loader_factory)));
}

DevModelClient::DevModelClient(
    const GURL& endpoint,
    std::string model,
    scoped_refptr<network::SharedURLLoaderFactory> factory)
    : endpoint_(endpoint),
      model_(std::move(model)),
      url_loader_factory_(std::move(factory)) {}

DevModelClient::~DevModelClient() = default;

mojo::PendingRemote<mojom::AgentModel>
DevModelClient::BindNewPipeAndPassRemote() {
  mojo::PendingRemote<mojom::AgentModel> remote;
  receivers_.Add(this, remote.InitWithNewPipeAndPassReceiver());
  return remote;
}

void DevModelClient::Propose(const std::string& system_prompt,
                             const std::string& user_prompt,
                             ProposeCallback callback) {
  base::ListValue messages;

  base::DictValue system;
  system.Set("role", "system");
  system.Set("content", system_prompt);
  messages.Append(std::move(system));

  base::DictValue user;
  user.Set("role", "user");
  user.Set("content", user_prompt);
  messages.Append(std::move(user));

  base::DictValue options;
  // Temperature zero because a browser that does something different each time
  // you ask it the same thing is not debuggable.
  options.Set("temperature", 0);
  options.Set("seed", 7);

  base::DictValue body;
  body.Set("model", model_);
  body.Set("stream", false);
  body.Set("messages", std::move(messages));
  body.Set("options", std::move(options));

  std::string json;
  if (!base::JSONWriter::Write(body, &json)) {
    std::move(callback).Run(std::string());
    return;
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = endpoint_.Resolve("/api/chat");
  request->method = "POST";
  // Nothing about this is a credentialed request, and the prompt should not
  // carry cookies for a site the user happens to be logged into.
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE;

  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->AttachStringForUpload(json, "application/json");
  network::SimpleURLLoader* raw_loader = loader.get();

  const LoaderList::iterator handle =
      loaders_.insert(loaders_.end(), std::move(loader));

  raw_loader->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&DevModelClient::OnResponse, base::Unretained(this),
                     handle, std::move(callback)),
      kMaxResponseBytes);
}

void DevModelClient::OnResponse(LoaderList::iterator loader,
                                ProposeCallback callback,
                                std::optional<std::string> body) {
  loaders_.erase(loader);

  // Every failure is an empty response rather than an error. The loop treats a
  // reply with no tool call in it as a wasted step and carries on, which is the
  // right thing to do about a model server that is not running.
  if (!body) {
    std::move(callback).Run(std::string());
    return;
  }

  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
  if (!parsed) {
    std::move(callback).Run(std::string());
    return;
  }

  const base::DictValue* message = parsed->FindDict("message");
  const std::string* content =
      message ? message->FindString("content") : nullptr;
  std::move(callback).Run(content ? *content : std::string());
}

}  // namespace zephyrus::agent
