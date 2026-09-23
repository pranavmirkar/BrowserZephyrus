// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_TOOL_SURFACE_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_TOOL_SURFACE_H_

#include <string>
#include <utility>
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

  virtual ~ToolSurface();

  // The URL of the active tab. Authoritative: the executor sends this to the
  // kernel rather than accepting one from its caller, because the kernel uses
  // the current origin to judge where a navigation is going. A caller that
  // could name the page could talk the kernel into allowing an exfiltration.
  virtual std::string GetActiveUrl() = 0;

  // Tabs in the CURRENT WORKSPACE only. Tabs in other workspaces are not the
  // agent's to see; a task is bound to one workspace (ADR 0003).
  virtual std::vector<TabInfo> ListTabs() = 0;

  // These report that a load was STARTED, not that it finished, and the layer
  // above is what makes that safe.
  //
  // The note here used to say the next Observation might therefore be of the
  // old document, and to revisit once real models drove the loop. Real models
  // now do, and the answer turned out to be that the waiting belongs in the
  // LOOKING rather than in the moving: Observe() waits out a load in flight,
  // then waits for the page to stop changing, then gives up after a timeout --
  // because a page that never finishes loading must end the wait rather than
  // the task. An implementation marks these as actions that could navigate, so
  // the look that follows pays that wait.
  //
  // What the moving still owes is a RESULT that is true when it is said, and
  // that is the executor's job: browser.navigate checks where it arrived, and
  // the three below check that they moved at all. A history move that quietly
  // went nowhere used to be recorded as a success the model then reasoned
  // from.
  virtual bool Navigate(const GURL& url) = 0;
  virtual bool GoBack() = 0;
  virtual bool GoForward() = 0;
  virtual bool Reload() = 0;

  virtual bool OpenTab(const GURL& url) = 0;
  virtual bool SwitchToTab(int tab_id) = 0;
  virtual bool CloseTab(int tab_id) = 0;

  // Takes a fresh look at the active page. Asynchronous because it goes to the
  // renderer.
  // Looks at the page for the MODEL.
  //
  // What comes back is what the model will be shown, so it becomes the baseline
  // that the next look reports its changes against.
  virtual void Observe(ObserveCallback callback) = 0;

  // Search beyond the normal prompt's element budget. Implementations keep
  // these results available in the next observation so their ids remain usable.
  virtual void ObserveForFind(const std::string& query,
                              ObserveCallback callback);

  // The same look, for an internal check the model never sees.
  //
  // It must not become that baseline, and the distinction is not academic. A
  // check runs AFTER an action -- did the navigation arrive, did the text land
  // -- so treating it as "what the model last saw" means the model's next look
  // is compared against a picture taken after its own action. A step that
  // plainly changed the page then reports "nothing on the page changed", which
  // is the precise opposite of the truth and the signal the model uses to
  // decide whether it is finished.
  virtual void ObserveForCheck(ObserveCallback callback) = 0;

  // Identifies the tree the active page is currently showing. Compared against
  // the Observation's before acting on any element: if it has changed, every id
  // the model holds refers to something that no longer exists.
  virtual ui::AXTreeID CurrentTreeId() = 0;

  // Element actions. `node` is one the executor resolved from an issued element
  // id against its own Observation -- the model never supplies one, and never
  // supplies the position inside it, which is the point the pointer goes to.
  //
  // These take the whole ObservedNode rather than a bare id because the two
  // ways to reach an element -- the accessibility action and the pointer --
  // need different parts of it, and which one is used is this layer's decision
  // to make. A caller that had to pick would eventually pick wrong.
  virtual bool ClickNode(const ObservedNode& node) = 0;

  // Types text into a field, as a person would: put the pointer on it, then
  // send real keystrokes.
  //
  // Separate from SetNodeValue because they are different mechanisms for
  // different things, and the split is the lesson from three failures. A page
  // whose search box is a framework component decides whether it has content
  // by watching its own key events -- assigning a value leaves it convinced it
  // is still empty, so it never submits. Keystrokes go through the input
  // pipeline, which a page cannot tell apart from a person typing.
  virtual bool TypeIntoNode(const ObservedNode& node,
                            const std::string& text) = 0;

  // Chooses a value on a control that offers a fixed set of them.
  //
  // This one stays an accessibility action, because that is the right tool for
  // it: there is no way to "type" into a native <select>, and picking an option
  // is exactly what the accessibility API is for.
  virtual bool SetNodeValue(const ObservedNode& node,
                            const std::string& value) = 0;

  virtual bool ScrollPage(bool down, const std::string& amount) = 0;
  virtual bool PressKey(const std::string& key) = 0;
  virtual std::string ReadSelection() = 0;
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_TOOL_SURFACE_H_
