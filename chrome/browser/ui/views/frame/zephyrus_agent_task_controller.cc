// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_agent_task_controller.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/browser_process.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_memory_access.h"
#include "chrome/browser/zephyrus/agent/agent_memory.h"
#include "chrome/browser/zephyrus/agent/model_settings.h"
#include "components/prefs/pref_service.h"
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
  outcome->usage = mojom::TokenUsage::New();
  return outcome;
}

std::string ProviderName(const std::string& kind) {
  if (kind == "anthropic") {
    return "Anthropic";
  }
  if (kind == "gemini") {
    return "Google Gemini";
  }
  return "the OpenAI-compatible service";
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
  Begin();
}

PointerObserver* ZephyrusAgentTaskController::Delegate::GetPointerObserver() {
  return nullptr;
}

void ZephyrusAgentTaskController::Begin() {
  surface_ = std::make_unique<BrowserToolSurface>(browser_);
  // The agent moves the pointer and turns the wheel the way a hand does, and
  // whoever is drawing the mascot is told where the pointer is.
  surface_->SetHumanMotion(true);
  if (delegate_) {
    surface_->SetPointerObserver(delegate_->GetPointerObserver());
  }
  if (transport_) {
    // A cloud model reads a much larger page than the local model the default
    // was measured on. See BrowserToolSurface::SetObservationBudget.
    surface_->SetObservationBudget(120, 4000);
    surface_->SetModelScreenshots(cloud_config_->send_screenshots);
  }
  kernel_client_ = std::make_unique<AgentKernelClient>();
  executor_ = std::make_unique<ToolExecutor>(kernel_client_.get(),
                                             surface_.get());
  // What the agent asks to have remembered goes to this profile's long-term
  // memory, for this workspace -- unless the person has switched memory off, and
  // a private profile's memory has no file to write to.
  if (browser_ && browser_->profile() &&
      browser_->profile()->GetPrefs()->GetBoolean(kMemoryPref)) {
    Profile* profile = browser_->profile();
    LongTermMemory* store = GetLongTermMemory(profile);
    const int workspace = workspace_id_;
    const bool persist = !profile->IsOffTheRecord();
    ToolExecutor::MemorySink sink;
    sink.remember = base::BindRepeating(
        [](LongTermMemory* store, int workspace, bool persist,
           const std::string& fact) {
          return store->Add(fact, workspace, persist);
        },
        store, workspace, persist);
    sink.forget = base::BindRepeating(
        [](LongTermMemory* store, int workspace, const std::string& fact) {
          return store->Forget(fact, workspace);
        },
        store, workspace);
    executor_->SetMemory(std::move(sink));
  }
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

ZephyrusAgentTaskController::StartResult
ZephyrusAgentTaskController::StartTaskWithConfiguredModel(
    const std::string& task,
    uint32_t max_steps,
    int workspace_id,
    FinishedCallback done) {
  Profile* profile = browser_ ? browser_->profile() : nullptr;
  std::optional<CloudModelConfig> cloud =
      profile ? ReadCloudModelConfig(*profile->GetPrefs()) : std::nullopt;

  if (cloud) {
    // Private Workspace leaves nothing behind; sending its pages to a provider
    // would be the opposite of that, whatever the settings say.
    if (profile->IsOffTheRecord()) {
      return StartResult::kCloudOffInPrivate;
    }
    if (!IsCloudAllowedInWorkspace(*profile->GetPrefs(), workspace_id)) {
      return StartResult::kCloudOffInThisWorkspace;
    }
    task_ = task;
    steps_remaining_ = max_steps;
    done_ = std::move(done);
    workspace_id_ = workspace_id;
    cloud_config_ = std::move(cloud);
    cloud_prices_ = KnownPrices(cloud_config_->model);
    usd_remaining_ = cloud_config_->max_usd_per_task;
    tokens_remaining_ = kUnknownPriceTokenCap;
    if (!g_browser_process || !g_browser_process->os_crypt_async()) {
      Finish(FailedOutcome("the system keystore is not available", 0));
      return StartResult::kStarted;
    }
    g_browser_process->os_crypt_async()->GetInstance(
        base::BindOnce(&ZephyrusAgentTaskController::OnEncryptorForTask,
                       weak_factory_.GetWeakPtr()));
    return StartResult::kStarted;
  }

  dev_model_ = DevModelClient::CreateIfConfigured(
      browser_->profile()->GetURLLoaderFactory());
  if (!dev_model_) {
    return StartResult::kNoModel;
  }
  StartTask(task, dev_model_->BindNewPipeAndPassRemote(), max_steps,
            std::move(done));
  return StartResult::kStarted;
}

void ZephyrusAgentTaskController::OnEncryptorForTask(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  if (!done_ || !cloud_config_) {
    return;  // Cancelled while the keystore was answering.
  }
  std::optional<std::string> key =
      encryptor ? LoadApiKey(*browser_->profile()->GetPrefs(), *encryptor,
                             cloud_config_->kind)
                : std::nullopt;
  // The system network context: no Workspace's cookies or cache, whichever
  // Workspace the task runs in. The provider is not a site the user visits.
  transport_ = ModelTransport::Create(
      cloud_config_->kind, cloud_config_->base_url, key.value_or(std::string()),
      g_browser_process->shared_url_loader_factory());
  if (!transport_) {
    Finish(FailedOutcome(
        key ? "the model's address is not usable; check the agent settings"
            : "no API key is saved for " + ProviderName(cloud_config_->kind) +
                  "; add one in the agent settings",
        0));
    return;
  }
  Begin();
}

void ZephyrusAgentTaskController::Cancel() {
  if (!done_) {
    return;
  }

  // Drop the pipes first. The kernel's loop is watching them, so this is what
  // actually stops the work -- and it stops it whether the loop is waiting on a
  // model, waiting on the browser, or wedged.
  runner_.reset();
  model_receivers_.Clear();
  pending_.reset();
  // Dropping the transport drops the key with it, and ends any request the
  // kernel was waiting on.
  transport_.reset();

  auto outcome = mojom::TaskOutcome::New();
  outcome->usage = mojom::TokenUsage::New();
  outcome->status = mojom::TaskStatus::kCancelled;
  outcome->message = "you stopped it";
  outcome->steps = 0;
  Finish(std::move(outcome));
}

void ZephyrusAgentTaskController::Run(mojom::PendingApprovalPtr approved) {
  if (steps_remaining_ == 0) {
    auto outcome = mojom::TaskOutcome::New();
    outcome->usage = mojom::TokenUsage::New();
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

  mojom::CloudModelPtr cloud;
  if (transport_) {
    // A fresh model pipe is not what a cloud run needs: the kernel speaks to
    // the provider itself, through the transport.
    model_receivers_.Clear();
    model_for_run.reset();
    if (browser_->profile()->IsOffTheRecord() ||
        !IsCloudAllowedInWorkspace(*browser_->profile()->GetPrefs(),
                                   workspace_id_)) {
      Finish(FailedOutcome(
          "stopped: the cloud model was turned off for this workspace", 0));
      return;
    }
    const bool priced = cloud_prices_.has_value();
    if ((priced && usd_remaining_ <= 0) || (!priced && tokens_remaining_ == 0)) {
      Finish(FailedOutcome("stopped: this task reached its spending limit", 0));
      return;
    }
    cloud = mojom::CloudModel::New();
    cloud->kind = cloud_config_->kind;
    cloud->model = cloud_config_->model;
    cloud->force_tool = cloud_config_->force_tool;
    // Room for thinking as well as the call: on current Claude models thinking
    // is always on and counts against this cap, and 1,024 was used up before
    // the model chose anything. The spending limit is what bounds cost.
    cloud->max_tokens_per_step = 16000;
    if (priced) {
      cloud->usd_per_mtok_input = cloud_prices_->input;
      cloud->usd_per_mtok_output = cloud_prices_->output;
      cloud->usd_per_mtok_cache_read = cloud_prices_->cache_read;
      cloud->usd_per_mtok_cache_write = cloud_prices_->cache_write;
      cloud->max_usd = usd_remaining_;
    } else {
      cloud->max_tokens = tokens_remaining_;
    }
    cloud->transport = transport_->BindNewPipeAndPassRemote();
  }

  kernel_->RunTask(
      task_, runner_->BindNewPipeAndPassRemote(), std::move(model_for_run),
      steps_remaining_, std::move(approved), std::move(cloud),
      memory_ ? memory_.Clone() : nullptr,
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
  // And spend what it cost, for the same reason.
  if (transport_ && outcome->usage) {
    const mojom::TokenUsage& used = *outcome->usage;
    if (cloud_prices_) {
      usd_remaining_ -= (used.input * cloud_prices_->input +
                         used.output * cloud_prices_->output +
                         used.cache_read * cloud_prices_->cache_read +
                         used.cache_write * cloud_prices_->cache_write) /
                        1'000'000.0;
    }
    const uint64_t tokens =
        used.input + used.output + used.cache_read + used.cache_write;
    tokens_remaining_ = tokens >= tokens_remaining_ ? 0 : tokens_remaining_ - tokens;
  }

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
        pending_->reason, pending_->risk, pending_->tool,
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
    outcome->usage = mojom::TokenUsage::New();
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
  // A step that did nothing is the most important thing in this log, and it
  // used to be the only thing that left no trace.
  //
  // The loop looks once per step, so two looks with no action between them mean
  // a whole step was spent and nothing happened -- the model replied with no
  // usable tool call. A real run spent EIGHT of twenty steps that way and the
  // panel showed a column of identical "Looking at the page" lines, which reads
  // as the browser being slow rather than the model saying nothing.
  if (looked_before_ && !acted_since_look_) {
    Report("The model did not choose an action -- step wasted");
  }
  looked_before_ = true;
  acted_since_look_ = false;

  Report("Looking at the page");
  if (delegate_) {
    delegate_->OnAgentActivity(Delegate::Activity::kLooking, std::string());
  }
}

void ZephyrusAgentTaskController::OnAgentToolStarted(
    const std::string& tool,
    const std::string& arguments_json,
    const std::string& target) {
  acted_since_look_ = true;
  AgentTrace("TOOL " + tool + " " + arguments_json +
             (target.empty() ? std::string() : " -> " + target) + "\n");
  if (delegate_) {
    delegate_->OnAgentActivity(Delegate::Activity::kActing, tool);
  }

  // page.observe IS reported now, unlike before.
  //
  // It is no longer offered in the prompt -- the loop looks before every turn
  // anyway -- so a model asking for it is asking for a tool it was never shown,
  // and that is worth seeing rather than hiding. Silently swallowing it is what
  // made a wasted step look like a slow one.
  if (tool == "page.observe") {
    Report("Asked to look again (not needed -- the page is already shown)");
    return;
  }
  // Name the target. Watching a browser act on its own, "Clicking something" is
  // the moment you most want to know WHAT.
  Report(target.empty() ? PlainName(tool)
                        : PlainName(tool) + ": " + target);
}

void ZephyrusAgentTaskController::OnAgentToolFinished(
    const std::string& tool,
    const mojom::ToolOutcome& outcome) {
  AgentTrace("RESULT " + tool + " status=" +
             base::NumberToString(static_cast<int>(outcome.status)) + " " +
             outcome.message + "\n");
  if (delegate_) {
    delegate_->OnAgentActivity(Delegate::Activity::kFinished, tool);
  }
  // Only failures and refusals are worth a line. Success is visible in what
  // happens next; a log that narrates every success buries the one line that
  // explains why the agent stopped.
  if (outcome.status == mojom::ToolStatus::kDenied) {
    Report("Refused: " + outcome.message);
    if (delegate_) {
      delegate_->OnAgentActivity(Delegate::Activity::kRefused, tool);
    }
  } else if (outcome.status == mojom::ToolStatus::kFailed) {
    Report("Could not: " + outcome.message);
    if (delegate_) {
      delegate_->OnAgentActivity(Delegate::Activity::kFailed, tool);
    }
  }
}

void ZephyrusAgentTaskController::Report(const std::string& line) {
  AgentTrace("PANEL: " + line + "\n");
  if (delegate_) {
    delegate_->OnAgentProgress(line);
  }
}

}  // namespace zephyrus::agent
