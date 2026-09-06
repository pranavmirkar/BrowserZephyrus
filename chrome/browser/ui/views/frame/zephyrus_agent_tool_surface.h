// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_TOOL_SURFACE_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_TOOL_SURFACE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
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
  ui::AXTreeID CurrentTreeId() override;
  bool ClickNode(const ObservedNode& node) override;
  bool TypeIntoNode(const ObservedNode& node,
                    const std::string& text) override;
  bool SetNodeValue(const ObservedNode& node,
                    const std::string& value) override;
  bool ScrollPage(bool down, const std::string& amount) override;
  bool PressKey(const std::string& key) override;
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

  // One keystroke, press and release, with whatever modifiers are given.
  bool SendKey(content::RenderWidgetHost* widget,
               ui::KeyboardCode key_code,
               ui::DomCode dom_code,
               ui::DomKey dom_key,
               int flags);

  // One printable character, as the three events a real key produces: press,
  // the character itself, release. The character event is what inserts; the
  // other two are what the page's handlers watch for.
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
  bool vision_checked_ = false;

  std::unique_ptr<FetchWatcher> fetch_watcher_;
  std::string settle_signature_;
  int settle_rounds_ = 0;

  raw_ptr<Browser> browser_;

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
