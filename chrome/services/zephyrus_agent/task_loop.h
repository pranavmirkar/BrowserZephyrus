// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_SERVICES_ZEPHYRUS_AGENT_TASK_LOOP_H_
#define CHROME_SERVICES_ZEPHYRUS_AGENT_TASK_LOOP_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/task/sequenced_task_runner_helpers.h"
#include "chrome/services/zephyrus_agent/kernel/src/lib.rs.h"
#include "chrome/services/zephyrus_agent/public/mojom/agent_kernel.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/rust/cxx/v1/cxx.h"

namespace zephyrus::agent {

// One task, from the user's words to a stopping point.
//
// Look at the page, ask the model, execute what it proposed, repeat. It runs in
// the kernel's own process, which is the point: ADR 0001 exists because a
// runaway inference loop must not be able to take the browser down, and that
// only holds if the loop is on this side of the boundary.
//
// Two properties are worth stating because they are easy to erode:
//
// 1. **Every path out is a stopping point.** The loop never returns while it
//    still intends to do something, and it always returns exactly once. An
//    agent that goes quiet is worse than one that fails, because nothing
//    upstream can tell the difference between thinking and dead.
//
// 2. **The step budget is not advisory.** It is the only thing that stops a
//    model that has decided to look at the same page forever. Nothing in the
//    loop can extend it.
//
// Owns itself: `Start` creates one and it deletes itself once it has answered.
class TaskLoop {
 public:
  using DoneCallback = base::OnceCallback<void(mojom::TaskOutcomePtr)>;

  // Runs a task and answers `done` exactly once. Nothing else needs to hold on
  // to the returned object; there isn't one.
  static void Start(const Kernel& kernel,
                    std::string task,
                    mojo::PendingRemote<mojom::ToolRunner> runner,
                    mojo::PendingRemote<mojom::AgentModel> model,
                    uint32_t max_steps,
                    mojom::PendingApprovalPtr approved,
                    DoneCallback done);

  TaskLoop(const TaskLoop&) = delete;
  TaskLoop& operator=(const TaskLoop&) = delete;

 private:
  // DeleteSoon needs to reach the destructor. `delete this` from inside a mojo
  // reply callback tears down the Remote that is currently dispatching, which
  // is a use-after-free that only appears under load -- the same lesson as the
  // self-owned dialog listener.
  friend class base::DeleteHelper<TaskLoop>;

  TaskLoop(const Kernel& kernel,
           std::string task,
           mojo::PendingRemote<mojom::ToolRunner> runner,
           mojo::PendingRemote<mojom::AgentModel> model,
           uint32_t max_steps,
           mojom::PendingApprovalPtr approved,
           DoneCallback done);
  ~TaskLoop();

  // One turn: observe, propose, execute.
  void Step();
  void OnObserved(const std::string& observation_json);
  void OnProposed(const std::string& response);
  void OnExecuted(std::string tool,
                  std::string arguments_json,
                  mojom::ToolOutcomePtr outcome);

  // Answers `done_` and schedules deletion. Safe to call from inside a mojo
  // reply callback, which is where every caller is.
  void Finish(mojom::TaskStatus status,
              std::string message,
              mojom::PendingApprovalPtr pending = nullptr);

  std::string SystemPrompt() const;
  std::string UserPrompt() const;

  // Called when either remote drops. A task whose browser or model has gone
  // away cannot make progress and must say so rather than wait.
  void OnDisconnected();

  const raw_ref<const Kernel> kernel_;
  const std::string task_;
  const uint32_t max_steps_;

  mojo::Remote<mojom::ToolRunner> runner_;
  mojo::Remote<mojom::AgentModel> model_;
  DoneCallback done_;

  // Set only on the first step, and consumed there. An approval is for one
  // call; the loop deliberately does not remember it, so the next call that
  // needs one stops the task again.
  mojom::PendingApprovalPtr approved_;

  uint32_t steps_ = 0;
  std::string observation_json_;

  // What has happened so far, oldest first, already shaped for the prompt. A
  // model with no memory of its last step repeats it.
  std::vector<std::string> history_;
};

}  // namespace zephyrus::agent

#endif  // CHROME_SERVICES_ZEPHYRUS_AGENT_TASK_LOOP_H_
