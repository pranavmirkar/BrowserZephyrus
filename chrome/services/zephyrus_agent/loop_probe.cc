// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Runs the REAL TaskLoop for the benchmark, with the benchmark standing in for
// the browser and the model.
//
// The multi-step benchmark used to keep its own copy of the loop: the system
// prompt, the closing instruction, the history lines, the stuck and repeat
// rules, the refusal hint. Every piece was "copied from task_loop.cc, keep it
// copied", and by the time anyone compared them they had drifted in six
// places. The benchmark was grading a loop nobody ships. This is the same fix
// as the extractor: stop copying, run the thing.
//
// Protocol: one JSON object per line each way. The benchmark starts a run with
//
//   {"op":"run","task":"...","max_steps":12,
//    "approved":{"tool":"...","arguments_json":"..."}}   (approved optional)
//
// and the loop then asks it three kinds of question, each answered with one
// line before the loop goes on:
//
//   {"event":"observe","level":1}        <- {"observation_json":"..."}
//   {"event":"propose","system":"...","user":"..."}
//                                        <- {"response":"..."}
//   {"event":"execute","tool":"...","arguments_json":"...","approved":false}
//                                        <- {"status":"ok|needs_approval|
//                                             denied|failed","message":"...",
//                                             "value_json":"...","risk":"..."}
//
// and ends every run with exactly one
//
//   {"event":"done","status":"...","message":"...","steps":N,
//    "pending":{"tool":...,"arguments_json":...,"reason":...,"risk":...,
//               "history":[...],"last_url":...}}
//
// after which another "run" line may follow -- that is how a resume after an
// approval works, exactly as the browser does it: a NEW TaskLoop holding the
// approved call and the remaining budget. End of input ends the process, at
// any point, which is how the benchmark stops a run that has reached harm.

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/values.h"
#include "chrome/services/zephyrus_agent/kernel/src/lib.rs.h"
#include "chrome/services/zephyrus_agent/public/mojom/agent_kernel.mojom.h"
#include "chrome/services/zephyrus_agent/task_loop.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/public/cpp/bindings/receiver.h"

namespace zephyrus::agent {
namespace {

void Send(const base::DictValue& message) {
  std::cout << base::WriteJson(message).value_or("{}") << std::endl;
}

// The benchmark's answer to the question just sent. No answer means the
// benchmark has finished with this run, so the process ends here: TaskLoop has
// no cancellation hook, and a loop that is simply abandoned is the honest
// equivalent of a browser that went away.
base::DictValue Receive() {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) {
      continue;
    }
    std::optional<base::DictValue> parsed =
        base::JSONReader::ReadDict(line, base::JSON_PARSE_RFC);
    if (!parsed) {
      std::cerr << "loop probe: not a JSON object: " << line << "\n";
      std::exit(2);
    }
    return std::move(*parsed);
  }
  std::exit(0);
}

std::string TextOr(const base::DictValue& dict, const char* key) {
  const std::string* value = dict.FindString(key);
  return value ? *value : std::string();
}

mojom::ToolStatus StatusFrom(const std::string& name) {
  if (name == "ok") {
    return mojom::ToolStatus::kOk;
  }
  if (name == "needs_approval") {
    return mojom::ToolStatus::kNeedsApproval;
  }
  if (name == "denied") {
    return mojom::ToolStatus::kDenied;
  }
  return mojom::ToolStatus::kFailed;
}

const char* StatusName(mojom::TaskStatus status) {
  switch (status) {
    case mojom::TaskStatus::kCompleted:
      return "completed";
    case mojom::TaskStatus::kAskedTheUser:
      return "asked_the_user";
    case mojom::TaskStatus::kNeedsApproval:
      return "needs_approval";
    case mojom::TaskStatus::kOutOfSteps:
      return "out_of_steps";
    case mojom::TaskStatus::kFailed:
      return "failed";
    case mojom::TaskStatus::kCancelled:
      return "cancelled";
  }
  return "failed";
}

// The browser and the model, both answered by the benchmark.
class Bench : public mojom::ToolRunner, public mojom::AgentModel {
 public:
  mojo::PendingRemote<mojom::ToolRunner> BindRunner() {
    return runner_.BindNewPipeAndPassRemote();
  }
  mojo::PendingRemote<mojom::AgentModel> BindModel() {
    return model_.BindNewPipeAndPassRemote();
  }

  // mojom::ToolRunner:
  void Observe(int32_t level, ObserveCallback callback) override {
    base::DictValue event;
    event.Set("event", "observe");
    event.Set("level", level);
    Send(event);
    std::move(callback).Run(TextOr(Receive(), "observation_json"));
  }
  void Execute(const std::string& tool,
               const std::string& arguments_json,
               ExecuteCallback callback) override {
    std::move(callback).Run(Run(tool, arguments_json, /*approved=*/false));
  }
  void ExecuteApproved(const std::string& tool,
                       const std::string& arguments_json,
                       ExecuteApprovedCallback callback) override {
    std::move(callback).Run(Run(tool, arguments_json, /*approved=*/true));
  }

  // mojom::AgentModel:
  void Propose(const std::string& system_prompt,
               const std::string& user_prompt,
               ProposeCallback callback) override {
    base::DictValue event;
    event.Set("event", "propose");
    event.Set("system", system_prompt);
    event.Set("user", user_prompt);
    Send(event);
    std::move(callback).Run(TextOr(Receive(), "response"));
  }

 private:
  mojom::ToolOutcomePtr Run(const std::string& tool,
                            const std::string& arguments_json,
                            bool approved) {
    base::DictValue event;
    event.Set("event", "execute");
    event.Set("tool", tool);
    event.Set("arguments_json", arguments_json);
    event.Set("approved", approved);
    Send(event);
    const base::DictValue answer = Receive();
    auto outcome = mojom::ToolOutcome::New();
    outcome->status = StatusFrom(TextOr(answer, "status"));
    outcome->message = TextOr(answer, "message");
    outcome->value_json = TextOr(answer, "value_json");
    outcome->risk = TextOr(answer, "risk");
    return outcome;
  }

  mojo::Receiver<mojom::ToolRunner> runner_{this};
  mojo::Receiver<mojom::AgentModel> model_{this};
};

// One TaskLoop, start to finish. The receivers outlive it: the loop deletes
// itself with DeleteSoon after answering, and the idle run below lets that
// happen before its pipes close, so it never sees a disconnect it did not earn.
void RunOnce(const Kernel& kernel, const base::DictValue& request) {
  mojom::PendingApprovalPtr approved;
  if (const base::DictValue* pending = request.FindDict("approved")) {
    approved = mojom::PendingApproval::New();
    approved->tool = TextOr(*pending, "tool");
    approved->arguments_json = TextOr(*pending, "arguments_json");
    approved->reason = TextOr(*pending, "reason");
    approved->risk = TextOr(*pending, "risk");
    approved->last_url = TextOr(*pending, "last_url");
    if (const base::ListValue* history = pending->FindList("history")) {
      for (const base::Value& line : *history) {
        if (line.is_string()) {
          approved->history.push_back(line.GetString());
        }
      }
    }
  }

  Bench bench;
  base::RunLoop run_loop;
  TaskLoop::Start(
      kernel, TextOr(request, "task"), bench.BindRunner(), bench.BindModel(),
      static_cast<uint32_t>(request.FindInt("max_steps").value_or(12)),
      std::move(approved),
      base::BindOnce(
          [](base::OnceClosure quit, mojom::TaskOutcomePtr outcome) {
            base::DictValue done;
            done.Set("event", "done");
            done.Set("status", StatusName(outcome->status));
            done.Set("message", outcome->message);
            done.Set("steps", static_cast<int>(outcome->steps));
            if (outcome->pending) {
              base::DictValue pending;
              pending.Set("tool", outcome->pending->tool);
              pending.Set("arguments_json", outcome->pending->arguments_json);
              pending.Set("reason", outcome->pending->reason);
              pending.Set("risk", outcome->pending->risk);
              pending.Set("last_url", outcome->pending->last_url);
              base::ListValue history;
              for (const std::string& line : outcome->pending->history) {
                history.Append(line);
              }
              pending.Set("history", std::move(history));
              done.Set("pending", std::move(pending));
            }
            Send(done);
            std::move(quit).Run();
          },
          run_loop.QuitClosure()));
  run_loop.Run();
  base::RunLoop().RunUntilIdle();
}

}  // namespace
}  // namespace zephyrus::agent

int main() {
  mojo::core::Init();
  base::SingleThreadTaskExecutor executor;

  rust::Box<zephyrus::agent::Kernel> kernel = zephyrus::agent::load_kernel();
  if (!kernel->is_valid()) {
    std::cerr << "kernel did not load: " << std::string(kernel->last_error())
              << "\n";
    return 2;
  }

  while (true) {
    const base::DictValue request = zephyrus::agent::Receive();
    if (zephyrus::agent::TextOr(request, "op") != "run") {
      std::cerr << "loop probe: expected a run request\n";
      return 2;
    }
    zephyrus::agent::RunOnce(*kernel, request);
  }
}
