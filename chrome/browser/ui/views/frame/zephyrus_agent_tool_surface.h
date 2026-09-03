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
#include "chrome/browser/zephyrus/agent/tool_surface.h"
#include "ui/accessibility/ax_enums.mojom-forward.h"
#include "ui/accessibility/ax_tree_update_forward.h"

class Browser;

namespace content {
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
  bool ClickNode(ui::AXNodeID node) override;
  bool SetNodeValue(ui::AXNodeID node, const std::string& value) override;
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

  void OnSnapshot(ObserveCallback callback,
                  std::string url,
                  std::string title,
                  ui::AXTreeUpdate& update);

  raw_ptr<Browser> browser_;

  // The tab `accessibility_` was turned on for, so a tab switch re-scopes it.
  raw_ptr<content::WebContents> accessible_contents_ = nullptr;
  std::unique_ptr<content::ScopedAccessibilityMode> accessibility_;
  base::WeakPtrFactory<BrowserToolSurface> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_TOOL_SURFACE_H_
