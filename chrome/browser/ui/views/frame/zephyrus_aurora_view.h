// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AURORA_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AURORA_VIEW_H_

#include <array>
#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/zephyrus/aurora/zephyrus_aurora_palette.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/render_widget_host_view.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

namespace content {
class WebContents;
}  // namespace content

class BrowserView;

namespace zephyrus {

// The Appearance toggle. Registered in browser_prefs.cc, which cannot include this
// header (layering), so the literal is repeated there.
inline constexpr char kAuroraPref[] = "zephyrus.appearance.aurora";

// Northern lights rising from the content card: a few soft curtains of light
// that are brightest where the chrome meets the top edge of the page card and
// fade away upward, in the colours of the page you are looking at.
//
// It is the BACKGROUND of the title bar and toolbar, never a layer over them:
// it is the first child of the top container and paints the bar's own fill and
// the light together, and the toolbar's fill steps aside while it is showing
// (CustomCornersBackground::Paint asks BrowserView::IsZephyrusAuroraBackgroundFor()).
// Every control, tab and label is drawn on top of it, untouched. It is a VIEW
// inside the window, so it needs nothing from the OS. (The browser window itself
// cannot be translucent, which is why there is no blur of the desktop to put
// colour in; see zephyrus-acrylic-translucent-window.)
//
// In a split view both pages lend colour: the left of the aurora wears the
// left page and the right wears the right.
//
// Where the colours come from, in order: the vivid colours of a small picture
// of the page (CopyFromSurface, 128x80, read once the page has settled and kept
// per tab); else the page's own theme-color; else the theme's primary. The
// picture is read here, in the browser process, and nothing is stored or sent.
//
// What it costs: at rest, nothing. A colour change is a short crossfade and
// that is the only thing that ever animates: every frame of an animation is a
// whole-window composite, so an effect that never stopped moving would be a
// permanent tax on an otherwise idle browser. (It is painted, not layered, so
// there is no compositor drift either; a layer would sit over the controls.)
class ZephyrusAuroraView : public views::View, public views::ViewObserver {
  METADATA_HEADER(ZephyrusAuroraView, views::View)

 public:
  explicit ZephyrusAuroraView(BrowserView* browser_view);
  ZephyrusAuroraView(const ZephyrusAuroraView&) = delete;
  ZephyrusAuroraView& operator=(const ZephyrusAuroraView&) = delete;
  ~ZephyrusAuroraView() override;

  // The Appearance toggle. Off, the view is hidden and takes no captures.
  void SetAuroraEnabled(bool enabled);
  bool aurora_enabled() const { return enabled_; }

  // Whether the aurora is painting the bar's background right now, so the
  // toolbar must not paint its own fill over it.
  bool IsShowing() const { return enabled_ && GetVisible(); }

  // The pages on show changed: one normally, two in a split view, in reading
  // order. Wear their colours now (each one's last picture, or its theme-color
  // until a new one is taken) and look at them once they have painted. In a
  // split the left of the aurora wears the left page and the right wears the
  // right, meeting in the middle.
  void OnPanesChanged(const std::vector<content::WebContents*>& panes);

  // A page finished loading (or changed what it shows): take a fresh look at
  // what is on show once things have settled.
  void OnPageSettled(content::WebContents* contents);

 private:
  class Morph;

  // views::View:
  void ViewHierarchyChanged(const views::ViewHierarchyChangedDetails& details) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewIsDeleting(views::View* observed_view) override;

  void FitToParent();
  bool IsDarkChrome() const;
  SkColor FallbackColor(content::WebContents* contents) const;

  // Shows `colors`, crossfading from what is showing when `animate`.
  void ShowColors(const std::array<SkColor, aurora::kSlots>& colors,
                  bool animate);
  void OnMorphFrame();

  // The colours for what is on show: one page's three, or two pages' blended.
  std::array<SkColor, aurora::kSlots> Compose() const;

  void ScheduleCapture(base::TimeDelta delay);
  void CapturePages();
  void OnCaptured(base::WeakPtr<content::WebContents> contents,
                  const content::CopyFromSurfaceResult& result);

  const raw_ptr<BrowserView> browser_view_;
  bool enabled_ = true;

  // What is showing, what it is leaving, and what it is heading for.
  std::array<SkColor, aurora::kSlots> current_{};
  std::array<SkColor, aurora::kSlots> from_{};
  std::array<SkColor, aurora::kSlots> target_{};
  bool has_colors_ = false;
  std::unique_ptr<Morph> morph_;

  // The pages whose colours are showing, in reading order.
  std::vector<base::WeakPtr<content::WebContents>> panes_;

  base::OneShotTimer capture_timer_;
  base::TimeTicks last_capture_;

  base::ScopedObservation<views::View, views::ViewObserver> parent_observation_{
      this};

  base::WeakPtrFactory<ZephyrusAuroraView> weak_ptr_factory_{this};
};

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AURORA_VIEW_H_
