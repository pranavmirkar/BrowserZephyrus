// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/agent_kernel_client.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/service_process_host.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"

namespace zephyrus::agent {
namespace {

// What the browser assumes when it cannot ask.
//
// Named rather than inlined because this value is the failure mode of the whole
// feature: if the kernel is unreachable, this is the answer the agent gets, and
// it must never be kAllow.
mojom::PolicyDecisionPtr UnreachableKernelDecision() {
  auto decision = mojom::PolicyDecision::New();
  decision->disposition = mojom::Disposition::kDeny;
  decision->risk = "R3";
  decision->reason = "the agent is not available right now";
  return decision;
}

}  // namespace

AgentKernelClient::AgentKernelClient() = default;

AgentKernelClient::AgentKernelClient(
    mojo::PendingRemote<mojom::AgentKernel> kernel)
    : kernel_(std::move(kernel)) {
  kernel_.set_disconnect_handler(base::BindOnce(
      &AgentKernelClient::OnDisconnected, weak_factory_.GetWeakPtr()));
}
AgentKernelClient::~AgentKernelClient() = default;

void AgentKernelClient::EnsureLaunched() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (kernel_.is_bound()) {
    return;
  }
  content::ServiceProcessHost::Launch<mojom::AgentKernel>(
      kernel_.BindNewPipeAndPassReceiver(),
      content::ServiceProcessHost::Options()
          .WithDisplayName("Zephyrus Agent Kernel")
          .Pass());
  kernel_.set_disconnect_handler(base::BindOnce(
      &AgentKernelClient::OnDisconnected, weak_factory_.GetWeakPtr()));
}

void AgentKernelClient::OnDisconnected() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  kernel_.reset();
}

void AgentKernelClient::Decide(mojom::PolicyRequestPtr request,
                               DecideCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  EnsureLaunched();

  // Wrapping is what makes the "always answered" guarantee real. If the service
  // process dies with this call in flight, mojo destroys the reply callback
  // without running it, and an agent waiting on it would hang. The wrapper runs
  // it with a deny instead.
  auto safe_callback = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), UnreachableKernelDecision());

  kernel_->Decide(std::move(request), std::move(safe_callback));
}

}  // namespace zephyrus::agent
