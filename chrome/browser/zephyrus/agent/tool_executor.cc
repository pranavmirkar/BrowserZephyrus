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
#include "base/strings/string_util.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "chrome/browser/zephyrus/agent/sanitizer.h"
#include "url/gurl.h"

namespace zephyrus::agent {
namespace {

// True if two addresses name the same place.
//
// Lenient on purpose. youtube.com redirecting to www.youtube.com/ is the site
// working normally, not a failed navigation, and reporting it as one would teach
// the model to distrust a tool that had done exactly what it asked.
bool SameDestination(const GURL& wanted, const GURL& actual) {
  if (!wanted.is_valid() || !actual.is_valid()) {
    return false;
  }
  // GURL hands back string_view in this tree, so these are explicit copies.
  const std::string want_host(wanted.host());
  const std::string got_host(actual.host());
  const bool same_host = want_host == got_host ||
                         base::EndsWith(got_host, "." + want_host) ||
                         base::EndsWith(want_host, "." + got_host);

  std::string want_path(wanted.path());
  std::string got_path(actual.path());
  while (want_path.size() > 1 && want_path.back() == '/') {
    want_path.pop_back();
  }
  while (got_path.size() > 1 && got_path.back() == '/') {
    got_path.pop_back();
  }
  if (want_path == "/") {
    want_path.clear();
  }
  if (got_path == "/") {
    got_path.clear();
  }
  return same_host && want_path == got_path;
}

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

// What to say when page.find matched nothing.
//
// "[]" is the least useful true answer available. It says "your query was
// wrong, try another one", and a model does exactly that -- MEASURED, twice, on
// two different sites in one run:
//
//   page.find "price"             -> []
//   page.find "cheapest RTX 4090" -> []
//   page.find "RTX 4090 price"    -> []
//
// The page had two things on it, both covered: a "Close" button and a "Zoom In
// On Image" button. An image viewer had opened over the product page and
// nothing was ever going to match, however the query was phrased. The way out
// was one Escape, and the browser knew the whole time.
//
// So the empty answer carries the reason. A model cannot infer "an overlay is
// open" from an empty list, and it does not have to: the browser can see it.
// A name, short enough to sit in a sentence.
std::string ShortName(const std::string& name) {
  constexpr size_t kMax = 40;
  if (name.size() <= kMax) {
    return name;
  }
  // Cut on a space so a truncated name reads as a name, and never mid-codepoint
  // -- this string goes into JSON.
  size_t cut = name.rfind(' ', kMax);
  if (cut == std::string::npos || cut < kMax / 2) {
    cut = kMax;
    while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) {
      --cut;
    }
  }
  return name.substr(0, cut) + "...";
}

std::string NothingMatched(const std::string& query,
                           const Observation& observation) {
  std::string note = base::StrCat({"nothing here matches \"", query, "\". "});

  if (observation.elements.empty()) {
    return note +
           "There is nothing on this page to act on at all, which usually "
           "means it has not finished loading. Look again before deciding it "
           "is empty.";
  }

  // Every single one behind something else. One element hidden is ordinary --
  // a sticky header, a tooltip. ALL of them is a page that is not the page any
  // more.
  bool everything_covered = true;
  for (const ObservedNode& node : observation.elements) {
    if (!node.obscured) {
      everything_covered = false;
      break;
    }
  }

  base::StrAppend(&note, {"The page has ",
                          base::NumberToString(observation.elements.size()),
                          observation.elements.size() == 1 ? " thing on it"
                                                           : " things on it"});
  if (everything_covered) {
    base::StrAppend(&note, {", every one of them covered by something on top"});
  }

  int shown = 0;
  for (const ObservedNode& node : observation.elements) {
    if (shown >= 3) {
      break;
    }
    base::StrAppend(&note, {shown == 0 ? ": " : ", ", node.id, " \"",
                            ShortName(node.name), "\""});
    ++shown;
  }

  if (everything_covered) {
    base::StrAppend(
        &note,
        {". Something is open OVER the page -- a viewer, a dialog, a banner. "
         "No query will match past it. Close it first: page.press with key "
         "\"Escape\", or click whatever dismisses it."});
  } else {
    base::StrAppend(&note, {". Call page.observe to see all of them."});
  }
  return note;
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
    element->sensitivity = node.sensitivity;
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
    // Already there. Going again cannot change anything.
    //
    // A real run navigated to the same search page TEN TIMES in a row while
    // standing on it, because nothing said no and every attempt reported
    // success. The repeat guard in the loop did not catch it: it needs the
    // Observation to be identical, and a page like a results list is never
    // quite identical between loads.
    //
    // This does not need the pages to match, only the addresses -- which is
    // exactly the case that is provably pointless.
    if (SameDestination(GURL(surface_->GetActiveUrl()), *url)) {
      std::move(callback).Run(
          Failed("you are already on that page. Look at what it shows and act "
                 "on something that is on it."));
      return;
    }

    if (!surface_->Navigate(*url)) {
      std::move(callback).Run(Failed("the page did not load"));
      return;
    }
    VerifyArrived(url->spec(), std::move(callback));
    return;
  }

  // Back, forward and reload all used to answer the instant the load STARTED.
  //
  // browser.navigate had already been given a check (VerifyArrived) and these
  // three had not, which left the history moves reporting success about a
  // journey that had not happened yet. Two consequences, and the second is the
  // expensive one:
  //
  //  - "went back" entered the history before the browser had gone anywhere,
  //    so a move that was refused -- a page that immediately sends you forward
  //    again, a restored entry that fails to load -- was recorded as a success
  //    the model then reasoned from.
  //  - The model was never told WHERE it had arrived. Going back is only ever
  //    a means to something, and a result that does not name the page it
  //    reached makes the model look again to find out, which costs a step.
  //
  // The look these share settles: it waits out a load in flight and then waits
  // for the page to stop changing, with a timeout, so a page that never
  // finishes loading ends the wait rather than the task.
  if (tool == "browser.back") {
    const std::string from = surface_->GetActiveUrl();
    if (!surface_->GoBack()) {
      std::move(callback).Run(Failed("there is nothing to go back to"));
      return;
    }
    VerifyMoved("going back", from, std::move(callback));
    return;
  }

  if (tool == "browser.forward") {
    const std::string from = surface_->GetActiveUrl();
    if (!surface_->GoForward()) {
      std::move(callback).Run(Failed("there is nothing to go forward to"));
      return;
    }
    VerifyMoved("going forward", from, std::move(callback));
    return;
  }

  if (tool == "browser.reload") {
    if (!surface_->Reload()) {
      std::move(callback).Run(Failed("the page could not be reloaded"));
      return;
    }
    // A reload lands where it started, so there is nothing to compare. What
    // the wait buys here is that "reloaded" is true when it is said, rather
    // than a claim about a page still on its way.
    VerifyMoved("reloading", std::string(), std::move(callback));
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
    //
    // "about:blank" counts as no url. It is the name of the blank page, so it
    // is what a model reaches for when asked to open a blank tab, and refusing
    // it meant refusing the exact thing the tool already does. MEASURED: a user
    // typed "can you open a new tab" and got back "that is not a web address
    // this browser will open", twice, and the task was abandoned.
    //
    // This widens nothing. The tab that opens is the same blank tab the no-url
    // path opens; browser.navigate's gate is untouched, and every other scheme
    // is still refused below.
    GURL url;
    const std::string* asked = arguments.FindString("url");
    const bool wants_a_blank_tab = !asked || asked->empty() ||
                                   *asked == "about:blank" ||
                                   *asked == "about:newtab";
    if (!wants_a_blank_tab) {
      std::optional<GURL> parsed_url = WebUrlArgument(arguments);
      if (!parsed_url) {
        // Say what WOULD work. A refusal that only says no leaves the model
        // rewriting the argument, which is the syntax churn this harness has
        // paid for before.
        std::move(callback).Run(Failed(
            "that is not a web address this browser will open. tabs.open takes "
            "an http or https address, or no url at all for a blank tab."));
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
      // An empty ANSWER is not a failure to finish; an empty QUESTION is.
      //
      // The contract stopped requiring an answer -- finishing without a summary
      // is still finishing -- and this check went on refusing it, so a
      // completed task was rejected for not describing itself. Asking the user
      // nothing at all is different: there is no question to put in front of
      // them, so there is nothing for them to answer.
      if (tool == "task.complete") {
        base::DictValue done;
        done.Set("answer", "");
        std::string empty_json;
        base::JSONWriter::Write(done, &empty_json);
        std::move(callback).Run(Ok(std::move(empty_json)));
        return;
      }
      std::move(callback).Run(Failed("there was no question to ask"));
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
    auto observed = base::BindOnce(
        &ToolExecutor::OnObserved, weak_factory_.GetWeakPtr(), tool, level,
        query ? *query : std::string(), std::move(callback));
    if (tool == "page.find" && query) {
      surface_->ObserveForFind(*query, std::move(observed));
    } else {
      surface_->Observe(std::move(observed));
    }
    return;
  }

  if (tool == "page.click" || tool == "page.type" || tool == "page.select") {
    const std::string* id = arguments.FindString("element_id");
    const ObservedNode* expected = id ? observation_.Find(*id) : nullptr;
    if (!expected) {
      std::move(callback).Run(Failed("that element is not on the page"));
      return;
    }
    surface_->ObserveForCheck(base::BindOnce(
        &ToolExecutor::OnElementReady, weak_factory_.GetWeakPtr(), tool,
        arguments.Clone(), *expected, std::move(callback)));
    return;
  }
  DispatchElement(tool, arguments.Clone(), std::move(callback));
}

void ToolExecutor::OnElementReady(std::string tool,
                                  base::DictValue arguments,
                                  ObservedNode expected,
                                  ExecuteCallback callback,
                                  Observation fresh) {
  if (fresh.tree_id != observation_.tree_id || fresh.url != observation_.url) {
    std::move(callback).Run(Failed("the page changed while the model was thinking; choose from the next observation"));
    return;
  }
  const ObservedNode* match = nullptr;
  for (const auto& node : fresh.elements) {
    if (node.name != expected.name || node.role != expected.role ||
        node.sensitivity != expected.sensitivity) {
      continue;
    }
    if (match) {
      std::move(callback).Run(Failed("the target is ambiguous after the page updated; use page.find to choose a distinct target"));
      return;
    }
    match = &node;
  }
  if (!match) {
    std::move(callback).Run(Failed("the target disappeared while the model was thinking; choose from the next observation"));
    return;
  }
  // Refresh the authorized target's geometry, keeping every model-issued id
  // bound to its original element. A slow inference must not click old pixels.
  for (auto& node : observation_.elements) {
    if (node.id == expected.id) {
      node.bounds = match->bounds;
      node.offscreen = match->offscreen;
      node.ax_id = match->ax_id;
      break;
    }
  }

  // Let the renderer settle before synthesising input.
  //
  // The staleness check above is a full look at the page, which crosses to the
  // renderer and back. Sending fake mouse events the instant it returns loses
  // them: the page's own listeners see nothing, while Chromium's injector
  // aimed at the same coordinates hits the element -- measured by
  // ClickingReachesAListenerOnAServedPage, which regressed the moment this
  // check was added in front of the dispatch.
  //
  // The same shape cost this code a working click once before, when
  // WebContents::Focus() sat here; removing that call is what made clicking
  // work at all. Anything that touches the page immediately before synthetic
  // input appears to swallow it.
  //
  // A quarter of a second, matching the pause VerifyEntered already takes after
  // typing for the same reason. It is paid once per element action, and it buys
  // the check that stops the agent clicking pixels that have moved.
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ToolExecutor::DispatchElement,
                     weak_factory_.GetWeakPtr(), tool, std::move(arguments),
                     std::move(callback)),
      base::Milliseconds(250));
}

void ToolExecutor::DispatchElement(const std::string& tool,
                                   base::DictValue arguments,
                                   ExecuteCallback callback) {
  Result result = PerformOnElement(tool, arguments);
  if (result.status == Result::Status::kOk && tool == "page.click") {
    surface_->ObserveForCheck(base::BindOnce(
        &ToolExecutor::OnClickChecked, weak_factory_.GetWeakPtr(),
        std::move(callback)));
    return;
  }

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

void ToolExecutor::OnClickChecked(ExecuteCallback callback, Observation fresh) {
  base::DictValue evidence;
  evidence.Set("input_dispatched", true);
  evidence.Set("url", fresh.url);
  evidence.Set("title", fresh.title);
  evidence.Set("media_playing", fresh.media_playing);
  // A dispatched click is not proof that the user's task succeeded.
  evidence.Set("verification", "Check the next observation for the requested effect.");
  std::string json;
  base::JSONWriter::Write(evidence, &json);
  std::move(callback).Run(Ok(std::move(json)));
}

void ToolExecutor::VerifyArrived(std::string wanted, ExecuteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Looking settles, so this waits for the load rather than racing it.
  surface_->ObserveForCheck(base::BindOnce(&ToolExecutor::OnArrived,
                                          weak_factory_.GetWeakPtr(),
                                          std::move(wanted),
                                          std::move(callback)));
}

void ToolExecutor::OnArrived(std::string wanted,
                             ExecuteCallback callback,
                             Observation fresh) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Discarded, like every other check: replacing observation_ would move every
  // element id the model is holding out from under it.
  const GURL asked(wanted);
  const GURL landed(fresh.url);

  if (SameDestination(asked, landed)) {
    std::move(callback).Run(Ok());
    return;
  }

  // The page loaded and then rewrote its own address.
  //
  // Sites do this constantly -- stripping a tracking parameter, canonicalising
  // a path, a single-page app moving to its first view -- and none of it means
  // the navigation failed. Judged on the document that was actually fetched,
  // the request plainly succeeded.
  //
  // Found by a test written for something else: a served page called
  // replaceState on load, and the harness told the model "that address did not
  // open -- addresses cannot be guessed", about an address it had just opened
  // correctly. That is the most damaging thing it could have said, because it
  // is the sentence that sends a model looking somewhere else.
  const GURL document(fresh.document_url);
  if (document.is_valid() && SameDestination(asked, document)) {
    std::move(callback).Run(Ok());
    return;
  }

  // Say where it actually is, and say what to do instead. A model that guessed
  // an address needs to learn that guessing is what failed, or it guesses again.
  std::move(callback).Run(Failed(
      "that address did not open -- you are on " + fresh.url +
      " instead. Addresses cannot be guessed. Open a page by clicking a link "
      "that is actually on the page."));
}

void ToolExecutor::VerifyMoved(std::string what,
                               std::string from,
                               ExecuteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  surface_->ObserveForCheck(base::BindOnce(&ToolExecutor::OnMoved,
                                           weak_factory_.GetWeakPtr(),
                                           std::move(what), std::move(from),
                                           std::move(callback)));
}

void ToolExecutor::OnMoved(std::string what,
                           std::string from,
                           ExecuteCallback callback,
                           Observation fresh) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Discarded like every other check, so the ids the model is holding keep
  // pointing at the page it was shown.
  //
  // An empty `from` means there was nothing to compare -- a reload -- and the
  // wait alone was the point.
  if (!from.empty() && SameDestination(GURL(from), GURL(fresh.url))) {
    std::move(callback).Run(Failed(
        what + " did not move the page -- you are still on " + fresh.url +
        ". Reach a different page by clicking a link that is on this one."));
    return;
  }
  // Name the page. A history move is a means to something, and a result that
  // does not say where it arrived costs the model a step to find out.
  //
  // As JSON, because that is what the field is: value_json is what the model
  // reads back on success, and tabs.list already fills it that way. A bare
  // sentence would render, and would be the one value in there that is not
  // what the field says it is.
  base::DictValue landed;
  landed.Set("url", fresh.url);
  landed.Set("title", fresh.title);
  std::string json;
  if (!base::JSONWriter::Write(landed, &json)) {
    std::move(callback).Run(Ok());
    return;
  }
  std::move(callback).Run(Ok(std::move(json)));
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
  surface_->ObserveForCheck(base::BindOnce(&ToolExecutor::OnVerified,
                                          weak_factory_.GetWeakPtr(),
                                          std::move(text),
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

    // page.select picks from a list; it is not typing, and must not be judged
    // as if it were.
    //
    // Both tools shared the whole block below, so choosing an option was
    // checked against the roles that can hold TEXT. A native <select> holds no
    // text, so a perfectly good call came back "\"Price: Low to High\" is a
    // option, not something you can type into" -- an error about typing, for a
    // tool that does not type, naming the value rather than the target. Traced:
    // the model tried four times and the run was called stuck.
    if (tool == "page.select") {
      if (node->offscreen) {
        return Failed("\"" + node->name +
                      "\" is scrolled out of view -- scroll to it first");
      }
      // An <option> is a plausible thing for a model to aim at, and the thing
      // that actually takes the value is the list it belongs to. Say so rather
      // than refusing flatly.
      if (node->role == "option" || node->role == "menuitem") {
        return Failed(
            "\"" + node->name +
            "\" is one of the choices, not the control that holds them. Pick "
            "the list itself and pass this as the value.");
      }
      return surface_->SetNodeValue(*node, *text)
                 ? Ok()
                 : Failed("that choice did not take -- check the list and the "
                          "exact wording of the option");
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
      // Not when the name is nothing but a mask.
      //
      // The disambiguation below rests entirely on the name meaning something:
      // two controls called "Search" are two halves of one search box. Once
      // redaction has replaced a name that WAS the private thing, several
      // unrelated elements can end up called exactly "[redacted]" -- and then
      // "the other element with the same name" is not disambiguation, it is
      // picking one at random and typing the model's text into it.
      if (node->name != kRedactedMarker && !node->name.empty()) {
        for (const ObservedNode* candidate : observation_.Matching(node->name)) {
          if (candidate->name == node->name && IsTextEntryRole(candidate->role)) {
            typeable = candidate;
            break;
          }
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

    // Only page.type reaches here now; select returned above.
    return surface_->TypeIntoNode(*node, *text)
               ? Ok()
               : Failed("that could not be filled in");
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
    if (matches.empty()) {
      base::DictValue answer;
      answer.Set("matched", base::ListValue());
      answer.Set("note", NothingMatched(query, observation_));
      std::string json;
      base::JSONWriter::Write(answer, &json);
      std::move(callback).Run(Ok(std::move(json)));
      return;
    }

    std::string json;
    base::JSONWriter::Write(matches, &json);
    std::move(callback).Run(Ok(std::move(json)));
    return;
  }

  std::move(callback).Run(Ok(observation_.ToJson(level)));
}

}  // namespace zephyrus::agent
