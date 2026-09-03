// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_agent_task_controller.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window.h"
#include "components/constrained_window/constrained_window_views.h"
#include "content/public/browser/service_process_host.h"
#include "ui/base/models/dialog_model.h"

namespace zephyrus::agent {
namespace {

mojom::TaskOutcomePtr FailedOutcome(std::string message, uint32_t steps) {
  auto outcome = mojom::TaskOutcome::New();
  outcome->status = mojom::TaskStatus::kFailed;
  outcome->message = std::move(message);
  outcome->steps = steps;
  return outcome;
}

// What to call each tool in the running log.
//
// The model speaks in tool names; a person watching should not have to. An
// unknown name falls back to itself rather than to nothing, because a log line
// that says "working" while something unexpected happens is worse than an ugly
// one.
std::string PlainName(const std::string& tool) {
  if (tool == "page.observe" || tool == "page.find") {
    return "Looking at the page";
  }
  if (tool == "browser.navigate" || tool == "tabs.open") {
    return "Opening a page";
  }
  if (tool == "browser.back" || tool == "browser.forward") {
    return "Going back";
  }
  if (tool == "browser.reload") {
    return "Reloading";
  }
  if (tool == "tabs.list") {
    return "Checking the tabs";
  }
  if (tool == "tabs.switch") {
    return "Switching tab";
  }
  if (tool == "tabs.close") {
    return "Closing a tab";
  }
  if (tool == "page.click") {
    return "Clicking something";
  }
  if (tool == "page.type" || tool == "page.select") {
    return "Filling something in";
  }
  if (tool == "page.scroll") {
    return "Scrolling";
  }
  if (tool == "page.press") {
    return "Pressing a key";
  }
  if (tool == "selection.read") {
    return "Reading the selection";
  }
  if (tool == "task.complete" || tool == "task.ask") {
    return "Finishing up";
  }
  return tool;
}

}  // namespace

ZephyrusAgentTaskController::ZephyrusAgentTaskController(Browser* browser)
    : browser_(browser) {}

ZephyrusAgentTaskController::~ZephyrusAgentTaskController() = default;

void ZephyrusAgentTaskController::StartTask(
    const std::string& task,
    mojo::PendingRemote<mojom::AgentModel> model,
    uint32_t max_steps,
    FinishedCallback done) {
  task_ = task;
  model_.Bind(std::move(model));
  steps_remaining_ = max_steps;
  done_ = std::move(done);

  if (!model_.is_bound()) {
    // There is no in-process inference runtime yet, so a caller with nothing to
    // pass gets told so. Failing here beats a task that starts and then waits
    // on a model that will never answer.
    Finish(FailedOutcome("no model is configured for the agent", 0));
    return;
  }

  surface_ = std::make_unique<BrowserToolSurface>(browser_);
  kernel_client_ = std::make_unique<AgentKernelClient>();
  executor_ = std::make_unique<ToolExecutor>(kernel_client_.get(),
                                             surface_.get());
  kernel_ = content::ServiceProcessHost::Launch<mojom::AgentKernel>(
      content::ServiceProcessHost::Options()
          .WithDisplayName("Zephyrus Agent Kernel")
          .Pass());
  // A kernel that dies mid-task must end the task rather than leave it hanging.
  kernel_.set_disconnect_handler(base::BindOnce(
      [](base::WeakPtr<ZephyrusAgentTaskController> self) {
        if (self) {
          self->Finish(
              FailedOutcome("the agent stopped unexpectedly",
                            self->steps_remaining_));
        }
      },
      weak_factory_.GetWeakPtr()));

  Run(/*approved=*/nullptr);
}

bool ZephyrusAgentTaskController::StartTaskWithConfiguredModel(
    const std::string& task,
    uint32_t max_steps,
    FinishedCallback done) {
  dev_model_ = DevModelClient::CreateIfConfigured(
      browser_->profile()->GetURLLoaderFactory());
  if (!dev_model_) {
    return false;
  }
  StartTask(task, dev_model_->BindNewPipeAndPassRemote(), max_steps,
            std::move(done));
  return true;
}

void ZephyrusAgentTaskController::Run(mojom::PendingApprovalPtr approved) {
  if (steps_remaining_ == 0) {
    auto outcome = mojom::TaskOutcome::New();
    outcome->status = mojom::TaskStatus::kOutOfSteps;
    outcome->message = "stopped without finishing";
    outcome->steps = 0;
    Finish(std::move(outcome));
    return;
  }

  // Both rebuilt for this run. A mojo::Receiver binds once and a PendingRemote
  // is consumed once, so reusing either across a resume would fail -- the
  // receiver with a CHECK, the remote silently with a dead model.
  runner_ = std::make_unique<ToolRunnerImpl>(executor_.get(), task_);
  runner_->SetObserver(this);
  mojo::PendingRemote<mojom::AgentModel> model_for_run;
  model_receivers_.Add(this, model_for_run.InitWithNewPipeAndPassReceiver());

  kernel_->RunTask(
      task_, runner_->BindNewPipeAndPassRemote(), std::move(model_for_run),
      steps_remaining_, std::move(approved),
      base::BindOnce(&ZephyrusAgentTaskController::OnTaskOutcome,
                     weak_factory_.GetWeakPtr()));
}

void ZephyrusAgentTaskController::OnTaskOutcome(
    mojom::TaskOutcomePtr outcome) {
  if (!outcome) {
    Finish(FailedOutcome("the agent did not answer", 0));
    return;
  }

  // Spend what the run used, whatever happened. A task that stops for approval
  // has still done work, and not charging for it would let a model buy unlimited
  // steps by asking often enough.
  steps_remaining_ =
      outcome->steps >= steps_remaining_ ? 0 : steps_remaining_ - outcome->steps;

  if (outcome->status == mojom::TaskStatus::kNeedsApproval &&
      outcome->pending) {
    ShowApproval(std::move(outcome->pending));
    return;
  }

  Finish(std::move(outcome));
}

void ZephyrusAgentTaskController::ShowApproval(
    mojom::PendingApprovalPtr pending) {
  pending_ = std::move(pending);

  if (auto_answer_.has_value()) {
    OnAnswered(*auto_answer_);
    return;
  }

  if (delegate_) {
    delegate_->OnAgentApprovalNeeded(
        pending_->reason, pending_->risk,
        base::BindOnce(&ZephyrusAgentTaskController::OnAnswered,
                       weak_factory_.GetWeakPtr()));
    return;
  }

  // No surface to ask in. A modal is the fallback rather than the default:
  // better a dialog than a task that silently waits for an answer nobody was
  // asked for.
  auto model =
      ui::DialogModel::Builder()
          .SetTitle(u"Let the agent do this?")
          // The kernel's own words. It writes them to be read by a person
          // deciding in one second, and rewording them here would put a second
          // account of the same action in front of the user.
          .AddParagraph(
              ui::DialogModelLabel(base::UTF8ToUTF16(pending_->reason)))
          .AddOkButton(
              base::BindOnce(&ZephyrusAgentTaskController::OnAnswered,
                             weak_factory_.GetWeakPtr(), /*approved=*/true),
              ui::DialogModel::Button::Params().SetLabel(u"Allow once"))
          .AddCancelButton(
              base::BindOnce(&ZephyrusAgentTaskController::OnAnswered,
                             weak_factory_.GetWeakPtr(), /*approved=*/false),
              ui::DialogModel::Button::Params().SetLabel(u"Not now"))
          // Closing the dialog any other way is a no, not a yes. Silence must
          // never be consent for something the kernel decided to ask about.
          .SetCloseActionCallback(
              base::BindOnce(&ZephyrusAgentTaskController::OnAnswered,
                             weak_factory_.GetWeakPtr(), /*approved=*/false))
          .Build();

  constrained_window::ShowBrowserModal(
      std::move(model), browser_->window()->GetNativeWindow());
}

void ZephyrusAgentTaskController::OnAnswered(bool approved) {
  mojom::PendingApprovalPtr pending = std::move(pending_);
  if (!pending) {
    // Already answered. The close callback fires alongside a button on some
    // platforms, and answering twice would resume the task twice.
    return;
  }

  if (!approved) {
    auto outcome = mojom::TaskOutcome::New();
    outcome->status = mojom::TaskStatus::kAskedTheUser;
    outcome->message = "you declined: " + pending->reason;
    outcome->steps = 0;
    Finish(std::move(outcome));
    return;
  }

  Run(std::move(pending));
}

void ZephyrusAgentTaskController::Finish(mojom::TaskOutcomePtr outcome) {
  if (!done_) {
    return;
  }
  std::move(done_).Run(std::move(outcome));
}

void ZephyrusAgentTaskController::Propose(const std::string& system_prompt,
                                         const std::string& user_prompt,
                                         ProposeCallback callback) {
  if (!model_.is_bound()) {
    std::move(callback).Run(std::string());
    return;
  }
  model_->Propose(system_prompt, user_prompt, std::move(callback));
}

void ZephyrusAgentTaskController::OnAgentLooked() {
  Report("Looking at the page");
}

void ZephyrusAgentTaskController::OnAgentToolStarted(
    const std::string& tool,
    const std::string& arguments_json) {
  // page.observe is already reported by OnAgentLooked; saying it twice per step
  // would fill the log with the least interesting thing the agent does.
  if (tool != "page.observe") {
    Report(PlainName(tool));
  }
}

void ZephyrusAgentTaskController::OnAgentToolFinished(
    const std::string& tool,
    const mojom::ToolOutcome& outcome) {
  // Only failures and refusals are worth a line. Success is visible in what
  // happens next; a log that narrates every success buries the one line that
  // explains why the agent stopped.
  if (outcome.status == mojom::ToolStatus::kDenied) {
    Report("Refused: " + outcome.message);
  } else if (outcome.status == mojom::ToolStatus::kFailed) {
    Report("Could not: " + outcome.message);
  }
}

void ZephyrusAgentTaskController::Report(const std::string& line) {
  if (delegate_) {
    delegate_->OnAgentProgress(line);
  }
}

}  // namespace zephyrus::agent
