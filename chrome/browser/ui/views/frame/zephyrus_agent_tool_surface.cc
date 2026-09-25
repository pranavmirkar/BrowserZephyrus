// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_agent_tool_surface.h"

#include "chrome/browser/zephyrus/buildflags/dev_switches.h"
#include "build/buildflag.h"
#include <tuple>

#include "base/memory/raw_ptr.h"
#include "base/command_line.h"
#include "chrome/browser/zephyrus/agent/dev_model_client.h"
#include "chrome/browser/zephyrus/agent/sanitizer.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/browser_accessibility_state.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/scoped_accessibility_mode.h"
#include "base/timer/timer.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/global_request_id.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_tree_update.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/types/event_type.h"
#include "url/gurl.h"

namespace zephyrus::agent {
namespace {

ZephyrusWorkspaceManager* WorkspaceManagerFor(Browser* browser) {
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser);
  return view ? view->zephyrus_workspace_manager() : nullptr;
}

int TabIdFor(content::WebContents* contents) {
  return sessions::SessionTabHelper::IdForTab(contents).id();
}


// One key, in the three forms Blink wants it.
struct NamedKey {
  ui::KeyboardCode key_code;
  ui::DomCode dom_code;
  ui::DomKey dom_key;
};

// The keys the contract allows, and nothing else.
//
// A closed list is the whole security story for this tool. The agent names a
// key from the contract's enum; it cannot describe a keystroke. There is no way
// to ask for a modifier, so there is no Ctrl+W, no Alt+F4, and no browser
// shortcut -- only the six keys a page needs to be usable.
//
// A lookup rather than a table because ui::DomKey's constants are runtime
// values, and a namespace-scope array of them would need a global constructor.
//
// Case-insensitive, because a model writes `enter` as readily as `Enter` and
// being strict about it taught it nothing -- it just burned a step on a refusal
// it could not act on. The list stays closed, which is where the security is.
std::optional<NamedKey> KeyByName(const std::string& name) {
  if (base::EqualsCaseInsensitiveASCII(name, "Enter")) {
    return NamedKey{ui::VKEY_RETURN, ui::DomCode::ENTER, ui::DomKey::ENTER};
  }
  if (base::EqualsCaseInsensitiveASCII(name, "Escape")) {
    return NamedKey{ui::VKEY_ESCAPE, ui::DomCode::ESCAPE, ui::DomKey::ESCAPE};
  }
  if (base::EqualsCaseInsensitiveASCII(name, "Tab")) {
    return NamedKey{ui::VKEY_TAB, ui::DomCode::TAB, ui::DomKey::TAB};
  }
  if (base::EqualsCaseInsensitiveASCII(name, "ArrowUp")) {
    return NamedKey{ui::VKEY_UP, ui::DomCode::ARROW_UP, ui::DomKey::ARROW_UP};
  }
  if (base::EqualsCaseInsensitiveASCII(name, "ArrowDown")) {
    return NamedKey{ui::VKEY_DOWN, ui::DomCode::ARROW_DOWN,
                    ui::DomKey::ARROW_DOWN};
  }
  if (base::EqualsCaseInsensitiveASCII(name, "Backspace")) {
    return NamedKey{ui::VKEY_BACK, ui::DomCode::BACKSPACE,
                    ui::DomKey::BACKSPACE};
  }
  return std::nullopt;
}

// How long a page gets to settle before it is observed anyway.
//
// A page that never stops loading is common -- a live stream, an ad that polls,
// a site with a long-running request. Waiting forever would hang the task, so
// the timeout is what keeps "wait for the page" from becoming "wait".
constexpr base::TimeDelta kLoadSettleTimeout = base::Seconds(4);

// How the browser decides a page has finished reacting.
//
// Every wait used to key off a LOAD, and a single-page app never loads: on
// youtube.com a click swaps the DOM and fetches in the background while
// IsLoading() stays false throughout. The snapshot was therefore taken of the
// page as it had been BEFORE the click, and the model was handed a photograph
// of the past labelled as the present. Measured -- see
// LookingWaitsForThePageToReact.
//
// So look more than once and stop when two consecutive looks agree. Stability
// is the signal, because it is the one that does not depend on the page
// announcing anything.
//
// The floor is the load-bearing part, and stability only refines it.
//
// Stability alone answers too early, because a page that has not STARTED
// reacting is perfectly stable -- two identical looks 150ms apart prove nothing
// while the answer is still in flight. Chromium offers no per-WebContents
// signal for a request that is merely outstanding (ResourceLoadComplete fires
// when one FINISHES), so there is nothing to wait on and the wait has to be
// chosen rather than derived.
//
// 400ms, spent only after the agent has acted. Consecutive looks at an
// untouched page pay nothing, and a tool call itself costs about 3ms against a
// model call of several seconds, so this is a small fraction of a step.
//
// A page that reacts later than this is not lost. The Observation says whether
// it was still changing when the browser stopped waiting, so the model can look
// again -- which is the recoverable version of the failure, and much better
// than a confident photograph of the wrong moment.
constexpr base::TimeDelta kSettleFloor = base::Milliseconds(400);
constexpr base::TimeDelta kSettleInterval = base::Milliseconds(150);
constexpr base::TimeDelta kActionIsRecent = base::Seconds(3);

// A page that never stops moving -- a carousel, a clock, a live view -- would
// otherwise be waited on forever. At this point take what is there and go.
constexpr int kMaxSettleRounds = 8;

// The budget when the page still looks EXACTLY as it did before the agent
// acted, or a navigation is in flight.
//
// Stability was being used as a proxy for "my action took effect", and the two
// are not the same. Immediately after a click the page is still the old page
// and perfectly stable, so two looks 150ms apart agreed with each other and the
// browser called it settled -- on the page the click was meant to leave. The
// model was then told "nothing on the page changed", which is not a missing
// signal but a wrong one: it reads as "that click failed, try something else".
//
// Watched on youtube.com: the agent opened the correct video, was told nothing
// had happened, went back to the results page and clicked it again, until the
// twenty-step budget ran out. It played the right video several times over.
//
// Bounded by kActionIsRecent in practice, so a click that genuinely changes
// nothing costs about three seconds once and then reports the truth -- which is
// the trade worth making, because the alternative is reporting a falsehood
// immediately and losing the whole task to it.
constexpr int kMaxSettleRoundsAfterAction = 24;

// How long to keep looking after a click or a key press before accepting that
// the page is not going to navigate.
//
// Byte-identity was the wrong test and it is worth saying why, because it looked
// right. The first version kept waiting only while the page was EXACTLY as the
// model last saw it -- but a click that is about to navigate usually changes
// something trivial first: a ripple, a focus ring, a title tweak. Any of those
// makes the page "different", the wait ends, and the model is handed the
// pre-navigation page with "nothing on the page changed" attached. The rule has
// to be about the NAVIGATION, not about whether some pixel moved.
//
// Only clicks and key presses pay this, and only until the URL actually moves,
// so the common case -- a click that navigates -- settles as soon as it lands
// and costs nothing extra. A click that opens a menu pays the full grace once.
constexpr base::TimeDelta kGraceAfterNavigatingAction = base::Seconds(2.5);

// How long a page counts as still ARRIVING after its address changes.
//
// The mirror image of the wait above, and the one that actually lost a task.
// That wait runs until the URL moves; this one runs from the moment it moved.
// A single-page app changes its address FIRST and builds the page afterwards,
// so the instant the old wait ends is the instant the page is emptiest.
//
// MEASURED, on the run this was written for. The agent searched, opened the
// Sidemen channel, read eleven upload ages and clicked the newest -- "8 days
// ago" -- which is exactly the task it was given. It arrived at the right
// video and was handed this:
//
//     title: "YouTube"        (not the video's title -- the site's)
//     text:  "INSkip navigationsidemen latestSign in"
//     10 elements, one of them a covered "Play" button
//
// Nothing there says a video was reached, so the model went back to the search
// results and tried again. Four times. The same URL, looked at later in the
// same run, had 30 elements and the title "SIDEMEN $100,000 USA ROAD TRIP".
// The model was right every time and the browser threw the answer away.
//
// Chromium was no help: IsLoading() was already false and no fetch was in
// flight, because the document had finished and the application had not. Two
// looks 150ms apart agreed with each other, and agreement was being read as
// completion.
constexpr base::TimeDelta kGraceAfterArriving = base::Seconds(3);

// Consecutive looks that must agree before a just-arrived page is believed.
//
// One round of agreement is enough for a page sitting still and is not enough
// for one being built, because construction has gaps: a frame where the next
// batch of nodes has not landed looks exactly like a frame where there is no
// next batch. Three in a row is 450ms of quiet, which construction does not
// have and a finished page pays without noticing.
//
// A ceiling, not a wait. A page that is genuinely done agrees immediately and
// leaves after those three looks; only a page that keeps changing spends the
// whole window above.
constexpr int kStableRoundsOnArrival = 3;

// What "the page changed" means for settling: the things the model is shown.
// Pixels move constantly and mean nothing here.
std::string SignatureOf(const Observation& observation) {
  // Plain separators, not control bytes: this string exists to be compared, and
  // a source file carrying invisible characters is a source file nobody can
  // read. Collisions would only cost an early stop, never a wrong action.
  std::string signature = observation.url + " >< " + observation.title;
  for (const ObservedNode& node : observation.elements) {
    signature += " >< " + node.role + " :: " + node.name + " :: " + node.value;
  }
  return signature;
}

}  // namespace

// Notices the page fetching things.
//
// Stability alone is not enough to know a page has finished reacting, and the
// reason is simple once seen: a page that has not STARTED reacting is perfectly
// stable. Two identical looks 150ms apart prove nothing if the answer is still
// on its way.
//
// A real single-page app tells us anyway, just not through anything that looks
// like a page load. Clicking a result on youtube.com issues a fetch, and a
// finished subresource is a signal that more is probably about to change. So
// settling waits while the page is still talking to the network, and stability
// decides only once it has gone quiet.
class BrowserToolSurface::FetchWatcher : public content::WebContentsObserver {
 public:
  FetchWatcher(content::WebContents* contents, BrowserToolSurface* owner)
      : content::WebContentsObserver(contents), owner_(owner) {}

  void ResourceLoadComplete(
      content::RenderFrameHost*,
      const content::GlobalRequestID&,
      const blink::mojom::ResourceLoadInfo&) override {
    last_fetch_at_ = base::TimeTicks::Now();
  }

  // A navigation that has started and not yet finished.
  //
  // Same-document navigations count, and that is the point: a single-page app
  // moving from a results list to a video does it with the History API, so
  // there is no load, no new document, and nothing in ResourceLoadComplete to
  // notice. Without this the browser cannot tell "the page has not reacted yet"
  // from "the page is not going to react".
  void DidStartNavigation(content::NavigationHandle*) override {
    ++navigations_in_flight_;
  }

  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (navigations_in_flight_ > 0) {
      --navigations_in_flight_;
    }
    // A real document, in the tab the agent is driving.
    //
    // Same-document navigations are excluded deliberately: pushState and
    // replaceState move the address without loading anything, and it is
    // precisely that move which was being mistaken for a failed navigation.
    if (handle && handle->IsInPrimaryMainFrame() && handle->HasCommitted() &&
        !handle->IsSameDocument()) {
      owner_->last_document_url_ = handle->GetURL().spec();
    }
  }

  bool NavigationPending() const { return navigations_in_flight_ > 0; }

  bool BusyWithin(base::TimeDelta window) const {
    return !last_fetch_at_.is_null() &&
           base::TimeTicks::Now() - last_fetch_at_ < window;
  }

 private:
  const raw_ptr<BrowserToolSurface> owner_;
  base::TimeTicks last_fetch_at_;
  int navigations_in_flight_ = 0;
};

// Runs `done` when the page stops loading, or when it has waited long enough.
class BrowserToolSurface::LoadWaiter : public content::WebContentsObserver {
 public:
  LoadWaiter(content::WebContents* contents, base::OnceClosure done)
      : content::WebContentsObserver(contents), done_(std::move(done)) {
    timer_.Start(FROM_HERE, kLoadSettleTimeout,
                 base::BindOnce(&LoadWaiter::Finish, base::Unretained(this)));
  }

  void DidStopLoading() override { Finish(); }

  // A tab that went away is not going to finish loading. Answering is still
  // required: the Observation callback above it must run exactly once.
  void WebContentsDestroyed() override { Finish(); }

 private:
  void Finish() {
    timer_.Stop();
    Observe(nullptr);
    if (done_) {
      std::move(done_).Run();
    }
  }

  base::OnceClosure done_;
  base::OneShotTimer timer_;
};

BrowserToolSurface::BrowserToolSurface(Browser* browser) : browser_(browser) {}

BrowserToolSurface::~BrowserToolSurface() = default;

content::WebContents* BrowserToolSurface::ActiveContents() const {
  return browser_->tab_strip_model()->GetActiveWebContents();
}

bool BrowserToolSurface::InCurrentWorkspace(
    content::WebContents* contents) const {
  ZephyrusWorkspaceManager* manager = WorkspaceManagerFor(browser_);
  if (!manager) {
    // No workspace manager means no workspaces in this window, so there is
    // nothing to be outside of.
    return true;
  }
  // The manager's own predicate rather than comparing ids here: it also
  // carries the pinning rule, and a second implementation of "is this tab in
  // this workspace" is a second thing to get wrong.
  return manager->IsContentsInCurrentWorkspace(contents);
}

std::string BrowserToolSurface::GetActiveUrl() {
  content::WebContents* contents = ActiveContents();
  if (!contents) {
    return std::string();
  }
  // The last committed URL, not the visible one. The visible URL can be a
  // pending navigation the user typed, and the kernel uses this to decide
  // whether a destination is expected -- it should reflect where the page
  // actually is, not where it might be going.
  return contents->GetLastCommittedURL().spec();
}

std::vector<ToolSurface::TabInfo> BrowserToolSurface::ListTabs() {
  std::vector<TabInfo> tabs;
  TabStripModel* model = browser_->tab_strip_model();
  content::WebContents* active = model->GetActiveWebContents();

  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (!contents || !InCurrentWorkspace(contents)) {
      continue;
    }
    TabInfo info;
    info.id = TabIdFor(contents);
    info.title = base::UTF16ToUTF8(contents->GetTitle());
    info.url = contents->GetLastCommittedURL().spec();
    info.active = contents == active;
    tabs.push_back(std::move(info));
  }
  return tabs;
}

content::WebContents* BrowserToolSurface::FindTabInCurrentWorkspace(
    int tab_id) const {
  TabStripModel* model = browser_->tab_strip_model();
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (contents && TabIdFor(contents) == tab_id &&
        InCurrentWorkspace(contents)) {
      return contents;
    }
  }
  return nullptr;
}

bool BrowserToolSurface::Navigate(const GURL& url) {
  find_query_.clear();
  // Definitionally a navigation, so the look that follows must wait for it.
  //
  // Only clicks and key presses were marked, which left the one action that is
  // ALWAYS a navigation without the grace: browser.navigate looked before the
  // load committed and reported "the page did not load" for a page that was on
  // its way. Traced on a real run, first step of the task.
  last_action_could_navigate_ = true;
  last_action_at_ = base::TimeTicks::Now();
  content::WebContents* contents = ActiveContents();
  if (!contents) {
    return false;
  }
  NavigateParams params(browser_, url, ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  params.disposition = WindowOpenDisposition::CURRENT_TAB;
  ::Navigate(&params);
  return true;
}

bool BrowserToolSurface::GoBack() {
  last_action_could_navigate_ = true;
  last_action_at_ = base::TimeTicks::Now();
  content::WebContents* contents = ActiveContents();
  if (!contents || !contents->GetController().CanGoBack()) {
    return false;
  }
  contents->GetController().GoBack();
  return true;
}

bool BrowserToolSurface::GoForward() {
  last_action_could_navigate_ = true;
  last_action_at_ = base::TimeTicks::Now();
  content::WebContents* contents = ActiveContents();
  if (!contents || !contents->GetController().CanGoForward()) {
    return false;
  }
  contents->GetController().GoForward();
  return true;
}

bool BrowserToolSurface::Reload() {
  last_action_could_navigate_ = true;
  last_action_at_ = base::TimeTicks::Now();
  content::WebContents* contents = ActiveContents();
  if (!contents) {
    return false;
  }
  contents->GetController().Reload(content::ReloadType::NORMAL,
                                   /*check_for_repost=*/true);
  return true;
}

bool BrowserToolSurface::OpenTab(const GURL& url) {
  last_action_could_navigate_ = true;
  last_action_at_ = base::TimeTicks::Now();
  // An empty URL means a blank new tab, which is what the contract's optional
  // url argument asks for.
  NavigateParams params(browser_, url.is_empty() ? GURL("about:blank") : url,
                        ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  ::Navigate(&params);
  return true;
}

bool BrowserToolSurface::SwitchToTab(int tab_id) {
  last_action_could_navigate_ = true;
  last_action_at_ = base::TimeTicks::Now();
  content::WebContents* contents = FindTabInCurrentWorkspace(tab_id);
  if (!contents) {
    return false;
  }
  const int index =
      browser_->tab_strip_model()->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab) {
    return false;
  }
  browser_->tab_strip_model()->ActivateTabAt(index);
  return true;
}

bool BrowserToolSurface::CloseTab(int tab_id) {
  content::WebContents* contents = FindTabInCurrentWorkspace(tab_id);
  if (!contents) {
    return false;
  }
  const int index =
      browser_->tab_strip_model()->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab) {
    return false;
  }
  browser_->tab_strip_model()->CloseWebContentsAt(
      index, TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  return true;
}


void BrowserToolSurface::EnsureAccessibility() {
  content::WebContents* contents = ActiveContents();
  if (!contents) {
    return;
  }
  if (accessibility_ && accessible_contents_ == contents) {
    return;
  }
  accessible_contents_ = contents;
  accessibility_ =
      content::BrowserAccessibilityState::GetInstance()
          ->CreateScopedModeForWebContents(contents,
                                           ui::kAXModeWebContentsOnly);
}

void BrowserToolSurface::ObserveForCheck(ObserveCallback callback) {
  remember_this_look_ = false;
  Observe(std::move(callback));
}

void BrowserToolSurface::Observe(ObserveCallback callback) {
  EnsureAccessibility();
  content::WebContents* contents = ActiveContents();
  if (!contents) {
    // No page is an empty Observation, not an error. It offers nothing, so
    // every element id the model could name is ungrounded and gets refused.
    std::move(callback).Run(Observation());
    return;
  }

  // Let a page in flight settle first. Observing mid-navigation hands the
  // model element ids from a document that is about to be replaced, and by the
  // time it has decided what to do with them they refer to nothing.
  settle_rounds_ = 0;
  settle_signature_.clear();
  // Starts listening now, which is enough: the fetches that matter are the ones
  // the agent's own action just caused.
  fetch_watcher_ = std::make_unique<FetchWatcher>(contents, this);

  if (contents->IsLoading()) {
    load_waiter_ = std::make_unique<LoadWaiter>(
        contents, base::BindOnce(&BrowserToolSurface::TakeSnapshot,
                                 weak_factory_.GetWeakPtr(),
                                 std::move(callback)));
    return;
  }

  // If the agent has just acted, give the page a moment before the first look.
  // Sampling immediately catches it before it has started, and a page that has
  // not started is indistinguishable from one that has finished.
  const base::TimeDelta since = base::TimeTicks::Now() - last_action_at_;
  if (since < kSettleFloor && since < kActionIsRecent) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&BrowserToolSurface::TakeSnapshot,
                       weak_factory_.GetWeakPtr(), std::move(callback)),
        kSettleFloor - since);
    return;
  }

  TakeSnapshot(std::move(callback));
}

void BrowserToolSurface::ObserveForFind(const std::string& query,
                                       ObserveCallback callback) {
  find_query_ = query;
  Observe(std::move(callback));
}

void BrowserToolSurface::TakeSnapshot(ObserveCallback callback) {
  load_waiter_.reset();

  content::WebContents* contents = ActiveContents();
  if (!contents) {
    std::move(callback).Run(Observation());
    return;
  }

  // NOT RequestAXTreeSnapshotWithinBrowserProcess. That reads the trees the
  // browser already holds, which would guarantee our ids are ids an action can
  // address -- but what it returns is not a standalone serialisable tree, and
  // feeding it to AXTree::Unserialize hits a NOTREACHED and yields an empty
  // Observation. Measured, then reverted.
  //
  // kAXModeWebContentsOnly rather than kAXModeComplete: we want the page's own
  // tree, not the platform-facing one, and asking for less keeps the snapshot
  // cheaper on pages with a lot of nodes.
  contents->RequestAXTreeSnapshot(
      base::BindOnce(&BrowserToolSurface::OnSnapshot, weak_factory_.GetWeakPtr(),
                     std::move(callback), contents->GetLastCommittedURL().spec(),
                     base::UTF16ToUTF8(contents->GetTitle())),
      ui::kAXModeWebContentsOnly,
      /*max_nodes=*/5000,
      /*timeout=*/base::Seconds(3),
      content::WebContents::AXTreeSnapshotPolicy::kSameOriginDirectDescendants);
}

void BrowserToolSurface::OnSnapshot(ObserveCallback callback,
                                    std::string url,
                                    std::string title,
                                    ui::AXTreeUpdate& update) {
  if (has_answered_ && url != answered_url_) {
    find_query_.clear();
  }
  Observation observation = BuildObservation(
      update, url, title, find_query_.empty() ? kMaxObservedElements : 5000,
      kMaxObservedTextLength);
  if (!find_query_.empty()) {
    const auto matches = observation.Matching(find_query_);
    if (matches.empty()) {
      // Nothing matched, so show the page rather than nothing at all.
      //
      // Replacing the elements with an empty list here handed the model a page
      // with no controls on it -- indistinguishable from a blank document --
      // and because the query survives until a navigation or a click, the step
      // after that was blank too. The only ways out are a navigate or a click,
      // and a click needs an element it can no longer see.
      //
      // The query is dropped as well: a filter that matches nothing is not
      // worth carrying into the next look.
      find_query_.clear();

      // The budget was raised to 5000 for the search. Put it back, keeping the
      // content-first order BuildObservation already placed them in, and say
      // that it was cut.
      if (observation.elements.size() > kMaxObservedElements) {
        observation.elements.resize(kMaxObservedElements);
        observation.truncated = true;
      }
    } else {
      std::vector<ObservedNode> selected;
      for (const ObservedNode* match : matches) {
        if (selected.size() == kMaxObservedElements) {
          break;
        }
        selected.push_back(*match);
      }
      observation.elements = std::move(selected);
    }
  }

  // What the page is doing, not just what is on it.
  if (content::WebContents* contents = ActiveContents()) {
    // Either signal, not one overriding the other.
    //
    // They answer different halves of the question and neither is complete.
    // IsCurrentlyAudible() measures sound actually leaving the tab, which a
    // muted autoplaying video does not produce; the media session knows about
    // that video and can also go stale, still reporting the last thing it was
    // told after the page has moved on. Letting the session REPLACE the
    // measurement meant a genuinely audible video was reported silent whenever
    // the session disagreed.
    //
    // So: playing if anything says it is playing. An overclaim here costs a
    // model one wasted look; an underclaim costs it the completed task it
    // cannot tell it has finished.
    observation.media_playing = contents->IsCurrentlyAudible();
    if (auto* session = content::MediaSession::GetIfExists(contents)) {
      auto info = session->GetMediaSessionInfoSync();
      if (info && info->playback_state ==
                      media_session::mojom::MediaPlaybackState::kPlaying) {
        observation.media_playing = true;
      }
    }
    observation.loading = contents->IsLoading();
    observation.document_url = last_document_url_;
  }

  // Look again unless this look matched the last one. The first look never
  // matches, so a page is always seen at least twice before it is believed.
  const std::string signature = SignatureOf(observation);

  // When the address last moved under us, which is when arrival started.
  if (observation.url != url_when_last_looked_) {
    url_when_last_looked_ = observation.url;
    arrived_at_ = base::TimeTicks::Now();
  }
  const bool just_arrived =
      !arrived_at_.is_null() &&
      base::TimeTicks::Now() - arrived_at_ < kGraceAfterArriving;

  const bool still_fetching =
      fetch_watcher_ && fetch_watcher_->BusyWithin(kSettleInterval * 2);
  const bool navigating =
      fetch_watcher_ && fetch_watcher_->NavigationPending();

  // A click or a key press has happened and the address has not moved yet.
  //
  // This is the case the whole fix is about. A single-page app decides to
  // navigate in its own time -- on youtube.com, a moment after the click -- and
  // there is no signal for "a navigation is about to start". So the only honest
  // thing is to keep looking for a bounded while and see whether one does.
  //
  // Judged on the URL rather than on the page looking identical, because a page
  // about to navigate rarely looks identical: it puts up a spinner, moves the
  // focus ring, tweaks its title. Watching for those and calling it "changed"
  // is what handed the model the pre-navigation page in the first place.
  const bool awaiting_navigation =
      last_action_could_navigate_ && has_answered_ &&
      observation.url == answered_url_ &&
      base::TimeTicks::Now() - last_action_at_ < kGraceAfterNavigatingAction;

  // How many looks have agreed in a row, rather than whether this one did.
  //
  // Arrival is judged on a COARSER signature than settling is, and the reason
  // is a page with a clock on it. The full signature carries every name and
  // value, so a live view count, a countdown or an unread badge changes it on
  // every look and it never agrees with itself -- and demanding three
  // agreements from a page like that would spend the whole window, then report
  // "still loading", then be looked at three more times by the loop. A page
  // that ticks would have become the slowest thing the agent does.
  //
  // What arrival is actually asking is "has the rest of the page shown up
  // yet", and that is a question about how MUCH is there, not what it says.
  // The measured failure was ten elements becoming thirty under a title that
  // went from "YouTube" to the video's own. A count and a title catch exactly
  // that, and a ticking number moves neither.
  const std::string arrival_signature = base::StrCat(
      {observation.url, " >< ", observation.title, " >< ",
       base::NumberToString(observation.elements.size())});

  bool stable = false;
  if (just_arrived) {
    if (arrival_signature == arrival_signature_) {
      ++stable_rounds_;
    } else {
      arrival_signature_ = arrival_signature;
      stable_rounds_ = 0;
    }
    stable = stable_rounds_ >= kStableRoundsOnArrival;
    // Kept current so the ordinary rule below resumes cleanly afterwards.
    settle_signature_ = signature;
  } else {
    stable = signature == settle_signature_;
    settle_signature_ = signature;
  }

  const int rounds_allowed =
      (navigating || awaiting_navigation || just_arrived)
          ? kMaxSettleRoundsAfterAction
          : kMaxSettleRounds;

  const bool still_moving =
      !stable || still_fetching || navigating || awaiting_navigation;

  if (still_moving && settle_rounds_ < rounds_allowed) {
    ++settle_rounds_;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&BrowserToolSurface::TakeSnapshot,
                       weak_factory_.GetWeakPtr(), std::move(callback)),
        kSettleInterval);
    return;
  }

  settle_rounds_ = 0;
  stable_rounds_ = 0;
  settle_signature_.clear();
  arrival_signature_.clear();
  fetch_watcher_.reset();

  // Say so when the waiting ran out on a page that had not finished.
  //
  // This is the half that matters. Waiting longer helps a page that settles
  // within the window and does nothing for one that does not, and there will
  // always be one that does not. What makes THAT recoverable is admitting it:
  // the loop already re-observes a page marked loading, up to three times, and
  // that machinery sat idle through the whole failed run because it was told
  // the half-built page was finished.
  //
  // An honest "still building" costs a model nothing to handle. A confident
  // photograph of an empty page costs it the task.
  observation.loading = observation.loading || still_moving;

  // The page has stopped moving, so this is the moment worth photographing.
  CaptureScreenshot(std::move(observation), std::move(callback));
}

// The widest a screenshot is allowed to be, in pixels.
//
// A model charges by the pixel, roughly, and a 2000px-wide capture buys nothing
// a 1024px one does not -- the text a page uses for labels is still legible and
// the layout, which is the reason to look at all, is unchanged. Bounding this is
// what keeps the second channel from undoing the prompt cut that took a step
// from 43 seconds to 9.
constexpr int kMaxScreenshotWidth = 1024;

// Quality for the JPEG. High enough that small text survives, low enough that
// the bytes stay sane; screenshots compress well because they are mostly flat.
constexpr int kScreenshotQuality = 72;

void BrowserToolSurface::CaptureScreenshot(Observation observation,
                                           ObserveCallback callback) {
  // Off unless asked for. When it is off this costs one flag read, and the
  // Observation goes out exactly as it did before vision existed.
  if (!zephyrus::DevSwitchesEnabled() ||
      !base::CommandLine::ForCurrentProcess()->HasSwitch(
          kAgentVisionSwitch)) {
    Answer(std::move(observation), std::move(callback));
    return;
  }

  content::WebContents* contents = ActiveContents();
  content::RenderWidgetHostView* view =
      contents ? contents->GetRenderWidgetHostView() : nullptr;
  if (!view || !view->IsSurfaceAvailableForCopy()) {
    // No picture is not an error. The Observation still describes the page, and
    // a task that stops because a screenshot was unavailable would be worse
    // than one that carries on with the channel that does work.
    Answer(std::move(observation), std::move(callback));
    return;
  }

  const gfx::Size full = view->GetVisibleViewportSize();
  // Kept so the masks can be scaled to the downscaled picture later.
  observation.viewport = full;
  gfx::Size wanted = full;
  if (full.width() > kMaxScreenshotWidth && full.width() > 0) {
    wanted = gfx::Size(
        kMaxScreenshotWidth,
        std::max(1, full.height() * kMaxScreenshotWidth / full.width()));
  }

  view->CopyFromSurface(
      gfx::Rect(), wanted, base::Seconds(2),
      base::BindOnce(&BrowserToolSurface::OnScreenshot,
                     weak_factory_.GetWeakPtr(), std::move(observation),
                     std::move(callback)));
}

void BrowserToolSurface::OnScreenshot(
    Observation observation,
    ObserveCallback callback,
    const content::CopyFromSurfaceResult& result) {
  if (!result.has_value() || result.value().bitmap.drawsNothing()) {
    // No picture is not an error; the Observation still describes the page.
    Answer(std::move(observation), std::move(callback));
    return;
  }

  SkBitmap bitmap = result.value().bitmap;

  // Paint over the private parts BEFORE encoding, so no unredacted image ever
  // exists as bytes anywhere -- not in a buffer, not in a temporary, not on the
  // wire. Masking after encoding would mean the whole picture had already been
  // made once.
  //
  // The regions come from the same classification that redacts the text, so the
  // two channels cannot disagree about what is private. A picture that still
  // showed an address the JSON had masked would be worse than sending neither.
  const std::vector<Redaction> redactions = FindRedactions(observation);
  if (!redactions.empty()) {
    // Element bounds are in the page's own pixels; the capture was scaled down
    // to bound its cost. Without this the masks would land in the wrong place,
    // which on a form means covering a label while leaving the value beside it.
    const gfx::Size full = observation.viewport;
    const double scale_x =
        full.width() > 0 ? static_cast<double>(bitmap.width()) / full.width()
                         : 1.0;
    const double scale_y =
        full.height() > 0 ? static_cast<double>(bitmap.height()) / full.height()
                          : 1.0;

    SkCanvas canvas(bitmap);
    SkPaint paint;
    paint.setColor(SK_ColorBLACK);
    paint.setStyle(SkPaint::kFill_Style);
    for (const Redaction& redaction : redactions) {
      if (redaction.bounds.IsEmpty()) {
        continue;
      }
      const SkRect over = SkRect::MakeXYWH(
          static_cast<float>(redaction.bounds.x() * scale_x),
          static_cast<float>(redaction.bounds.y() * scale_y),
          static_cast<float>(redaction.bounds.width() * scale_x),
          static_cast<float>(redaction.bounds.height() * scale_y));
      canvas.drawRect(over, paint);
    }
  }

  SkPixmap pixmap;
  if (bitmap.peekPixels(&pixmap)) {
    std::optional<std::vector<uint8_t>> encoded =
        gfx::JPEGCodec::Encode(pixmap, kScreenshotQuality);
    if (encoded) {
      observation.screenshot_jpeg = std::move(*encoded);
    }
  }

  DescribeScreenshot(std::move(observation), std::move(callback));
}

void BrowserToolSurface::DescribeScreenshot(Observation observation,
                                            ObserveCallback callback) {
  // Built once. In the ordinary case this is null and stays null, so the whole
  // vision path costs one bool after the first Observation.
  if (!vision_checked_) {
    vision_checked_ = true;
    vision_ = LocalVisionClient::CreateIfConfigured(
        browser_->profile()->GetURLLoaderFactory());
  }

  if (!vision_ || observation.screenshot_jpeg.empty()) {
    Answer(std::move(observation), std::move(callback));
    return;
  }

  // Described on EVERY step that has a picture, rather than only when the
  // accessibility tree looks thin.
  //
  // Thinness was the obvious trigger and it is the wrong one: the cases where
  // sight actually helps are not the sparse pages but the busy ones -- a dialog
  // sitting over a full page, a video playing behind a banner, a form that is
  // really an image. Those have plenty of elements, so a thin-tree test would
  // skip exactly the pages worth looking at.
  //
  // The switch is the gate instead. Someone who asked for vision gets vision,
  // and pays a predictable cost per step rather than an unpredictable one.
  std::vector<uint8_t> jpeg = observation.screenshot_jpeg;
  vision_->Describe(
      jpeg, base::BindOnce(&BrowserToolSurface::OnDescribed,
                           weak_factory_.GetWeakPtr(), std::move(observation),
                           std::move(callback)));
}

void BrowserToolSurface::OnDescribed(Observation observation,
                                     ObserveCallback callback,
                                     std::string summary) {
  observation.vision_summary = std::move(summary);

  // The picture has done its work and is dropped here.
  //
  // Everything downstream of this point -- the mojo hop to the kernel, the
  // prompt, the cloud model -- receives the DESCRIPTION and never the image.
  // Clearing it is what makes that structural rather than a promise about who
  // reads which field: there is nothing left to send.
  observation.screenshot_jpeg.clear();

  Answer(std::move(observation), std::move(callback));
}

void BrowserToolSurface::Answer(Observation observation,
                                ObserveCallback callback) {
  // The private values go here, at the one door every Observation leaves by.
  //
  // This was written, tested and never called. The picture was masked and the
  // TEXT was not, so an address that had been carefully painted out of the
  // screenshot was sitting in the JSON beside it -- and the JSON is the half
  // that actually goes to a model. A single exit is what makes it impossible to
  // add a fifth route that forgets.
  //
  // After the mask, not before: the mask needs the real values to know what to
  // paint over and where.
  RedactObservation(observation);

  // Belt and braces on the promise that the picture never leaves. OnDescribed
  // clears it, but that is only one of the ways out -- vision switched on with
  // no model configured reaches here with the bytes still attached. Cleared at
  // the door instead, so the guarantee does not depend on the route.
  observation.screenshot_jpeg.clear();

  // Said last, so it compares the finished Observation against the finished
  // one before it -- after settling, after redaction, after the description.
  if (has_previous_) {
    observation.DescribeChangeFrom(previous_);
  }

  // Only a look the model is shown may move the baseline.
  //
  // An internal check runs after an action, so letting it stand in for "what
  // the model last saw" makes the next real look report no change for a step
  // that changed everything.
  if (!remember_this_look_) {
    remember_this_look_ = true;
    std::move(callback).Run(std::move(observation));
    return;
  }

  // Kept for next time. The elements are what the comparison needs; the
  // picture is already gone by here and the description is not worth diffing.
  previous_ = Observation();
  previous_.url = observation.url;
  previous_.title = observation.title;
  previous_.media_playing = observation.media_playing;
  previous_.elements.reserve(observation.elements.size());
  for (const ObservedNode& node : observation.elements) {
    ObservedNode copy;
    copy.name = node.name;
    // The VALUE too. Keeping only names made typing invisible: the box now
    // holds "sidemen", every name is exactly what it was, and the change
    // report said nothing happened. Filling something in is the commonest way
    // an agent changes a page without changing its shape.
    //
    // Safe to keep, because this runs after redaction -- what is stored here is
    // the masked value the model was shown, never the real one.
    copy.value = node.value;
    previous_.elements.push_back(std::move(copy));
  }
  has_previous_ = true;

  // What the model is about to be shown. The next settle compares against it to
  // tell "the page has not reacted yet" from "the page did not react".
  answered_signature_ = SignatureOf(observation);
  answered_url_ = observation.url;
  has_answered_ = true;

  // The grace above is spent, whatever happened. Without this a later look on
  // an unrelated step would still think it was waiting for that click.
  last_action_could_navigate_ = false;

  std::move(callback).Run(std::move(observation));
}

ui::AXTreeID BrowserToolSurface::CurrentTreeId() {
  content::WebContents* contents = ActiveContents();
  if (!contents || !contents->GetPrimaryMainFrame()) {
    return ui::AXTreeIDUnknown();
  }
  return contents->GetPrimaryMainFrame()->GetAXTreeID();
}

bool BrowserToolSurface::PerformAction(ax::mojom::Action action,
                                       ui::AXNodeID node,
                                       const std::string& value) {
  last_action_at_ = base::TimeTicks::Now();
  // An action sent to a tab with no live accessibility tree is dropped by the
  // renderer without a word, so this is what makes the difference between
  // clicking something and only appearing to.
  EnsureAccessibility();
  content::WebContents* contents = ActiveContents();
  if (!contents || !contents->GetPrimaryMainFrame()) {
    return false;
  }
  // Send to the frame the node actually belongs to.
  //
  // The Observation spans same-origin subframes (kSameOriginDirectDescendants),
  // so a node can come from one -- and an action addressed to the main frame
  // with a subframe's node id names nothing there. The renderer drops it
  // without a word, which looks exactly like an action that ran and did
  // nothing.
  content::RenderFrameHost* target = contents->GetPrimaryMainFrame();
  contents->GetPrimaryMainFrame()->ForEachRenderFrameHost(
      [&](content::RenderFrameHost* frame) {
        if (frame->GetAXTreeID() == node_tree_id_) {
          target = frame;
        }
      });

  ui::AXActionData data;
  data.action = action;
  data.target_node_id = node;
  data.target_tree_id = target->GetAXTreeID();
  data.value = value;
  target->AccessibilityPerformAction(data);
  return true;
}

bool BrowserToolSurface::MoveAndClick(const gfx::Rect& bounds) {
  find_query_.clear();
  // A click and a key press are the two things that make a page decide to go
  // somewhere. Typing and scrolling do not, and must not pay the grace.
  last_action_could_navigate_ = true;
  last_action_at_ = base::TimeTicks::Now();
  content::WebContents* contents = ActiveContents();
  if (!contents || !contents->GetPrimaryMainFrame()) {
    return false;
  }
  // The VIEW's widget, not the main frame's.
  //
  // Measured: forwarding to the main frame's RenderWidgetHost delivered nothing
  // at all on an ordinary served page -- no mousedown, no mouseup, no click --
  // while Chromium's own SimulateMouseClickAt at the identical point delivered
  // all three to the right element. The coordinates were never the problem; the
  // widget was. Chromium's helper targets the view, so this does too.
  content::RenderWidgetHostView* host_view = contents->GetRenderWidgetHostView();
  content::RenderWidgetHost* widget =
      host_view ? host_view->GetRenderWidgetHost() : nullptr;
  if (!widget) {
    return false;
  }
  // NO Focus() HERE, and it took a long time to learn why.
  //
  // Calling WebContents::Focus() immediately before dispatching SWALLOWS the
  // mouse events. Measured, with a control both ways: with it, an ordinary
  // served page received no mousedown, no mouseup and no click at all, while
  // Chromium's own SimulateMouseClickAt at the identical point delivered all
  // three; without it, ours delivers. On Windows that call moves native focus,
  // and events dispatched into that transition are lost.
  //
  // It was intermittent, which is what made it expensive: the same test passed
  // under parallel load and failed when run alone and fast, because it is a
  // race rather than a rejection. Nothing in ForwardMouseEvent drops them.
  //
  // Focusing was never needed anyway. A click is what focuses a page -- that is
  // what clicking DOES -- so the call was doing no work and costing the events.

  // Accessibility bounds are in DEVICE pixels; a mouse event wants DIP.
  //
  // Measured, after the click missed on a real page twice: aiming at an element
  // whose CSS position was 550,342 delivered a click at 825,513 -- exactly 1.5x
  // on both axes, on a display running at 150% scaling. Every click was landing
  // that far right and down, which on a real page is a different element or no
  // element at all.
  //
  // It survived two rounds of testing because the earlier test's field sat at
  // the top-left of the page, where 1.5x of a small coordinate is still inside
  // the element. A test whose subject is near the origin cannot see a scale
  // error; ClickLandsOnTheElementNotNearIt exists to make sure one always can.
  const float scale =
      host_view ? host_view->GetDeviceScaleFactor() : 1.0f;
  const gfx::Point at = gfx::ToRoundedPoint(gfx::ScalePoint(
      gfx::PointF(bounds.CenterPoint()), scale > 0 ? 1.0f / scale : 1.0f));

  const gfx::Rect container = contents->GetContainerBounds();
  const base::TimeTicks now = ui::EventTimeForNow();

  auto at_target = [&](blink::WebInputEvent::Type type) {
    blink::WebMouseEvent event(type, blink::WebInputEvent::kNoModifiers, now);
    event.button = blink::WebMouseEvent::Button::kLeft;
    event.click_count = 1;
    // Widget coordinates, which is what the tree's bounds are already in: both
    // are the frame's own space with scroll offsets applied.
    event.SetPositionInWidget(at.x(), at.y());
    event.SetPositionInScreen(at.x() + container.x(), at.y() + container.y());
    return event;
  };

  // Move, then press, then release -- all three, in that order.
  //
  // The move is not decoration. A page that reveals its real control on hover,
  // or that tracks the pointer to decide what a press means, has to see the
  // cursor arrive before the press does; a press out of nowhere lands on
  // whatever the page last thought was under the pointer. And a press without
  // its release leaves the page believing a button is still held down.
  blink::WebMouseEvent move =
      at_target(blink::WebInputEvent::Type::kMouseMove);
  move.button = blink::WebMouseEvent::Button::kNoButton;
  move.click_count = 0;
  widget->ForwardMouseEvent(move);
  widget->ForwardMouseEvent(at_target(blink::WebInputEvent::Type::kMouseDown));
  widget->ForwardMouseEvent(at_target(blink::WebInputEvent::Type::kMouseUp));
  return true;
}

bool BrowserToolSurface::ClickNode(const ObservedNode& node) {
  // A real pointer, NOT kDoDefault -- because the node id we hold does not
  // address the node we mean.
  //
  // `RequestAXTreeSnapshot` runs its result through `ui::AXTreeCombiner`, and
  // `AXTreeCombiner::MapId` renumbers EVERY node sequentially (`next_id_++`) as
  // it assembles the combined tree. The ids in an Observation are therefore
  // combiner-local counters with no relationship to the renderer's real ids, so
  // an accessibility action addressed with one acts on whatever node happens to
  // hold that number.
  //
  // On a real page that is not subtle: asking to click "Search" on youtube.com
  // activated the Copyright link in the footer. On a small test page the
  // renumbering comes out as the identity, which is why every browsertest here
  // passed while real sites behaved at random -- and why this looked for weeks
  // like a bad model rather than a bug.
  //
  // Bounds do not have this problem. They are per-node data that the combiner
  // carries across correctly, so a coordinate is the one thing in an
  // Observation that still means something after the renumbering.
  if (node.offscreen || node.bounds.IsEmpty()) {
    return false;
  }
  return MoveAndClick(node.bounds);
}

bool BrowserToolSurface::SendKey(content::RenderWidgetHost* widget,
                                 ui::KeyboardCode key_code,
                                 ui::DomCode dom_code,
                                 ui::DomKey dom_key,
                                 int flags) {
  // Both halves, always: some handlers run on keyup, and a press with no
  // release leaves the page believing a key is still held.
  const base::TimeTicks now = ui::EventTimeForNow();
  const ui::KeyEvent press(ui::EventType::kKeyPressed, key_code, dom_code,
                           flags, dom_key, now);
  const ui::KeyEvent release(ui::EventType::kKeyReleased, key_code, dom_code,
                             flags, dom_key, now);
  widget->ForwardKeyboardEvent(input::NativeWebKeyboardEvent(press));
  widget->ForwardKeyboardEvent(input::NativeWebKeyboardEvent(release));
  return true;
}

bool BrowserToolSurface::TypeCharacter(content::RenderWidgetHost* widget,
                                       char16_t character) {
  const ui::DomKey dom_key = ui::DomKey::FromCharacter(character);
  const ui::DomCode dom_code = ui::UsLayoutDomKeyToDomCode(dom_key);

  // Shift for a capital, so the keystroke reads the way a real one would to a
  // page that inspects modifiers before deciding whether to swallow a key.
  int flags = ui::EF_NONE;
  if (base::IsAsciiUpper(character)) {
    flags |= ui::EF_SHIFT_DOWN;
  }

  // Recover the legacy key code from the physical key. Pages still read
  // event.keyCode, and a zero there is a keystroke that does not look real.
  ui::DomKey unused = dom_key;
  ui::KeyboardCode key_code = ui::VKEY_UNKNOWN;
  if (dom_code != ui::DomCode::NONE) {
    std::ignore =
        ui::DomCodeToUsLayoutDomKey(dom_code, flags, &unused, &key_code);
  }

  const base::TimeTicks now = ui::EventTimeForNow();
  const ui::KeyEvent press(ui::EventType::kKeyPressed, key_code, dom_code,
                           flags, dom_key, now);
  // The character event is the one that actually inserts. The press and
  // release around it are what the page's own handlers listen for, and a page
  // that only ever sees an insertion is a page that thinks nobody typed.
  const ui::KeyEvent typed =
      ui::KeyEvent::FromCharacter(character, key_code, dom_code, flags, now);
  const ui::KeyEvent release(ui::EventType::kKeyReleased, key_code, dom_code,
                             flags, dom_key, now);

  widget->ForwardKeyboardEvent(input::NativeWebKeyboardEvent(press));
  widget->ForwardKeyboardEvent(input::NativeWebKeyboardEvent(typed));
  widget->ForwardKeyboardEvent(input::NativeWebKeyboardEvent(release));
  return true;
}

bool BrowserToolSurface::TypeIntoNode(const ObservedNode& node,
                                      const std::string& text) {
  // Type, rather than assign a value. This is the third attempt at this bug and
  // the first that stops using the accessibility layer to ACT.
  //
  // Measured on youtube.com: kSetValue put the text in the field and fired the
  // page's `input` event, and the search box still reported itself empty and
  // refused to submit -- because a search box built as a framework component
  // watches its own key events rather than reading .value. No keydown ever
  // happened, so as far as it was concerned nobody had typed. The synthetic
  // test missed this for two rounds because a bare <input> has no such opinion.
  //
  // There is a second reason, found while proving the first. The click and the
  // accessibility action travel DIFFERENT mojo channels, so they were
  // unordered: the test page recorded `value:` before `focus:`. Keystrokes and
  // the click share the input channel, which is ordered, so the race is gone
  // rather than merely unlikely.
  content::WebContents* contents = ActiveContents();
  if (!contents || !contents->GetPrimaryMainFrame()) {
    return false;
  }
  content::RenderWidgetHost* widget =
      contents->GetPrimaryMainFrame()->GetRenderWidgetHost();
  if (!widget) {
    return false;
  }

  // Nowhere to aim means no way to focus, and keystrokes sent at an unfocused
  // page land on the document. Failing is the honest answer; the executor
  // refuses an offscreen element before it ever gets here, and this is the same
  // rule for a node with no bounds at all.
  if (node.offscreen || node.bounds.IsEmpty() || !MoveAndClick(node.bounds)) {
    return false;
  }

  // Select what is already in the field so the text replaces it -- but ONLY if
  // there is something to replace.
  //
  // Ctrl+A is safe inside a focused field and destructive outside one: if the
  // click missed, it selects the whole document instead. That is exactly what a
  // real run on youtube.com did, and the page turning entirely blue is how the
  // missed click was finally spotted. Sending it only when the field actually
  // holds text keeps the common case (an empty search box) from ever reaching
  // for it.
  if (!node.value.empty()) {
    SendKey(widget, ui::VKEY_A, ui::DomCode::US_A,
            ui::DomKey::FromCharacter('a'), ui::EF_CONTROL_DOWN);
  }

  for (const char16_t character : base::UTF8ToUTF16(text)) {
    TypeCharacter(widget, character);
  }
  return true;
}

bool BrowserToolSurface::SetNodeValue(const ObservedNode& node,
                                      const std::string& value) {
  // page.select: choose one of a fixed set of values.
  //
  // This used to be an accessibility action, which meant it addressed the node
  // by id -- and the id in an Observation is a counter invented by
  // ui::AXTreeCombiner, not the renderer's node id (see ClickNode). So it could
  // set a value on an entirely unrelated element, silently. That is fixed the
  // same way clicking was: coordinates, then the input pipeline.
  //
  // A <select> matches options by typed prefix once it has focus, and Enter
  // commits the highlighted one. Whether those keys actually reach the popup
  // Chromium opens is a platform question, not a guess worth making -- see
  // SelectingChoosesTheOptionByName, which is what decides it.
  content::WebContents* contents = ActiveContents();
  if (!contents || !contents->GetPrimaryMainFrame()) {
    return false;
  }
  content::RenderWidgetHost* widget =
      contents->GetPrimaryMainFrame()->GetRenderWidgetHost();
  if (!widget) {
    return false;
  }
  if (node.offscreen || node.bounds.IsEmpty() || !MoveAndClick(node.bounds)) {
    return false;
  }

  for (const char16_t character : base::UTF8ToUTF16(value)) {
    TypeCharacter(widget, character);
  }
  SendKey(widget, ui::VKEY_RETURN, ui::DomCode::ENTER, ui::DomKey::ENTER,
          ui::EF_NONE);
  return true;
}

bool BrowserToolSurface::ScrollPage(bool down, const std::string& amount) {
  // `amount` is part of the tool contract but the accessibility actions are
  // page-at-a-time only. Honouring the direction and ignoring the distance is
  // the honest reading; a scroll that goes the right way but not as far is a
  // step the agent can repeat.
  return PerformAction(
      down ? ax::mojom::Action::kScrollDown : ax::mojom::Action::kScrollUp,
      ui::kInvalidAXNodeID, amount);
}

bool BrowserToolSurface::PressKey(const std::string& key) {
  // A click and a key press are the two things that make a page decide to go
  // somewhere. Typing and scrolling do not, and must not pay the grace.
  last_action_could_navigate_ = true;
  last_action_at_ = base::TimeTicks::Now();
  content::WebContents* contents = ActiveContents();
  if (!contents) {
    return false;
  }
  // Same reason as SetNodeValue: the key has to arrive at a page that is
  // actually the focused thing, not at one sitting behind a focused panel.
  contents->Focus();

  // An unknown name is a refusal, not a guess. The kernel already checked this
  // against the contract's enum, so reaching here with something else means the
  // two disagree.
  const std::optional<NamedKey> named = KeyByName(key);
  if (!named) {
    return false;
  }

  content::RenderWidgetHost* widget =
      contents->GetPrimaryMainFrame()->GetRenderWidgetHost();
  if (!widget) {
    return false;
  }

  // A real synthesised keystroke, not an accessibility action. Keys are not
  // something the accessibility API expresses -- kDoDefault on a node activates
  // that node, which is a click, not a keypress. A page listening for keydown,
  // a form submitting on Enter, or a text field handling Backspace all need
  // actual key events, and they need both halves: some handlers run on keyup.
  const base::TimeTicks now = base::TimeTicks::Now();
  const ui::KeyEvent press(ui::EventType::kKeyPressed, named->key_code,
                           named->dom_code, ui::EF_NONE, named->dom_key, now);
  const ui::KeyEvent release(ui::EventType::kKeyReleased, named->key_code,
                             named->dom_code, ui::EF_NONE, named->dom_key, now);

  widget->ForwardKeyboardEvent(input::NativeWebKeyboardEvent(press));
  widget->ForwardKeyboardEvent(input::NativeWebKeyboardEvent(release));
  return true;
}

std::string BrowserToolSurface::ReadSelection() {
  content::WebContents* contents = ActiveContents();
  if (!contents || !contents->GetRenderWidgetHostView()) {
    return std::string();
  }
  return base::UTF16ToUTF8(
      contents->GetRenderWidgetHostView()->GetSelectedText());
}

}  // namespace zephyrus::agent
