// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_SERVICES_ZEPHYRUS_AGENT_TASK_LOOP_H_
#define CHROME_SERVICES_ZEPHYRUS_AGENT_TASK_LOOP_H_

#include <deque>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
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
                    DoneCallback done,
                    mojom::TaskMemoryPtr memory = nullptr);

  // The same, with a cloud model the kernel speaks to through the browser's
  // transport (ADR 0004). The kernel builds each request, reads each reply and
  // enforces the spending limit in `cloud`.
  static void StartCloud(const Kernel& kernel,
                         std::string task,
                         mojo::PendingRemote<mojom::ToolRunner> runner,
                         mojom::CloudModelPtr cloud,
                         uint32_t max_steps,
                         mojom::PendingApprovalPtr approved,
                         DoneCallback done,
                         mojom::TaskMemoryPtr memory = nullptr);

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
           mojom::CloudModelPtr cloud,
           uint32_t max_steps,
           mojom::PendingApprovalPtr approved,
           DoneCallback done,
           mojom::TaskMemoryPtr memory);

  // What the agent is told it knows, before the page: the user's saved facts and
  // the chat so far. Empty when there is nothing.
  std::string MemoryPrompt() const;

  // Asks whichever model this task has for the next call.
  void AskModel();
  // The cloud path: send what the kernel built, and read what came back.
  void SendCloudRequest();
  void OnCloudReply(int32_t status, const std::string& body);
  // What this run has spent, at the prices the browser supplied.
  double SpentUsd() const;
  ~TaskLoop();

  // One turn: observe, propose, execute.
  void Step();
  void OnObserved(const std::string& observation_json);
  void OnProposed(const std::string& response);
  void OnExecuted(std::string tool,
                  std::string arguments_json,
                  mojom::ToolOutcomePtr outcome);
  // The history line for a page.read or notes.add that succeeded, and the
  // update to notes_/last_read_ it caused.
  std::string NoteOrReadLine(const std::string& tool,
                             const std::string& arguments_json,
                             const mojom::ToolOutcome& outcome);
  // Runs the next call the model made in the same turn. See `queued_`.
  void RunQueued();
  // Drops the rest of this turn's calls, saying so in the history.
  void DropQueued(std::string_view why);

  // Answers `done_` and schedules deletion. Safe to call from inside a mojo
  // reply callback, which is where every caller is.
  void Finish(mojom::TaskStatus status,
              std::string message,
              mojom::PendingApprovalPtr pending = nullptr);

  std::string SystemPrompt() const;
  std::string UserPrompt() const;

  // Two real, clickable elements from the current Observation, named, or empty.
  //
  // `instead_of` is the tool that just failed to change anything, and it is
  // left out of the suggested ways forward. Advice that recommends the tool
  // the model is already stuck on is not advice: a traced run was told "doing
  // it again will do nothing -- use a relevant untried target, page.find, or
  // task.ask", and answered with page.find. Ten times, until the budget ran
  // out. The harness had diagnosed the trap correctly and then pointed back
  // into it.
  // `failed_id` is the element the refused call named, if it named one, so
  // the advice never points back at what just did not work.
  std::string SomethingToActOn(std::string_view instead_of,
                               std::string_view failed_id = {}) const;

  // Called when either remote drops. A task whose browser or model has gone
  // away cannot make progress and must say so rather than wait.
  void OnDisconnected();

  const raw_ref<const Kernel> kernel_;
  const std::string task_;
  const uint32_t max_steps_;

  // The last call actually executed, and the page as it looked when it was.
  // Together they answer "would doing this again change anything" -- see the
  // guard in OnProposed. Neither is enough alone: the same call on a changed
  // page is often the right move, and a changed call on the same page always
  // is.
  // The page the model was last shown, so a move to a new one can be pointed
  // out. Knowing it ARRIVED somewhere is what tells it the job is finished.
  std::string last_seen_url_;

  std::string last_call_;
  // The Observation with `what_changed` taken out, which is what "did the page
  // change" actually means. See OnObserved.
  // How many times running the model has failed to produce a tool call, and
  // how many times a repeat has been refused. Both are shown to it, because a
  // zero-temperature model handed an unchanged prompt gives an unchanged reply
  // -- so every rejection has to leave a mark the model can see.
  uint32_t unparsed_replies_ = 0;
  uint32_t refused_repeats_ = 0;

  // Every (call, page) pair this task has already tried, so a loop that
  // walks in a circle is caught as well as one that stands still.
  std::set<std::string> seen_here_;

  std::string page_key_;
  std::string page_at_last_call_;

  // The page as it was one step ago, and how many steps have not moved it.
  //
  // The other repeat guards key on the CALL: the same tool with the same
  // arguments against the same page. That catches a model standing perfectly
  // still and misses one shuffling -- "RTX 4090 price", "cheapest RTX 4090",
  // "RTX 4090 price list" are three different calls and one identical
  // non-event. Ten of those went by in a traced run because no two adjacent
  // ones matched.
  //
  // Keyed on the RESULT instead, which is the thing that actually matters and
  // the one a model cannot vary its way around: if the page has not changed in
  // several steps, nothing being tried is working, whatever it was called.
  std::string page_at_last_step_;
  uint32_t steps_without_change_ = 0;

  mojo::Remote<mojom::ToolRunner> runner_;
  mojo::Remote<mojom::AgentModel> model_;

  // Set instead of `model_` for a cloud model. `cloud_->transport` has been
  // moved into `transport_`.
  mojom::CloudModelPtr cloud_;
  mojo::Remote<mojom::ModelTransport> transport_;
  // The request being sent, kept so a transient failure can resend exactly it.
  std::string pending_path_;
  base::flat_map<std::string, std::string> pending_headers_;
  std::string pending_body_;
  // Consecutive transient failures of the current request. See OnCloudReply.
  int transient_failures_ = 0;
  // What this run has spent, reported in the outcome.
  mojom::TokenUsage usage_;
  DoneCallback done_;

  // Set only on the first step, and consumed there. An approval is for one
  // call; the loop deliberately does not remember it, so the next call that
  // needs one stops the task again.
  mojom::PendingApprovalPtr approved_;

  uint32_t steps_ = 0;
  uint32_t loading_rechecks_ = 0;
  // The last action was a click on a link and the page has not moved yet: a
  // link to another site can take a couple of seconds to start opening, and the
  // look that follows the click usually beats it. Re-looked at for free (no
  // model step) a few times before the model is asked what to do.
  bool link_click_pending_ = false;
  uint32_t link_rechecks_ = 0;
  std::string observation_json_;
  // This step's screenshot (JPEG, base64), lifted out of the Observation so it
  // travels as an image and never as prompt text or part of the page key.
  std::string screenshot_base64_;
  // What the agent has written down with notes.add. Shown on every turn,
  // unlike the history, which has a window: a research task reads a dozen
  // pages, forgets each as it leaves, and answers from these.
  std::string notes_;
  // What the browser remembers for this task: the chat so far and the user's
  // saved facts. See mojom::TaskMemory.
  mojom::TaskMemoryPtr memory_;
  // How hard a cloud model is asked to think per step; empty for the provider's
  // default. Dropped for the rest of the task if the provider refuses it.
  std::string effort_;
  // The text of the agent's most recent page.read, and only that one -- an
  // older read is replaced, so the prompt is bounded however long the reading.
  std::string last_read_;
  // Calls the model made after the first in the same turn, not yet run: tool
  // and arguments, in order. Run one by one as each before it succeeds, with
  // no new look and no step spent; the first that fails, is refused or needs
  // approval drops the rest. Every one is still judged by policy on its own.
  std::deque<std::pair<std::string, std::string>> queued_;

  // What has happened so far, oldest first, already shaped for the prompt. A
  // model with no memory of its last step repeats it.
  std::vector<std::string> history_;
  base::WeakPtrFactory<TaskLoop> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_SERVICES_ZEPHYRUS_AGENT_TASK_LOOP_H_
