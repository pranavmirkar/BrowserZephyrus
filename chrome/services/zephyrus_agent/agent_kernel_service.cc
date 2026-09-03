// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/services/zephyrus_agent/agent_kernel_service.h"

#include <string>
#include <utility>

#include "base/numerics/safe_conversions.h"
#include "chrome/services/zephyrus_agent/task_loop.h"

namespace zephyrus::agent {
namespace {

mojom::Disposition ToMojo(Disposition disposition) {
  switch (disposition) {
    case Disposition::Allow:
      return mojom::Disposition::kAllow;
    case Disposition::Ask:
      return mojom::Disposition::kAsk;
    case Disposition::Deny:
      return mojom::Disposition::kDeny;
  }
  // A value outside the enum can only mean the two sides disagree about the
  // contract, which is not a situation to guess in.
  return mojom::Disposition::kDeny;
}

}  // namespace

AgentKernelService::AgentKernelService(
    mojo::PendingReceiver<mojom::AgentKernel> receiver)
    : receiver_(this, std::move(receiver)), kernel_(load_kernel()) {}

AgentKernelService::~AgentKernelService() = default;

void AgentKernelService::GetContractInfo(GetContractInfoCallback callback) {
  std::move(callback).Run(std::string(kernel_->contract_version()),
                          base::saturated_cast<uint32_t>(kernel_->tool_count()));
}

void AgentKernelService::Decide(mojom::PolicyRequestPtr request,
                                DecideCallback callback) {
  PolicyRequest bridged;
  bridged.tool = ::rust::String(request->tool);
  bridged.arguments_json = ::rust::String(request->arguments_json);
  bridged.task = ::rust::String(request->task);
  bridged.url = ::rust::String(request->url);
  bridged.elements.reserve(request->elements.size());
  for (const mojom::ObservedElementPtr& element : request->elements) {
    ObservedElement bridged_element;
    bridged_element.id = ::rust::String(element->id);
    bridged_element.role = ::rust::String(element->role);
    bridged_element.name = ::rust::String(element->name);
    bridged.elements.push_back(std::move(bridged_element));
  }

  const PolicyDecision decision = kernel_->decide(bridged);

  auto result = mojom::PolicyDecision::New();
  result->disposition = ToMojo(decision.disposition);
  result->risk = std::string(decision.risk);
  result->reason = std::string(decision.reason);
  std::move(callback).Run(std::move(result));
}

void AgentKernelService::RunTask(
    const std::string& task,
    mojo::PendingRemote<mojom::ToolRunner> runner,
    mojo::PendingRemote<mojom::AgentModel> model,
    uint32_t max_steps,
    mojom::PendingApprovalPtr approved,
    RunTaskCallback callback) {
  // A kernel with no contract has no rules, and a loop with no rules is not
  // something to start.
  if (!kernel_->is_valid()) {
    auto outcome = mojom::TaskOutcome::New();
    outcome->status = mojom::TaskStatus::kFailed;
    outcome->message = "the agent is not available";
    outcome->steps = 0;
    std::move(callback).Run(std::move(outcome));
    return;
  }

  TaskLoop::Start(*kernel_, task, std::move(runner), std::move(model),
                  max_steps, std::move(approved), std::move(callback));
}

}  // namespace zephyrus::agent
