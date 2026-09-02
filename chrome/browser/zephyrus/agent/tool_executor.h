// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_TOOL_EXECUTOR_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_TOOL_EXECUTOR_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "chrome/browser/zephyrus/agent/agent_kernel_client.h"
#include "chrome/browser/zephyrus/agent/observation.h"
#include "chrome/browser/zephyrus/agent/tool_surface.h"
#include "chrome/services/zephyrus_agent/public/mojom/agent_kernel.mojom.h"

namespace zephyrus::agent {

// Runs a tool call the model proposed, after the kernel has approved it.
//
// **There is one path from a proposed call to a performed action and it goes
// through the kernel.** Nothing here executes anything before Decide() comes
// back Allow. That is the property this class exists to hold; a second entry
// point that skipped the check would make the kernel decorative.
class ToolExecutor {
 public:
  struct Result {
    enum class Status {
      // The tool ran.
      kOk,
      // The kernel wants the user to approve this specific call first. Nothing
      // has been done to the browser.
      kNeedsApproval,
      // The kernel refused. Nothing has been done to the browser.
      kDenied,
      // Allowed, but it could not be carried out.
      kFailed,
    };

    Status status = Status::kFailed;
    // On kNeedsApproval and kDenied, the kernel's reason, written to be read by
    // the user. On kFailed, what went wrong. On kOk, usually empty.
    std::string message;
    // JSON for the model to read on kOk. Empty for tools that produce nothing.
    std::string value_json;
    // Effective risk class the kernel assigned: R0, R1, R2 or R3.
    std::string risk;
  };

  using ExecuteCallback = base::OnceCallback<void(Result)>;

  // Neither pointer may be null, and both must outlive this object.
  ToolExecutor(AgentKernelClient* kernel, ToolSurface* surface);
  ~ToolExecutor();

  ToolExecutor(const ToolExecutor&) = delete;
  ToolExecutor& operator=(const ToolExecutor&) = delete;

  // Judges and, if allowed, runs one proposed call.
  //
  // `arguments_json` is raw model output and is not parsed here before the
  // kernel has seen it. `task` is the user's own words.
  //
  // The element list the kernel grounds against comes from THIS OBJECT'S last
  // Observation, never from the caller. Nobody outside can claim an element was
  // on the page, so an invented id cannot be made to look real.
  //
  // `callback` always runs exactly once, even if this object is destroyed
  // first: a dropped callback would leave an agent loop waiting forever, which
  // is worse than a failure.
  void Execute(const std::string& tool,
               const std::string& arguments_json,
               const std::string& task,
               ExecuteCallback callback);

  // Runs a call the user has explicitly approved.
  //
  // Approval lifts an **Ask** and nothing else. A call the kernel DENIES stays
  // denied however emphatically it is approved: those are the calls where no
  // answer from the user makes the action safe -- an invented element, a tool
  // that does not exist, a password field. Letting a dialog override them would
  // turn every denial into a question, and a user who is clicking through
  // prompts would answer it.
  //
  // The kernel is still consulted. The approval is checked against what policy
  // says right now, not against what it said when the question was asked, so a
  // page that changed underneath cannot have an old approval applied to it.
  void ExecuteApproved(const std::string& tool,
                       const std::string& arguments_json,
                       const std::string& task,
                       ExecuteCallback callback);

  // What the browser last saw. Empty until something observes, which is why an
  // element-based call before any page.observe is refused rather than guessed
  // at.
  const Observation& observation() const { return observation_; }

 private:
  void Send(const std::string& tool,
            const std::string& arguments_json,
            const std::string& task,
            bool user_approved,
            ExecuteCallback callback);

  void OnDecided(std::string tool,
                 std::string arguments_json,
                 bool user_approved,
                 ExecuteCallback callback,
                 mojom::PolicyDecisionPtr decision);

  // Performs an approved call. Only ever reached from OnDecided with kAllow.
  // Asynchronous because looking at a page means asking the renderer.
  void Perform(const std::string& tool,
               const base::DictValue& arguments,
               ExecuteCallback callback);

  // Runs one of the tools that acts on an element the model named.
  Result PerformOnElement(const std::string& tool,
                          const base::DictValue& arguments);

  void OnObserved(std::string tool,
                  int level,
                  std::string query,
                  ExecuteCallback callback,
                  Observation observation);

  Observation observation_;

  raw_ptr<AgentKernelClient> kernel_;
  raw_ptr<ToolSurface> surface_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ToolExecutor> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_TOOL_EXECUTOR_H_
