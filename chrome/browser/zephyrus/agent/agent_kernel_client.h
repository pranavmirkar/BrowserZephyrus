// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_AGENT_KERNEL_CLIENT_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_AGENT_KERNEL_CLIENT_H_

#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "chrome/services/zephyrus_agent/public/mojom/agent_kernel.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace zephyrus::agent {

// The browser's handle on the agent kernel, which runs in a sandboxed utility
// process.
//
// Owns the service's lifetime: the process starts on the first call and stays
// up until this object is destroyed or the service dies. Callers never see the
// process.
//
// The contract this class exists to guarantee is that **every request is
// answered**. A service that crashes mid-call would otherwise drop the reply
// callback on the floor, and an agent loop waiting on a reply that will never
// arrive is stuck rather than safe. Any call that cannot reach the kernel
// completes with kDeny instead.
class AgentKernelClient {
 public:
  using DecideCallback = base::OnceCallback<void(mojom::PolicyDecisionPtr)>;

  AgentKernelClient();

  // Test-only. Binds to an already-running kernel instead of launching a
  // service process, so tests can drive the real policy engine in-process
  // rather than a fake of it. A fake kernel would test the executor against
  // rules nobody ships.
  explicit AgentKernelClient(mojo::PendingRemote<mojom::AgentKernel> kernel);

  ~AgentKernelClient();

  AgentKernelClient(const AgentKernelClient&) = delete;
  AgentKernelClient& operator=(const AgentKernelClient&) = delete;

  // Judges one proposed tool call. `callback` always runs exactly once, on the
  // calling sequence.
  void Decide(mojom::PolicyRequestPtr request, DecideCallback callback);

  // True if a service process is currently bound. Test-facing: production code
  // has no reason to care, because Decide() launches on demand.
  bool IsRunningForTesting() const { return kernel_.is_bound(); }

 private:
  // Starts the service if it is not already running.
  void EnsureLaunched();

  // Drops the remote so the next Decide() starts a fresh process. A crashed
  // kernel is recoverable; a permanently wedged one is not, and retrying is
  // both cheap and the only way back.
  void OnDisconnected();

  // mojo::Remote is sequence-affine and its own DCHECKs are compiled out of
  // our official builds, so the constraint is stated here instead of being
  // enforced only in configurations we do not ship.
  SEQUENCE_CHECKER(sequence_checker_);

  mojo::Remote<mojom::AgentKernel> kernel_ GUARDED_BY_CONTEXT(sequence_checker_);
  base::WeakPtrFactory<AgentKernelClient> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_AGENT_KERNEL_CLIENT_H_
