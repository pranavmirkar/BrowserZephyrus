// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_agent_tool_surface.h"

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/browser_accessibility_state.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/scoped_accessibility_mode.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_tree_update.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
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
std::optional<NamedKey> KeyByName(const std::string& name) {
  if (name == "Enter") {
    return NamedKey{ui::VKEY_RETURN, ui::DomCode::ENTER, ui::DomKey::ENTER};
  }
  if (name == "Escape") {
    return NamedKey{ui::VKEY_ESCAPE, ui::DomCode::ESCAPE, ui::DomKey::ESCAPE};
  }
  if (name == "Tab") {
    return NamedKey{ui::VKEY_TAB, ui::DomCode::TAB, ui::DomKey::TAB};
  }
  if (name == "ArrowUp") {
    return NamedKey{ui::VKEY_UP, ui::DomCode::ARROW_UP, ui::DomKey::ARROW_UP};
  }
  if (name == "ArrowDown") {
    return NamedKey{ui::VKEY_DOWN, ui::DomCode::ARROW_DOWN,
                    ui::DomKey::ARROW_DOWN};
  }
  if (name == "Backspace") {
    return NamedKey{ui::VKEY_BACK, ui::DomCode::BACKSPACE,
                    ui::DomKey::BACKSPACE};
  }
  return std::nullopt;
}

}  // namespace

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
  content::WebContents* contents = ActiveContents();
  if (!contents || !contents->GetController().CanGoBack()) {
    return false;
  }
  contents->GetController().GoBack();
  return true;
}

bool BrowserToolSurface::GoForward() {
  content::WebContents* contents = ActiveContents();
  if (!contents || !contents->GetController().CanGoForward()) {
    return false;
  }
  contents->GetController().GoForward();
  return true;
}

bool BrowserToolSurface::Reload() {
  content::WebContents* contents = ActiveContents();
  if (!contents) {
    return false;
  }
  contents->GetController().Reload(content::ReloadType::NORMAL,
                                   /*check_for_repost=*/true);
  return true;
}

bool BrowserToolSurface::OpenTab(const GURL& url) {
  // An empty URL means a blank new tab, which is what the contract's optional
  // url argument asks for.
  NavigateParams params(browser_, url.is_empty() ? GURL("about:blank") : url,
                        ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  ::Navigate(&params);
  return true;
}

bool BrowserToolSurface::SwitchToTab(int tab_id) {
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

void BrowserToolSurface::Observe(ObserveCallback callback) {
  EnsureAccessibility();
  content::WebContents* contents = ActiveContents();
  if (!contents) {
    // No page is an empty Observation, not an error. It offers nothing, so
    // every element id the model could name is ungrounded and gets refused.
    std::move(callback).Run(Observation());
    return;
  }

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
  std::move(callback).Run(BuildObservation(update, url, title,
                                           kMaxObservedElements,
                                           kMaxObservedTextLength));
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
  // An action sent to a tab with no live accessibility tree is dropped by the
  // renderer without a word, so this is what makes the difference between
  // clicking something and only appearing to.
  EnsureAccessibility();
  content::WebContents* contents = ActiveContents();
  if (!contents || !contents->GetPrimaryMainFrame()) {
    return false;
  }
  ui::AXActionData data;
  data.action = action;
  data.target_node_id = node;
  data.target_tree_id = contents->GetPrimaryMainFrame()->GetAXTreeID();
  data.value = value;
  contents->GetPrimaryMainFrame()->AccessibilityPerformAction(data);
  return true;
}

bool BrowserToolSurface::ClickNode(ui::AXNodeID node) {
  // The default action, which is what assistive technology sends and what the
  // page's own handlers are written to expect. Synthesising a mouse click at
  // the node's screen position would also fire on whatever is drawn on top.
  return PerformAction(ax::mojom::Action::kDoDefault, node, std::string());
}

bool BrowserToolSurface::SetNodeValue(ui::AXNodeID node,
                                      const std::string& value) {
  return PerformAction(ax::mojom::Action::kSetValue, node, value);
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
  content::WebContents* contents = ActiveContents();
  if (!contents) {
    return false;
  }

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
