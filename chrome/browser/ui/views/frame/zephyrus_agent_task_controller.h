// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_TASK_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_TASK_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_tool_surface.h"
#include "chrome/browser/zephyrus/agent/dev_model_client.h"
#include "chrome/browser/zephyrus/agent/tool_executor.h"
#include "chrome/browser/zephyrus/agent/tool_runner_impl.h"
#include "chrome/services/zephyrus_agent/public/mojom/agent_kernel.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"

class Browser;

namespace zephyrus::agent {

// One task, from the user's words to an answer, including the questions in
// between.
//
// This is the piece that makes an Ask mean something. The kernel stops a task
// when a call needs the user's say-so; this owns the conversation that follows:
// it puts the question on screen, and if the answer is yes it resumes the task
// with that one call approved.
//
// It stands in as the model rather than passing the caller's remote straight
// through, because a task can run more than once: a PendingRemote is single
// use, so handing the same one to a resumed RunTask would give the loop a dead
// model. Forwarding lets each run get a fresh pipe to the same place.
class ZephyrusAgentTaskController : public mojom::AgentModel,
                                    public ToolRunnerImpl::Observer {
 public:
  using FinishedCallback = base::OnceCallback<void(mojom::TaskOutcomePtr)>;

  // What a surface showing the task needs to hear.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // One line of plain English about what just happened, for the running log.
    virtual void OnAgentProgress(const std::string& line) = 0;

    // The kernel wants this call approved. `answer` must be run exactly once.
    //
    // Handed over rather than shown here on purpose: with a panel to put it in,
    // a browser-modal dialog seizes the whole window for a question the agent
    // asked, and splits one conversation across two surfaces.
    virtual void OnAgentApprovalNeeded(const std::string& reason,
                                       const std::string& risk,
                                       base::OnceCallback<void(bool)> answer) = 0;
  };

  // Not owned; must outlive this.
  void SetDelegate(Delegate* delegate) { delegate_ = delegate; }

  explicit ZephyrusAgentTaskController(Browser* browser);
  ~ZephyrusAgentTaskController() override;

  ZephyrusAgentTaskController(const ZephyrusAgentTaskController&) = delete;
  ZephyrusAgentTaskController& operator=(const ZephyrusAgentTaskController&) =
      delete;

  // Runs `task` until it finishes, is declined, or runs out of steps.
  //
  // `model` is the thing answering as the model. There is no production one
  // yet -- the inference runtime does not exist -- so a caller with nothing to
  // pass gets a clean failure rather than a hang.
  void StartTask(const std::string& task,
                 mojo::PendingRemote<mojom::AgentModel> model,
                 uint32_t max_steps,
                 FinishedCallback done);

  // Runs `task` with whatever model the command line configured.
  //
  // This is the entry point a UI should call. It returns false, having done
  // nothing, when no model is configured -- which is the ordinary case in a
  // build nobody passed the development switches to, and is why there is no
  // agent in a normal browser today.
  bool StartTaskWithConfiguredModel(const std::string& task,
                                    uint32_t max_steps,
                                    FinishedCallback done);

  // Answers every approval question with `answer` instead of showing a
  // dialog. Test-facing: it replaces the UI, not the decision -- everything
  // after the answer is the production path.
  void SetAutoAnswerForTesting(bool answer) { auto_answer_ = answer; }
  bool HasPendingApprovalForTesting() const { return !pending_.is_null(); }

  // mojom::AgentModel: forwards to whatever the caller supplied.
  void Propose(const std::string& system_prompt,
               const std::string& user_prompt,
               ProposeCallback callback) override;

 private:
  void Run(mojom::PendingApprovalPtr approved);
  void OnTaskOutcome(mojom::TaskOutcomePtr outcome);
  void ShowApproval(mojom::PendingApprovalPtr pending);

  // ToolRunnerImpl::Observer:
  void OnAgentLooked() override;
  void OnAgentToolStarted(const std::string& tool,
                          const std::string& arguments_json,
                          const std::string& target) override;
  void OnAgentToolFinished(const std::string& tool,
                           const mojom::ToolOutcome& outcome) override;

  void Report(const std::string& line);
  void OnAnswered(bool approved);
  void Finish(mojom::TaskOutcomePtr outcome);

  const raw_ptr<Browser> browser_;
  raw_ptr<Delegate> delegate_ = nullptr;

  std::string task_;
  FinishedCallback done_;

  // What is left of the budget. **Carried across a resume on purpose.** If it
  // reset, approving a call would hand the agent a fresh allowance, and a task
  // could run forever by asking often enough.
  uint32_t steps_remaining_ = 0;

  // Whether anything actually happened between two looks. The loop looks once
  // per step, so a look with no action behind it means the step was spent on a
  // reply that carried no usable tool call.
  bool looked_before_ = false;
  bool acted_since_look_ = false;
  std::optional<bool> auto_answer_;

  // The call waiting on an answer, kept so the answer applies to that call and
  // not to whatever the model would propose next.
  mojom::PendingApprovalPtr pending_;

  mojo::Remote<mojom::AgentKernel> kernel_;

  // The real model, and the receivers this hands out in its place -- one per
  // run, because each RunTask consumes the remote it is given.
  mojo::Remote<mojom::AgentModel> model_;
  mojo::ReceiverSet<mojom::AgentModel> model_receivers_;

  std::unique_ptr<BrowserToolSurface> surface_;
  std::unique_ptr<AgentKernelClient> kernel_client_;

  // Outlives individual runs on purpose: it holds the Observation, so a resumed
  // task still knows what was on the page when the question was asked.
  std::unique_ptr<ToolExecutor> executor_;

  // Rebuilt per run. A mojo::Receiver binds once, so reusing one across a
  // resume would CHECK.
  std::unique_ptr<ToolRunnerImpl> runner_;

  // Only present when the development switches were passed. Owned here so it
  // outlives the runs that talk to it.
  std::unique_ptr<DevModelClient> dev_model_;

  base::WeakPtrFactory<ZephyrusAgentTaskController> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_TASK_CONTROLLER_H_
