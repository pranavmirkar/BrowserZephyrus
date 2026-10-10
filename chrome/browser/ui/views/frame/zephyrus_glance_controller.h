// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_GLANCE_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_GLANCE_CONTROLLER_H_

#include "base/containers/flat_set.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/zephyrus/glance/glance_policy.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/gfx/geometry/point.h"
#include "url/gurl.h"

class BrowserView;
class ZephyrusGlanceView;

namespace blink {
class WebMouseEvent;
}

// Decides when a click opens a glance, and opens it. The card itself is
// ZephyrusGlanceView; the rules are in zephyrus/glance/glance_policy.
//
// HOW A CLICK IS CAUGHT. Chromium does not tell the browser which link was
// clicked, but it tells it two things that add up to that: the link under the
// pointer (Browser::UpdateTargetURL, which feeds the status bubble) and every
// mouse event before the page sees it (RenderWidgetHost::AddMouseEventCallback,
// which can swallow it). So the hovered link is remembered, and an Alt-press on
// it is swallowed; the release then opens the card. Nothing in Blink or content
// changed for this, and a click that is not ours is not touched.
class ZephyrusGlanceController : public TabStripModelObserver,
                                 public content::WebContentsObserver,
                                 public content::RenderWidgetHostObserver {
 public:
  ZephyrusGlanceController(BrowserView* browser_view, ZephyrusGlanceView* view);
  ZephyrusGlanceController(const ZephyrusGlanceController&) = delete;
  ZephyrusGlanceController& operator=(const ZephyrusGlanceController&) = delete;
  ~ZephyrusGlanceController() override;

  // From Browser::UpdateTargetURL: the link under the pointer in `source`, or
  // empty when the pointer left it.
  void OnTargetUrl(content::WebContents* source, const GURL& url);

  // Opens `url` over the active tab now, growing from `origin` in the page
  // area's coordinates. For anything that is not an Alt+click (a context menu
  // item, the developer switch).
  void OpenGlance(const GURL& url, const gfx::Point& origin);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // content::WebContentsObserver:
  void RenderFrameCreated(content::RenderFrameHost* render_frame_host) override;
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override;
  void PrimaryPageChanged(content::Page& page) override;
  void WebContentsDestroyed() override;

  // content::RenderWidgetHostObserver:
  void RenderWidgetHostDestroyed(content::RenderWidgetHost* host) override;

 private:
  // Returns true when the event is ours and the page must not see it.
  bool OnMouseEvent(const blink::WebMouseEvent& event);

  zephyrus::glance::Config ReadConfig() const;
  // Moves the interception to the active tab's widgets.
  void Rebind();
  void RegisterHosts(content::WebContents* contents);
  void UnregisterAll();

  const raw_ptr<BrowserView> browser_view_;
  // Owned by the browser view as a child, and so destroyed after this.
  const raw_ptr<ZephyrusGlanceView, DanglingUntriaged> view_;

  zephyrus::glance::ClickGate gate_;
  GURL hovered_link_;
  gfx::Point click_point_;

  content::RenderWidgetHost::MouseEventCallback mouse_callback_;
  base::flat_set<raw_ptr<content::RenderWidgetHost>> hosts_;
  base::ScopedMultiSourceObservation<content::RenderWidgetHost,
                                     content::RenderWidgetHostObserver>
      host_observations_{this};

  base::WeakPtrFactory<ZephyrusGlanceController> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_GLANCE_CONTROLLER_H_
