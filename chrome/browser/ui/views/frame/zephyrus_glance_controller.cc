// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_glance_controller.h"

#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_glance_view.h"
#include "chrome/browser/zephyrus/buildflags/dev_switches.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/gfx/geometry/point_conversions.h"

namespace {

namespace glance = zephyrus::glance;

glance::Button ButtonFrom(blink::WebPointerProperties::Button button) {
  switch (button) {
    case blink::WebPointerProperties::Button::kLeft:
      return glance::Button::kLeft;
    case blink::WebPointerProperties::Button::kMiddle:
      return glance::Button::kMiddle;
    case blink::WebPointerProperties::Button::kRight:
      return glance::Button::kRight;
    default:
      return glance::Button::kOther;
  }
}

}  // namespace

ZephyrusGlanceController::ZephyrusGlanceController(BrowserView* browser_view,
                                                   ZephyrusGlanceView* view)
    : browser_view_(browser_view), view_(view) {
  mouse_callback_ = base::BindRepeating(&ZephyrusGlanceController::OnMouseEvent,
                                        base::Unretained(this));
  TabStripModel* model = browser_view_->browser()->tab_strip_model();
  model->AddObserver(this);
  Rebind();

  // Dev only: --zephyrus-test-glance=<url> opens a glance on the active tab a
  // few seconds after the window is up, so the card can be looked at without a
  // real Alt+click.
  if (zephyrus::DevSwitchesEnabled()) {
    const std::string url =
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
            "zephyrus-test-glance");
    if (!url.empty()) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(
              [](base::WeakPtr<ZephyrusGlanceController> self, GURL url) {
                if (self) {
                  self->OpenGlance(url, gfx::Point(300, 200));
                }
              },
              weak_factory_.GetWeakPtr(), GURL(url)),
          base::Seconds(6));
    }
  }
}

ZephyrusGlanceController::~ZephyrusGlanceController() {
  // The view may already be gone (it is a child of the browser view and goes
  // with it), so nothing here touches it.
  UnregisterAll();
}

glance::Config ZephyrusGlanceController::ReadConfig() const {
  glance::Config config;
  const PrefService* prefs = browser_view_->browser()->profile()->GetPrefs();
  config.enabled = prefs->GetBoolean(glance::kEnabledPref);
  config.trigger =
      glance::TriggerFromName(prefs->GetString(glance::kTriggerPref));
  return config;
}

void ZephyrusGlanceController::OnTargetUrl(content::WebContents* source,
                                           const GURL& url) {
  if (source ==
      browser_view_->browser()->tab_strip_model()->GetActiveWebContents()) {
    hovered_link_ = url;
  }
}

void ZephyrusGlanceController::OpenGlance(const GURL& url,
                                          const gfx::Point& origin) {
  content::WebContents* active =
      browser_view_->browser()->tab_strip_model()->GetActiveWebContents();
  if (active && view_) {
    view_->Open(url, active, origin);
  }
}

bool ZephyrusGlanceController::OnMouseEvent(const blink::WebMouseEvent& event) {
  using Type = blink::WebInputEvent::Type;
  glance::MouseType type = glance::MouseType::kOther;
  if (event.GetType() == Type::kMouseDown) {
    type = glance::MouseType::kDown;
  } else if (event.GetType() == Type::kMouseUp) {
    type = glance::MouseType::kUp;
  }
  // While a card is up, the page behind it is not to be touched. The card's
  // view takes clicks on the dimmed area itself, but the page is a native
  // window, and an event can reach it without passing through the view. So the
  // page's own widget is also told to ignore the mouse, and a press on it is
  // the same "click outside" the view would have heard.
  if (view_ && view_->showing()) {
    if (type == glance::MouseType::kDown) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&ZephyrusGlanceView::Close,
                                    view_->GetWeakPtr()));
    }
    return true;
  }
  // Moves, wheel and the rest are never ours, and are the bulk of the traffic.
  if (type == glance::MouseType::kOther) {
    return false;
  }

  const int flags = event.GetModifiers();
  glance::Modifiers modifiers;
  modifiers.alt = flags & blink::WebInputEvent::kAltKey;
  modifiers.ctrl = flags & blink::WebInputEvent::kControlKey;
  modifiers.shift = flags & blink::WebInputEvent::kShiftKey;
  modifiers.meta = flags & blink::WebInputEvent::kMetaKey;
  const glance::Button button = ButtonFrom(event.button);

  if (type == glance::MouseType::kDown && button == glance::Button::kLeft) {
    click_point_ = gfx::ToRoundedPoint(event.PositionInWidget());
  }

  GURL url;
  const glance::ClickGate::Action action =
      gate_.OnMouse(ReadConfig(), type, button, modifiers, hovered_link_, &url);
  if (action == glance::ClickGate::Action::kOpen) {
    // Not from inside the input handler: opening builds a WebContents and a
    // view, and the event is still being dispatched.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&ZephyrusGlanceController::OpenGlance,
                       weak_factory_.GetWeakPtr(), std::move(url),
                       click_point_));
  }
  return action != glance::ClickGate::Action::kPass;
}

void ZephyrusGlanceController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed() ||
      change.type() == TabStripModelChange::kReplaced) {
    // The card belongs to the page it was opened over. Switching tabs (or
    // workspaces) dismisses it, as in Zen.
    if (view_) {
      view_->CloseImmediately();
    }
    gate_.Reset();
    hovered_link_ = GURL();
    Rebind();
  }
}

void ZephyrusGlanceController::Rebind() {
  UnregisterAll();
  content::WebContents* active =
      browser_view_->browser()->tab_strip_model()->GetActiveWebContents();
  Observe(active);
  if (active) {
    RegisterHosts(active);
  }
}

void ZephyrusGlanceController::RegisterHosts(content::WebContents* contents) {
  contents->ForEachRenderFrameHost([this](content::RenderFrameHost* frame) {
    content::RenderWidgetHost* host = frame->GetRenderWidgetHost();
    // A frame in the same process as its parent shares the parent's widget, so
    // one host is reached from many frames, and registering twice is a CHECK.
    if (!host || hosts_.contains(host)) {
      return;
    }
    host->AddMouseEventCallback(mouse_callback_);
    host_observations_.AddObservation(host);
    hosts_.insert(host);
  });
}

void ZephyrusGlanceController::UnregisterAll() {
  for (content::RenderWidgetHost* host : hosts_) {
    host->RemoveMouseEventCallback(mouse_callback_);
  }
  hosts_.clear();
  host_observations_.RemoveAllObservations();
}

void ZephyrusGlanceController::RenderFrameCreated(
    content::RenderFrameHost* render_frame_host) {
  RegisterHosts(web_contents());
}

void ZephyrusGlanceController::RenderFrameHostChanged(
    content::RenderFrameHost* old_host,
    content::RenderFrameHost* new_host) {
  RegisterHosts(web_contents());
}

void ZephyrusGlanceController::PrimaryPageChanged(content::Page& page) {
  RegisterHosts(web_contents());
}

void ZephyrusGlanceController::WebContentsDestroyed() {
  UnregisterAll();
}

void ZephyrusGlanceController::RenderWidgetHostDestroyed(
    content::RenderWidgetHost* host) {
  hosts_.erase(host);
  host_observations_.RemoveObservation(host);
}
