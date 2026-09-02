// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_TOOL_SURFACE_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_TOOL_SURFACE_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "chrome/browser/zephyrus/agent/observation.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree_id.h"

class GURL;

namespace zephyrus::agent {

// Everything the agent is allowed to do to the browser, and nothing else.
//
// This exists for two reasons. It makes the executor testable without a
// browser, and more importantly it is the complete list of the agent's reach:
// if a capability is not a method here, no tool can perform it. Adding one is a
// deliberate act, which is the same property the mojom interface gives the
// process boundary and the role table gives the Observation.
//
// Every method reports whether it worked. The world can move between the kernel
// deciding and the executor acting -- a tab closes, a navigation commits -- and
// a stale request must fail rather than act on whatever took its place.
class ToolSurface {
 public:
  struct TabInfo {
    int id = 0;
    std::string title;
    std::string url;
    bool active = false;
  };

  using ObserveCallback = base::OnceCallback<void(Observation)>;

  virtual ~ToolSurface() = default;

  // The URL of the active tab. Authoritative: the executor sends this to the
  // kernel rather than accepting one from its caller, because the kernel uses
  // the current origin to judge where a navigation is going. A caller that
  // could name the page could talk the kernel into allowing an exfiltration.
  virtual std::string GetActiveUrl() = 0;

  // Tabs in the CURRENT WORKSPACE only. Tabs in other workspaces are not the
  // agent's to see; a task is bound to one workspace (ADR 0003).
  virtual std::vector<TabInfo> ListTabs() = 0;

  // These report that a load was STARTED, not that it finished. A task can
  // therefore reach its next step while the page is still on its way, and the
  // Observation it takes may still be of the old document.
  //
  // Left this way on purpose for now: waiting for a commit means deciding what
  // to do about a load that never finishes, and a hung tool is worse than a
  // stale look. Revisit when the loop has real models driving it, since the
  // right answer depends on how often it actually bites.
  virtual bool Navigate(const GURL& url) = 0;
  virtual bool GoBack() = 0;
  virtual bool GoForward() = 0;
  virtual bool Reload() = 0;

  virtual bool OpenTab(const GURL& url) = 0;
  virtual bool SwitchToTab(int tab_id) = 0;
  virtual bool CloseTab(int tab_id) = 0;

  // Takes a fresh look at the active page. Asynchronous because it goes to the
  // renderer.
  virtual void Observe(ObserveCallback callback) = 0;

  // Identifies the tree the active page is currently showing. Compared against
  // the Observation's before acting on any element: if it has changed, every id
  // the model holds refers to something that no longer exists.
  virtual ui::AXTreeID CurrentTreeId() = 0;

  // Element actions. `node` is a real accessibility node id that the executor
  // resolved from an issued element id -- the model never supplies one.
  virtual bool ClickNode(ui::AXNodeID node) = 0;
  virtual bool SetNodeValue(ui::AXNodeID node, const std::string& value) = 0;

  virtual bool ScrollPage(bool down, const std::string& amount) = 0;
  virtual bool PressKey(const std::string& key) = 0;
  virtual std::string ReadSelection() = 0;
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_TOOL_SURFACE_H_
