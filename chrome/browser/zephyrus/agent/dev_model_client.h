// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_DEV_MODEL_CLIENT_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_DEV_MODEL_CLIENT_H_

#include <list>
#include <memory>
#include <optional>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "chrome/services/zephyrus_agent/public/mojom/agent_kernel.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"

namespace network {
class SimpleURLLoader;
}  // namespace network

namespace zephyrus::agent {

// Switch names live here rather than in chrome/common/chrome_switches.cc so
// this costs no upstream patch at all. Nothing outside this file needs them.
//
//   --zephyrus-agent-model-endpoint=http://127.0.0.1:11434
//   --zephyrus-agent-model=qwen2.5:7b
inline constexpr char kAgentModelEndpointSwitch[] =
    "zephyrus-agent-model-endpoint";
inline constexpr char kAgentModelSwitch[] = "zephyrus-agent-model";

// A development stand-in for the model that does not exist yet.
//
// Talks to a local Ollama-compatible server so the whole agent -- kernel,
// policy, Observation, executor, loop, approval -- can be watched running
// against real pages before there is an in-process inference runtime. It is not
// the shipping path and is not meant to become one.
//
// **It only ever talks to loopback.** The prompt it sends contains the page the
// user is looking at, and an endpoint switch is a string on a command line; a
// typo or a copied line must not be able to turn "help me debug the agent" into
// sending someone's browsing to a host on the internet. This is the same rule
// the benchmark harness enforces, for the same reason, and it is checked here
// rather than trusted to whoever wrote the flag.
class DevModelClient : public mojom::AgentModel {
 public:
  // Null unless both switches are present AND the endpoint is loopback.
  //
  // Returning null is the ordinary case: with no flags there is no dev model,
  // and the task controller reports that no model is configured.
  static std::unique_ptr<DevModelClient> CreateIfConfigured(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);

  // Builds one directly, skipping the command line. Used by tests and by
  // CreateIfConfigured. Returns null if `endpoint` is not loopback.
  static std::unique_ptr<DevModelClient> Create(
      const GURL& endpoint,
      std::string model,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);

  ~DevModelClient() override;

  DevModelClient(const DevModelClient&) = delete;
  DevModelClient& operator=(const DevModelClient&) = delete;

  mojo::PendingRemote<mojom::AgentModel> BindNewPipeAndPassRemote();

  // mojom::AgentModel:
  void Propose(const std::string& system_prompt,
               const std::string& user_prompt,
               ProposeCallback callback) override;

 private:
  DevModelClient(const GURL& endpoint,
                 std::string model,
                 scoped_refptr<network::SharedURLLoaderFactory> factory);

  using LoaderList = std::list<std::unique_ptr<network::SimpleURLLoader>>;

  void OnResponse(LoaderList::iterator loader,
                  ProposeCallback callback,
                  std::optional<std::string> body);

  const GURL endpoint_;
  const std::string model_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  // Kept alive for the length of their requests. A SimpleURLLoader cancels
  // itself when destroyed, so losing the handle would silently drop the reply
  // and leave the loop waiting.
  LoaderList loaders_;

  mojo::ReceiverSet<mojom::AgentModel> receivers_;
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_DEV_MODEL_CLIENT_H_
