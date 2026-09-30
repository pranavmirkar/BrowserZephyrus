// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_MODEL_TRANSPORT_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_MODEL_TRANSPORT_H_

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/containers/flat_map.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/services/zephyrus_agent/public/mojom/agent_kernel.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "url/gurl.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace zephyrus::agent {

// The browser's half of speaking to a cloud model: an authenticated pipe.
//
// ADR 0004. The kernel builds every request and reads every reply; this sends.
// It is the one place in the browser that holds a provider key while a task
// runs, so it is small on purpose and it trusts nothing it is given:
//
// - the host is the base URL the user saved, never anything the kernel says;
// - the path must be the exact shape of this provider's one endpoint;
// - only a fixed list of non-secret headers is accepted, and the auth header is
//   added here, after that check, so the kernel cannot set or read it;
// - no cookies or other credentials of any Workspace go with it, and nothing is
//   cached;
// - the reply is capped in size.
//
// A refusal is answered as status 0 with a JSON error body, which the kernel
// reads like any provider error. A network failure is -1, which the loop
// treats as transient and retries.
class ModelTransport : public mojom::ModelTransport {
 public:
  // `kind` is "anthropic", "openai" or "gemini". Returns null, having done
  // nothing, for anything this cannot send safely: an unknown kind, an empty
  // key where one is needed, or a base URL that is not https (plain http is
  // allowed only to this machine, for a local OpenAI-compatible server).
  static std::unique_ptr<ModelTransport> Create(
      const std::string& kind,
      const GURL& base_url,
      std::string api_key,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);

  ~ModelTransport() override;

  ModelTransport(const ModelTransport&) = delete;
  ModelTransport& operator=(const ModelTransport&) = delete;

  mojo::PendingRemote<mojom::ModelTransport> BindNewPipeAndPassRemote();

  // mojom::ModelTransport:
  void Send(const std::string& path,
            const base::flat_map<std::string, std::string>& headers,
            const std::string& body,
            SendCallback callback) override;

  // Exposed for tests: whether `path` is this provider's endpoint.
  bool IsAllowedPath(const std::string& path) const;

  // The largest request and reply this sends or reads.
  static constexpr size_t kMaxRequestBytes = 2 * 1024 * 1024;
  static constexpr size_t kMaxReplyBytes = 4 * 1024 * 1024;

 private:
  ModelTransport(std::string kind,
                 const GURL& base_url,
                 std::string api_key,
                 scoped_refptr<network::SharedURLLoaderFactory> factory);

  void OnDone(std::unique_ptr<network::SimpleURLLoader> loader,
              SendCallback callback,
              std::optional<std::string> body);

  const std::string kind_;
  const GURL base_url_;
  const std::string api_key_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  mojo::ReceiverSet<mojom::ModelTransport> receivers_;
  base::WeakPtrFactory<ModelTransport> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_MODEL_TRANSPORT_H_
