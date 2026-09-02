// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/tool_runner_impl.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"

namespace zephyrus::agent {
namespace {

mojom::ToolStatus ToMojo(ToolExecutor::Result::Status status) {
  switch (status) {
    case ToolExecutor::Result::Status::kOk:
      return mojom::ToolStatus::kOk;
    case ToolExecutor::Result::Status::kNeedsApproval:
      return mojom::ToolStatus::kNeedsApproval;
    case ToolExecutor::Result::Status::kDenied:
      return mojom::ToolStatus::kDenied;
    case ToolExecutor::Result::Status::kFailed:
      return mojom::ToolStatus::kFailed;
  }
  return mojom::ToolStatus::kFailed;
}

mojom::ToolOutcomePtr ToMojo(ToolExecutor::Result result) {
  auto outcome = mojom::ToolOutcome::New();
  outcome->status = ToMojo(result.status);
  outcome->message = std::move(result.message);
  outcome->value_json = std::move(result.value_json);
  outcome->risk = std::move(result.risk);
  return outcome;
}

}  // namespace

ToolRunnerImpl::ToolRunnerImpl(ToolExecutor* executor, std::string task)
    : executor_(executor), task_(std::move(task)) {
  CHECK(executor_);
}

ToolRunnerImpl::~ToolRunnerImpl() = default;

mojo::PendingRemote<mojom::ToolRunner>
ToolRunnerImpl::BindNewPipeAndPassRemote() {
  return receiver_.BindNewPipeAndPassRemote();
}

void ToolRunnerImpl::Observe(int32_t level, ObserveCallback callback) {
  if (observer_) {
    observer_->OnAgentLooked();
  }
  // Looking at the page is itself a tool, so it goes the same way as every
  // other one. page.observe is R0 and always allowed; routing it through the
  // executor anyway means there is no second path into the browser to audit.
  executor_->Execute(
      "page.observe",
      base::StrCat({"{\"level\":", base::NumberToString(level), "}"}), task_,
      base::BindOnce(
          [](ObserveCallback done, ToolExecutor::Result result) {
            // An Observation the browser could not take is an empty one, not an
            // error. It offers nothing, so every element id the model could
            // name is ungrounded and gets refused on the next call.
            std::move(done).Run(result.status ==
                                        ToolExecutor::Result::Status::kOk
                                    ? std::move(result.value_json)
                                    : std::string("{}"));
          },
          std::move(callback)));
}

void ToolRunnerImpl::Execute(const std::string& tool,
                             const std::string& arguments_json,
                             ExecuteCallback callback) {
  if (observer_) {
    observer_->OnAgentToolStarted(tool, arguments_json);
  }
  executor_->Execute(
      tool, arguments_json, task_,
      base::BindOnce(&ToolRunnerImpl::OnFinished, weak_factory_.GetWeakPtr(),
                     tool, std::move(callback)));
}

void ToolRunnerImpl::OnFinished(std::string tool,
                                ExecuteCallback callback,
                                ToolExecutor::Result result) {
  mojom::ToolOutcomePtr outcome = ToMojo(std::move(result));
  if (observer_) {
    observer_->OnAgentToolFinished(tool, *outcome);
  }
  std::move(callback).Run(std::move(outcome));
}

void ToolRunnerImpl::ExecuteApproved(const std::string& tool,
                                     const std::string& arguments_json,
                                     ExecuteApprovedCallback callback) {
  if (observer_) {
    observer_->OnAgentToolStarted(tool, arguments_json);
  }
  executor_->ExecuteApproved(
      tool, arguments_json, task_,
      base::BindOnce(&ToolRunnerImpl::OnFinished, weak_factory_.GetWeakPtr(),
                     tool, std::move(callback)));
}

}  // namespace zephyrus::agent
