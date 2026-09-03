// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/tool_executor.h"

#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "url/gurl.h"

namespace zephyrus::agent {
namespace {

ToolExecutor::Result Failed(std::string message) {
  ToolExecutor::Result result;
  result.status = ToolExecutor::Result::Status::kFailed;
  result.message = std::move(message);
  return result;
}

ToolExecutor::Result Ok(std::string value_json = std::string()) {
  ToolExecutor::Result result;
  result.status = ToolExecutor::Result::Status::kOk;
  result.value_json = std::move(value_json);
  return result;
}

// The URL argument of a navigating tool, or nullopt if it is not a URL this
// browser will load.
//
// The kernel already judged where the navigation was going, but it reasons
// about strings. This is the executor refusing to hand anything to the network
// stack that is not an ordinary web URL -- a `file:` or `chrome:` destination
// is a different kind of action than the one that was approved.
std::optional<GURL> WebUrlArgument(const base::DictValue& arguments) {
  const std::string* raw = arguments.FindString("url");
  if (!raw) {
    return std::nullopt;
  }
  GURL url(*raw);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  return url;
}

}  // namespace

ToolExecutor::ToolExecutor(AgentKernelClient* kernel, ToolSurface* surface)
    : kernel_(kernel), surface_(surface) {
  CHECK(kernel_);
  CHECK(surface_);
}

ToolExecutor::~ToolExecutor() = default;

void ToolExecutor::Execute(const std::string& tool,
                           const std::string& arguments_json,
                           const std::string& task,
                           ExecuteCallback callback) {
  Send(tool, arguments_json, task, /*user_approved=*/false,
       std::move(callback));
}

void ToolExecutor::ExecuteApproved(const std::string& tool,
                                   const std::string& arguments_json,
                                   const std::string& task,
                                   ExecuteCallback callback) {
  Send(tool, arguments_json, task, /*user_approved=*/true, std::move(callback));
}

void ToolExecutor::Send(const std::string& tool,
                        const std::string& arguments_json,
                        const std::string& task,
                        bool user_approved,
                        ExecuteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Guarantee the caller is answered even if this object is destroyed while the
  // kernel is deciding. The weak pointer below stops OnDecided from running,
  // which without this wrapper would silently drop the callback.
  auto safe_callback = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), Failed("the agent stopped before this could run"));

  auto request = mojom::PolicyRequest::New();
  request->tool = tool;
  request->arguments_json = arguments_json;
  request->task = task;
  // Deliberately from the browser, not from the caller. The kernel decides
  // whether a destination is expected partly by comparing it to the current
  // origin, so letting anyone else name the current page would let them talk it
  // into approving a navigation.
  request->url = surface_->GetActiveUrl();
  // From our own last Observation, so nothing outside this object can claim
  // an element was on the page. This is what makes the kernel's grounding
  // check mean something.
  for (const ObservedNode& node : observation_.elements) {
    auto element = mojom::ObservedElement::New();
    element->id = node.id;
    element->role = node.role;
    element->name = node.name;
    request->elements.push_back(std::move(element));
  }

  kernel_->Decide(
      std::move(request),
      base::BindOnce(&ToolExecutor::OnDecided, weak_factory_.GetWeakPtr(), tool,
                     arguments_json, user_approved, std::move(safe_callback)));
}

void ToolExecutor::OnDecided(std::string tool,
                             std::string arguments_json,
                             bool user_approved,
                             ExecuteCallback callback,
                             mojom::PolicyDecisionPtr decision) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // A kernel that answered with nothing is a kernel that did not answer.
  if (!decision) {
    std::move(callback).Run(Failed("the agent could not be reached"));
    return;
  }

  // The one thing approval changes: an Ask the user said yes to becomes an
  // Allow. A Deny is untouched, because those are the calls where no answer
  // makes the action safe.
  const bool allowed =
      decision->disposition == mojom::Disposition::kAllow ||
      (user_approved && decision->disposition == mojom::Disposition::kAsk);

  if (!allowed) {
    Result result;
    result.status = decision->disposition == mojom::Disposition::kAsk
                        ? Result::Status::kNeedsApproval
                        : Result::Status::kDenied;
    result.message = decision->reason;
    result.risk = decision->risk;
    std::move(callback).Run(std::move(result));
    return;
  }

  // The kernel already validated this against the contract, so a parse failure
  // here means the two sides disagree about what was approved. Refuse rather
  // than act on a second reading of the same bytes.
  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(arguments_json, base::JSON_PARSE_RFC);
  if (!parsed) {
    std::move(callback).Run(Failed("the approved call could not be read"));
    return;
  }

  // The risk the kernel assigned rides along on whatever the tool returns.
  Perform(tool, *parsed,
          base::BindOnce(
              [](std::string risk, ExecuteCallback done, Result result) {
                result.risk = std::move(risk);
                std::move(done).Run(std::move(result));
              },
              decision->risk, std::move(callback)));
}

void ToolExecutor::Perform(const std::string& tool,
                           const base::DictValue& arguments,
                           ExecuteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (tool == "browser.navigate") {
    std::optional<GURL> url = WebUrlArgument(arguments);
    if (!url) {
      std::move(callback).Run(
          Failed("that is not a web address this browser will open"));
      return;
    }
    std::move(callback).Run(surface_->Navigate(*url)
                                ? Ok()
                                : Failed("the page did not load"));
    return;
  }

  if (tool == "browser.back") {
    std::move(callback).Run(surface_->GoBack()
                                ? Ok()
                                : Failed("there is nothing to go back to"));
    return;
  }

  if (tool == "browser.forward") {
    std::move(callback).Run(surface_->GoForward()
                                ? Ok()
                                : Failed("there is nothing to go forward to"));
    return;
  }

  if (tool == "browser.reload") {
    std::move(callback).Run(surface_->Reload()
                                ? Ok()
                                : Failed("the page could not be reloaded"));
    return;
  }

  if (tool == "tabs.list") {
    base::ListValue tabs;
    for (const ToolSurface::TabInfo& tab : surface_->ListTabs()) {
      base::DictValue entry;
      entry.Set("id", tab.id);
      entry.Set("title", tab.title);
      entry.Set("url", tab.url);
      entry.Set("active", tab.active);
      tabs.Append(std::move(entry));
    }
    std::string json;
    if (!base::JSONWriter::Write(tabs, &json)) {
      std::move(callback).Run(Failed("the tab list could not be read"));
      return;
    }
    std::move(callback).Run(Ok(std::move(json)));
    return;
  }

  if (tool == "tabs.open") {
    // url is optional for this tool: no url means a new blank tab.
    GURL url;
    if (arguments.contains("url")) {
      std::optional<GURL> parsed_url = WebUrlArgument(arguments);
      if (!parsed_url) {
        std::move(callback).Run(
            Failed("that is not a web address this browser will open"));
        return;
      }
      url = *parsed_url;
    }
    std::move(callback).Run(surface_->OpenTab(url)
                                ? Ok()
                                : Failed("the tab could not be opened"));
    return;
  }

  if (tool == "tabs.switch" || tool == "tabs.close") {
    std::optional<int> tab_id = arguments.FindInt("tab_id");
    if (!tab_id) {
      std::move(callback).Run(Failed("no tab was named"));
      return;
    }
    // The tab may have closed between the kernel deciding and now. Acting on
    // whatever holds that id today would be acting on a tab nobody approved.
    const bool ok = tool == "tabs.switch" ? surface_->SwitchToTab(*tab_id)
                                          : surface_->CloseTab(*tab_id);
    std::move(callback).Run(ok ? Ok() : Failed("that tab is no longer open"));
    return;
  }

  if (tool == "task.complete" || tool == "task.ask") {
    // These end the turn rather than touching the browser. The agent loop owns
    // what happens next; the executor's job is to report that the call was
    // allowed and carried no browser action.
    const char* field = tool == "task.complete" ? "answer" : "question";
    const std::string* text = arguments.FindString(field);
    if (!text) {
      std::move(callback).Run(Failed("nothing was said"));
      return;
    }
    base::DictValue value;
    value.Set(field, *text);
    std::string json;
    base::JSONWriter::Write(value, &json);
    std::move(callback).Run(Ok(std::move(json)));
    return;
  }

  // Looking at the page. Asynchronous because it goes to the renderer, and the
  // result replaces the stored Observation -- which is what expires every
  // element id the model was previously holding.
  if (tool == "page.observe" || tool == "page.find") {
    const int level = arguments.FindInt("level").value_or(1);
    const std::string* query = arguments.FindString("query");
    surface_->Observe(base::BindOnce(
        &ToolExecutor::OnObserved, weak_factory_.GetWeakPtr(), tool, level,
        query ? *query : std::string(), std::move(callback)));
    return;
  }

  Result result = PerformOnElement(tool, arguments);

  // Entering text is the one action whose effect can be checked cheaply, and
  // the one that has silently failed most. If it claims to have worked, look
  // again before saying so.
  if (result.status == Result::Status::kOk &&
      (tool == "page.type" || tool == "page.select")) {
    const std::string* text =
        arguments.FindString(tool == "page.type" ? "text" : "value");
    if (text && !text->empty()) {
      VerifyEntered(*text, std::move(callback));
      return;
    }
  }

  std::move(callback).Run(std::move(result));
}

void ToolExecutor::VerifyEntered(std::string text, ExecuteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Give the renderer a moment before looking.
  //
  // The keystrokes went out on the input pipe and the snapshot request goes out
  // on another, and nothing orders one against the other. Looking immediately
  // would sometimes photograph the page before it had read its own mail, and a
  // verification that reports failure at random is worse than none.
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ToolExecutor::LookToVerify, weak_factory_.GetWeakPtr(),
                     std::move(text), std::move(callback)),
      base::Milliseconds(250));
}

void ToolExecutor::LookToVerify(std::string text, ExecuteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  surface_->Observe(base::BindOnce(&ToolExecutor::OnVerified,
                                   weak_factory_.GetWeakPtr(), std::move(text),
                                   std::move(callback)));
}

void ToolExecutor::OnVerified(std::string text,
                              ExecuteCallback callback,
                              Observation fresh) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // This look is DISCARDED. `observation_` is deliberately left alone.
  //
  // The model is holding element ids from the Observation it was last shown,
  // and those ids are only meaningful against that Observation. Quietly
  // swapping in a newer one would leave every id it holds pointing at whatever
  // took the same slot -- which is precisely the bug that had the agent
  // clicking a footer link when it asked for a search box. A check is not a
  // look, and must not behave like one.
  bool landed = false;
  for (const ObservedNode& node : fresh.elements) {
    if (!node.value.empty() && node.value.find(text) != std::string::npos) {
      landed = true;
      break;
    }
  }

  if (landed) {
    std::move(callback).Run(Ok());
    return;
  }

  // Say what was checked, not just that it failed. "It did not go in" sends the
  // model to try the same thing again; naming the field and the text gives it
  // something to change.
  std::move(callback).Run(
      Failed("that did not go in -- nothing on the page holds \"" + text +
             "\" now. Look at the page and check you picked the right field."));
}

ToolExecutor::Result ToolExecutor::PerformOnElement(
    const std::string& tool,
    const base::DictValue& arguments) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (tool == "page.scroll") {
    const std::string* direction = arguments.FindString("direction");
    const std::string* amount = arguments.FindString("amount");
    if (!direction || !amount) {
      return Failed("the scroll was not described");
    }
    return surface_->ScrollPage(*direction == "down", *amount)
               ? Ok()
               : Failed("the page did not scroll");
  }

  if (tool == "page.press") {
    const std::string* key = arguments.FindString("key");
    if (!key) {
      return Failed("no key was named");
    }
    return surface_->PressKey(*key) ? Ok() : Failed("the key had no effect");
  }

  if (tool == "selection.read") {
    base::DictValue value;
    value.Set("selection", surface_->ReadSelection());
    std::string json;
    base::JSONWriter::Write(value, &json);
    return Ok(std::move(json));
  }

  const std::string* element_id = arguments.FindString("element_id");
  if (!element_id) {
    return Failed("no element was named");
  }

  // Resolving through our own Observation is what makes an issued id mean
  // something. An id we did not issue resolves to nothing, whatever it looks
  // like.
  const ObservedNode* node = observation_.Find(*element_id);
  if (!node) {
    return Failed("that element is not on the page -- look at it first");
  }

  // The page can be replaced between looking and acting. A matching id in a new
  // tree is a different element, so this refuses rather than acting on it. The
  // contract says ids expire when the page changes; this is where that happens.
  if (observation_.tree_id != surface_->CurrentTreeId()) {
    return Failed("the page has changed since it was last looked at");
  }

  if (tool == "page.click") {
    return surface_->ClickNode(*node)
               ? Ok()
               : Failed("that could not be clicked");
  }

  if (tool == "page.type" || tool == "page.select") {
    const std::string* text =
        arguments.FindString(tool == "page.type" ? "text" : "value");
    if (!text) {
      return Failed("nothing was given to enter");
    }

    // Refuse a target that cannot hold text, and say what it is.
    //
    // This is what stalled a real task. A page can have a text field and a
    // button with the SAME accessible name -- a search box and its magnifying
    // glass are both "Search" -- and setting a value on the button does
    // nothing at all. The accessibility action reports no result, so the
    // executor said "ok" every time while the box stayed empty, and the model
    // had no way to learn it had picked the wrong one. Naming the role turns a
    // silent no-op into something it can act on.
    if (!IsTextEntryRole(node->role)) {
      // Before refusing, look for something with the SAME NAME that can hold
      // text, and use that instead.
      //
      // This is not guesswork, it is disambiguation. YouTube offers a button
      // and a search box both called "Search", so "type into Search" has
      // exactly one sensible reading and the model has no way to express which
      // one it meant beyond the name it was given. Refusing cost a real run
      // several steps, and worse: the model then CLICKED the button, which
      // submitted an empty search, changed the page, and invalidated every
      // element id it was holding.
      const ObservedNode* typeable = nullptr;
      for (const ObservedNode* candidate : observation_.Matching(node->name)) {
        if (candidate->name == node->name && IsTextEntryRole(candidate->role)) {
          typeable = candidate;
          break;
        }
      }
      if (!typeable) {
        return Failed("\"" + node->name + "\" is a " + node->role +
                      ", not something you can type into");
      }
      node = typeable;
    }

    // Refuse a field that is scrolled out of view, and say why.
    //
    // Filling one would half-work, which is the worst outcome available. The
    // value would land, because kSetValue does not care where the element is --
    // but the pointer click that focuses it would be skipped, because an
    // offscreen element's bounds have been clipped to the edge it went behind
    // and clicking them would press on something else. The model would be told
    // "ok", type its search term, press Enter, and watch nothing happen.
    //
    // Scrolling first is a step it can actually take, and page.scroll works.
    if (node->offscreen) {
      return Failed("\"" + node->name +
                    "\" is scrolled out of view -- scroll to it first");
    }

    // page.type types; page.select picks. Two mechanisms, because a native
    // <select> cannot be typed into and a framework search box cannot be
    // assigned to.
    const bool done = tool == "page.type"
                          ? surface_->TypeIntoNode(*node, *text)
                          : surface_->SetNodeValue(*node, *text);
    return done ? Ok() : Failed("that could not be filled in");
  }

  // Unreachable for the V1 contract: all eighteen tools are handled above. It
  // fires only if the contract grows a tool this executor has not learned, and
  // saying so plainly is the point -- an agent told "done" for something that
  // never happened builds its next step on a lie.
  return Failed("this browser cannot do that yet: " + tool);
}

void ToolExecutor::OnObserved(std::string tool,
                              int level,
                              std::string query,
                              ExecuteCallback callback,
                              Observation observation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Replacing it is what expires the previous ids. Nothing else has to
  // remember to invalidate them.
  observation_ = std::move(observation);

  if (tool == "page.find") {
    base::ListValue matches;
    for (const ObservedNode* node : observation_.Matching(query)) {
      base::DictValue entry;
      entry.Set("id", node->id);
      entry.Set("role", node->role);
      entry.Set("name", node->name);
      matches.Append(std::move(entry));
    }
    std::string json;
    base::JSONWriter::Write(matches, &json);
    std::move(callback).Run(Ok(std::move(json)));
    return;
  }

  std::move(callback).Run(Ok(observation_.ToJson(level)));
}

}  // namespace zephyrus::agent
