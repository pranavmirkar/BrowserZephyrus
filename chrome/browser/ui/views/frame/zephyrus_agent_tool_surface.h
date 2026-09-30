// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_TOOL_SURFACE_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_TOOL_SURFACE_H_

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/zephyrus/agent/pointer_events.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/zephyrus/agent/local_vision_client.h"
#include "chrome/browser/zephyrus/agent/tool_surface.h"
#include "ui/accessibility/ax_enums.mojom-forward.h"
#include "ui/accessibility/ax_tree_id.h"
#include "ui/accessibility/ax_tree_update_forward.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/render_widget_host_view.h"
#include "ui/gfx/geometry/rect.h"

class Browser;

namespace content {
class Page;
class RenderWidgetHost;
class ScopedAccessibilityMode;
class WebContents;
}  // namespace content

namespace zephyrus::agent {

// The real ToolSurface, over one Browser window.
//
// Lives here rather than beside the executor because reaching the workspace
// manager means reaching BrowserView, and //chrome/browser/ui depends on
// //chrome/browser -- putting this in the agent target would close that loop.
//
// **Everything is scoped to the browser's current workspace.** A task is bound
// to one workspace (ADR 0003), so tabs in other workspaces are not listed, not
// switchable and not closeable. That is enforced here, at the only place the
// agent can touch tabs at all, rather than trusted to callers.
class BrowserToolSurface : public ToolSurface {
 public:
  explicit BrowserToolSurface(Browser* browser);
  ~BrowserToolSurface() override;

  BrowserToolSurface(const BrowserToolSurface&) = delete;
  BrowserToolSurface& operator=(const BrowserToolSurface&) = delete;

  // How much of a page the model is shown per step. The defaults were measured
  // on a 7B local model, for which a long list costs accuracy; a cloud model
  // reads far more, and at thirty elements a busy app like Notion spent the
  // whole list on its sidebar before reaching the editor.
  // Show the model each step's page as a labelled screenshot. See
  // model_screenshots_.
  void SetModelScreenshots(bool enabled) { model_screenshots_ = enabled; }

  // Move the pointer and turn the wheel the way a hand does: along a curved,
  // eased path that takes longer for a longer reach, with a pause before the
  // press and a hold before the release; scrolling as a glide of wheel events
  // rather than a jump; typing at a pace instead of all at once. Off by
  // default, so a test drives the surface instantly and a person watching a
  // real run sees a person's motion. Every action still goes through the same
  // input pipeline as a real mouse, in the same order, so a page cannot tell
  // the difference -- which is the point where it matters: menus that open on
  // hover, and pages that watch for a pointer that never moved.
  void SetHumanMotion(bool enabled) { human_motion_ = enabled; }

  // Told where the pointer is and what it is doing, to draw it. Not owned;
  // must outlive this or be cleared with null.
  void SetPointerObserver(PointerObserver* observer) {
    pointer_observer_ = observer;
  }

  void SetObservationBudget(size_t max_elements, size_t max_text_length) {
    max_elements_ = max_elements;
    max_text_length_ = max_text_length;
  }

  // ToolSurface:
  std::string GetActiveUrl() override;
  std::vector<TabInfo> ListTabs() override;
  bool Navigate(const GURL& url) override;
  bool GoBack() override;
  bool GoForward() override;
  bool Reload() override;
  bool OpenTab(const GURL& url) override;
  bool SwitchToTab(int tab_id) override;
  bool CloseTab(int tab_id) override;
  void Observe(ObserveCallback callback) override;
  void ObserveForFind(const std::string& query, ObserveCallback callback) override;
  void ObserveForCheck(ObserveCallback callback) override;
  void ObserveQuick(ObserveCallback callback) override;
  ui::AXTreeID CurrentTreeId() override;
  bool ClickNode(const ObservedNode& node) override;
  bool HoverNode(const ObservedNode& node) override;
  bool TypeIntoNode(const ObservedNode& node,
                    const std::string& text) override;
  bool SetNodeValue(const ObservedNode& node,
                    const std::string& value) override;
  bool ScrollPage(bool down, const std::string& amount) override;
  bool PressKey(const std::string& key) override;
  bool TypeIntoFocus(const std::string& text) override;
  bool ClickAtPoint(const gfx::Point& point) override;
  std::string ReadSelection() override;

 private:
  // The active tab, or null if there is not one.
  content::WebContents* ActiveContents() const;

  // The tab with this session id, but only if it is in the current workspace.
  // Null otherwise -- including when the tab exists in a workspace the agent
  // was not given, which is a refusal rather than an error.
  content::WebContents* FindTabInCurrentWorkspace(int tab_id) const;

  // True if `contents` belongs to the workspace the window is showing.
  bool InCurrentWorkspace(content::WebContents* contents) const;

  // Waits for a still-loading tab to settle, then snapshots it.
  //
  // Observing mid-navigation is what made a real task fail: the loop observes,
  // spends several seconds asking the model, and acts -- and if the page
  // committed a new document in that window, every element id it was shown
  // belongs to a page that is gone. The staleness rule then correctly refuses
  // the action, the model re-navigates, and the task loops until its budget
  // runs out. Waiting here removes the churn at the source rather than
  // loosening the rule that caught it.
  class LoadWaiter;
  class FetchWatcher;

  void TakeSnapshot(ObserveCallback callback);

  // Turns on accessibility for the active tab, and keeps it on.
  //
  // Not optional. RequestAXTreeSnapshot explicitly does NOT change the
  // accessibility mode -- it spins up a snapshotter, reads the tree and tears
  // it down -- so without this there is a tree to LOOK at and no live tree to
  // ACT on, and every click and keystroke is dropped silently by the renderer.
  //
  // Held for as long as this surface exists, which is as long as the agent has
  // the tab. It goes away with the task, so a browser nobody is driving is not
  // paying for accessibility it does not need.
  void EnsureAccessibility();

  // Sends one accessibility action to the active page. False if there is no
  // page to send it to.
  bool PerformAction(ax::mojom::Action action,
                     ui::AXNodeID node,
                     const std::string& value);

  // Puts the pointer on `bounds` and clicks, as a person's hand would: move,
  // press, release. `bounds` comes from an Observation this browser took, never
  // from the agent, so there is no way to ask for a click at an arbitrary point.
  //
  // This is a genuinely different route into the page from PerformAction. It
  // goes through Blink's ordinary input handling rather than the accessibility
  // action path, which matters because that path is where focus is lost.
  bool MoveAndClick(const gfx::Rect& bounds);

  // ---- Ordered input ------------------------------------------------------
  //
  // Everything that reaches the page as input goes through one queue, and
  // nothing looks at the page until the queue is empty. With instant input that
  // is invisible -- each step finishes as it is enqueued. With human motion a
  // click takes most of a second, and this is what keeps the click, the typing
  // that follows it and the look at what happened in the order they were asked
  // for, without any caller having to wait.
  using InputStep = base::OnceCallback<void(base::OnceClosure done)>;
  void EnqueueInput(InputStep step);
  void RunNextInput();
  void WhenInputIdle(base::OnceClosure callback);

  // The steps themselves.
  //
  // Each takes the PAGE that was showing when it was asked for, and does nothing
  // if a different one is showing when its turn comes. With instant input the
  // two are the same moment. With human motion a click is most of a second
  // away, and a page that navigates in that time -- a redirect, a script -- must
  // not receive a click or a keystroke that was judged against the page before
  // it: the policy decision was about THAT page's elements.
  void ClickStep(gfx::Point at,
                 base::WeakPtr<content::Page> page,
                 base::OnceClosure done);
  void TypeStep(bool select_all,
                std::string text,
                base::WeakPtr<content::Page> page,
                base::OnceClosure done);
  void ChooseStep(std::string value,
                  base::WeakPtr<content::Page> page,
                  base::OnceClosure done);
  void ScrollStep(gfx::Point screen_from, float pixels, base::OnceClosure done);
  void HoverStep(gfx::Point at,
                 base::WeakPtr<content::Page> page,
                 base::OnceClosure done);
  void MoveThen(gfx::PointF target, base::OnceClosure then);
  void PressAt(gfx::PointF target, base::OnceClosure done);
  void ReleaseAt(gfx::PointF target, base::OnceClosure done);
  void ScrollGlide(gfx::PointF at, float pixels, base::OnceClosure done);
  void KeyStep(ui::KeyboardCode key_code,
               ui::DomCode dom_code,
               ui::DomKey dom_key,
               int flags,
               base::WeakPtr<content::Page> page,
               base::OnceClosure done);
  base::WeakPtr<content::Page> CurrentPage() const;
  bool IsCurrentPage(const base::WeakPtr<content::Page>& page) const;

  // The widget input goes to, or null.
  content::RenderWidgetHost* InputWidget() const;
  // One mouse event at a point in SCREEN coordinates. False if there is no
  // page, or a move that would land outside it.
  bool SendMouse(blink::WebInputEvent::Type type, gfx::PointF screen);
  void SendWheel(gfx::PointF screen, float delta_y, int phase);
  void Notify(PointerEvent::Kind kind, gfx::PointF screen, int direction = 0);
  // Where a move begins: where the drawn pointer is, if something draws one.
  gfx::PointF PointerStart(gfx::PointF target) const;

  // A repeating tick with the time elapsed since it began, for `duration`, then
  // `finished`. One at a time, because the queue runs one step at a time.
  void RunTimeline(base::TimeDelta duration,
                   base::RepeatingCallback<void(base::TimeDelta)> tick,
                   base::OnceClosure finished);
  void OnTimelineTick();
  void Later(base::TimeDelta delay, base::OnceClosure task);

  // One keystroke, press and release, with whatever modifiers are given.
  bool SendKey(content::RenderWidgetHost* widget,
               ui::KeyboardCode key_code,
               ui::DomCode dom_code,
               ui::DomKey dom_key,
               int flags);

  // One printable character, as the three events a real key produces: press,
  // the character itself, release. The character event is what inserts; the
  // other two are what the page's handlers watch for.
  void TypeText(content::RenderWidgetHost* widget, const std::string& text);
  bool TypeCharacter(content::RenderWidgetHost* widget, char16_t character);

  // Takes a picture of the page, downscaled and JPEG-encoded, or nothing at
  // all when vision is switched off. Asynchronous: it goes to the compositor.
  void CaptureScreenshot(Observation observation, ObserveCallback callback);
  void OnScreenshot(Observation observation,
                    ObserveCallback callback,
                    const content::CopyFromSurfaceResult& result);

  // Asks the LOCAL model what the page looks like, then finishes the
  // Observation with its answer. Skipped entirely when vision is off.
  void DescribeScreenshot(Observation observation, ObserveCallback callback);
  void OnDescribed(Observation observation,
                   ObserveCallback callback,
                   std::string summary);

  // The single exit for every Observation, whatever route it took. Works out
  // what changed and remembers this look for the next comparison.
  void Answer(Observation observation, ObserveCallback callback);

  void OnSnapshot(ObserveCallback callback,
                  std::string url,
                  std::string title,
                  ui::AXTreeUpdate& update);

  // When the agent last did something to the page.
  //
  // Looking at a page the agent has just acted on has to wait for it to react;
  // looking at one nobody touched does not. Without this every observation
  // would pay the settle floor, including the consecutive looks a model takes
  // while thinking.
  base::TimeTicks last_action_at_;

  // Settling state: the last look's signature and how many looks it has taken.
  // Reset at the start of every Observe, so a previous settle cannot leak into
  // the next one.
  // Built on first use and kept, because it is null in the ordinary case and
  // building it per step would mean re-reading the command line every time.
  std::unique_ptr<LocalVisionClient> vision_;
  // Send each step's screenshot, with every offered element outlined and
  // labelled by its id, to the model as an image. Set for cloud models.
  bool model_screenshots_ = false;
  bool vision_checked_ = false;

  std::unique_ptr<FetchWatcher> fetch_watcher_;
  // The last Observation handed out, kept only so the next one can say what
  // changed. Structured rather than re-parsed from JSON, because this is the
  // one place that has the real thing.
  Observation previous_;
  bool has_previous_ = false;

  std::string settle_signature_;
  std::string find_query_;
  size_t max_elements_ = kMaxObservedElements;
  size_t max_text_length_ = kMaxObservedTextLength;
  int settle_rounds_ = 0;
  base::TimeTicks settle_started_;

  // Consecutive looks that agreed with each other.
  //
  // Counted rather than tested, because a page under construction is stable
  // between batches and a single agreement cannot tell that apart from a page
  // that has finished.
  int stable_rounds_ = 0;

  // The address at the previous look, and when it last differed.
  //
  // A page whose address just changed is a page still being built: on a
  // single-page app the URL moves first and the content follows. See
  // kGraceAfterArriving.
  std::string url_when_last_looked_;
  base::TimeTicks arrived_at_;

  // Address, title and element count: what "the page has finished arriving"
  // is judged on, deliberately blind to text that merely ticks.
  std::string arrival_signature_;

  // The last address the browser actually LOADED, as opposed to the one the
  // page is currently showing. Written by FetchWatcher, which is why that class
  // holds a pointer back here. See Observation::document_url.
  std::string last_document_url_;

  // The address the model was last shown, and whether the last thing the agent
  // did was the kind that can make a page navigate. Together they answer "is
  // this page still on its way somewhere".
  std::string answered_url_;
  bool last_action_could_navigate_ = false;

  // Whether the look now in flight is the one the model will see.
  //
  // A member rather than a parameter threaded through the five asynchronous
  // hops between starting a look and finishing one. Safe because looks are
  // strictly serial: the loop runs one tool call at a time and each completes
  // before the next begins.
  bool remember_this_look_ = true;

  // The signature of the last Observation actually handed to the agent, so the
  // settle loop can recognise a page that has not moved since the model acted.
  std::string answered_signature_;
  bool has_answered_ = false;

  raw_ptr<Browser> browser_;

  // The look in flight is a quick one. See ToolSurface::ObserveQuick.
  bool quick_look_ = false;
  bool human_motion_ = false;
  raw_ptr<PointerObserver> pointer_observer_ = nullptr;
  std::deque<InputStep> input_queue_;
  bool input_running_ = false;
  std::vector<base::OnceClosure> idle_waiters_;
  gfx::PointF pointer_position_;
  bool has_pointer_position_ = false;
  uint32_t path_seed_ = 0x5EED;
  // The page a click in flight was aimed at. See ClickStep.
  base::WeakPtr<content::Page> click_page_;
  base::RepeatingTimer timeline_timer_;
  base::TimeTicks timeline_start_;
  base::TimeDelta timeline_duration_;
  base::RepeatingCallback<void(base::TimeDelta)> timeline_tick_;
  base::OnceClosure timeline_finished_;

  // The tab `accessibility_` was turned on for, so a tab switch re-scopes it.
  // The tree the last Observation's ids came from.
  ui::AXTreeID node_tree_id_;

  raw_ptr<content::WebContents> accessible_contents_ = nullptr;
  std::unique_ptr<content::ScopedAccessibilityMode> accessibility_;

  // Non-null only while an Observation is waiting for a page to settle.
  std::unique_ptr<LoadWaiter> load_waiter_;
  base::WeakPtrFactory<BrowserToolSurface> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_TOOL_SURFACE_H_
