// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_DEV_MODEL_CLIENT_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_DEV_MODEL_CLIENT_H_

#include <list>
#include <memory>
#include <optional>
#include <string>
#include "base/memory/weak_ptr.h"
#include "base/files/file_path.h"
#include <vector>

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

// Send the model a picture of the page as well as a description of it.
//
//   --zephyrus-agent-vision
//
// Off by default, and deliberately so. An image costs roughly ten times what
// the accessibility tree costs for the same page, and prompt size is what sets
// the wall-clock cost of a step -- measured at 43 seconds per step before the
// text was cut back, 9 after. Vision buys sight of what the tree gets wrong; it
// is not free, and it is not a replacement.
//
// Only useful with a model that can actually see. Pointing this at a text-only
// model wastes the bytes and, on some runtimes, the whole request.
inline constexpr char kAgentVisionSwitch[] = "zephyrus-agent-vision";

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
  // Loads the model without asking it anything.
  //
  // MEASURED on this machine, qwen2.5:7b: 19.2s for a trivial request with the
  // model cold, 2.5s with it resident. That 17 seconds was being paid AFTER the
  // user pressed send, because the client is built when a task starts -- so a
  // task appeared to take half a minute to begin while the browser sat waiting
  // for a 4.7GB file to come off disk.
  //
  // Called when the agent panel opens instead, which is the moment a person has
  // said "I am about to use this" and is still typing. The load then happens
  // beside them rather than in front of them.
  static void WarmUp(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);

  void Propose(const std::string& system_prompt,
               const std::string& user_prompt,
               ProposeCallback callback) override;

 private:
  // Replies read from a recording, replayed in order instead of asking a model.
  //
  // A model at temperature zero is repeatable in principle and not in practice:
  // it is a different process, on a machine whose load changes, behind a server
  // that may truncate a prompt. Debugging the HARNESS against it means every
  // change is tested against a moving input, which is how a week goes into
  // deciding whether a fix worked.
  //
  // With a recording the model is a constant. Change the parser, the prompt or
  // the executor, replay the exact session that failed, and any difference in
  // behaviour is yours. This is what the build plan meant by "deterministic
  // replay from day one, because debugging a non-replayable agent is
  // guesswork".
  std::vector<std::string> replies_;
  size_t next_reply_ = 0;
  bool replaying_ = false;

  // The recording is read off the disk, which cannot be done on this thread.
  //
  // A Propose that arrives before the file has loaded is held rather than
  // answered wrongly: answering "nothing recorded" because the read had not
  // finished would make replay depend on disk timing, and being independent of
  // timing is the entire point of it.
  bool loaded_ = false;
  std::vector<ProposeCallback> waiting_;

  void LoadRecording(const base::FilePath& path);
  void OnRecordingLoaded(std::vector<std::string> replies);
  void AnswerFromRecording(ProposeCallback callback);

 public:

 private:
  DevModelClient(const GURL& endpoint,
                 std::string model,
                 scoped_refptr<network::SharedURLLoaderFactory> factory);

  using LoaderList = std::list<std::unique_ptr<network::SimpleURLLoader>>;

  void OnResponse(LoaderList::iterator loader,
                  ProposeCallback callback,
                  std::string user_prompt,
                  std::optional<std::string> body);

  const GURL endpoint_;
  const std::string model_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  // Kept alive for the length of their requests. A SimpleURLLoader cancels
  // itself when destroyed, so losing the handle would silently drop the reply
  // and leave the loop waiting.
  LoaderList loaders_;


  mojo::ReceiverSet<mojom::AgentModel> receivers_;

  // Last member, as the style checker requires: everything above must still
  // exist when a pending callback runs.
  base::WeakPtrFactory<DevModelClient> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_DEV_MODEL_CLIENT_H_
