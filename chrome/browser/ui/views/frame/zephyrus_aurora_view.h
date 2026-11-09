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
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/zephyrus/aurora/zephyrus_aurora_palette.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/render_widget_host_view.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

namespace content {
class WebContents;
}  // namespace content

class BrowserView;

namespace zephyrus {

// The Appearance toggle. Registered in browser_prefs.cc, which cannot include this
// header (layering), so the literal is repeated there.
inline constexpr char kAuroraPref[] = "zephyrus.appearance.aurora";

// Northern lights around the content card: a soft halo of light that starts at
// every edge of the page card and fades outward, in the colours of the page you
// are looking at. Top, bottom, left and right, round the corners, as one
// continuous glow.
//
// It is the WINDOW GROUND, not something laid over the window. It is the first
// view in the BrowserView after the plain background, and it paints (a) the
// ground colour of every surface the glow has to show through -- the window
// frame, the title bar and toolbar, and the docked sidebar column -- and then
// (b) the halo on top of those. Those surfaces stand aside from their own fill
// while it is showing (BrowserView::IsZephyrusAuroraBackgroundFor), so the light
// sits behind every control, tab and label and never in front of one. The page
// card is drawn above it all and hides the inner half of the glow, which is
// what makes the light look as if it comes from the card.
//
// It is a VIEW inside the window, so it needs nothing from the OS. (The browser
// window itself cannot be translucent, which is why there is no blur of the
// desktop to put colour in; see zephyrus-acrylic-translucent-window.)
//
// Where the colours come from, in order: the vivid colours of a small picture
// of the page (CopyFromSurface, 128x80, read once the page has settled and kept
// per tab); else the page's own theme-color; else the theme's primary. The
// picture is read here, in the browser process, and nothing is stored or sent.
// In a split view each page is a card and glows in its own colours.
//
// What it costs: at rest, nothing. A colour change is a short crossfade and that
// is the only thing that ever animates: every frame of an animation is a
// whole-window composite, so an effect that never stopped moving would be a
// permanent tax on an otherwise idle browser.
class ZephyrusAuroraView : public views::View {
  METADATA_HEADER(ZephyrusAuroraView, views::View)

 public:
  // A surface the glow shows through: painted in `color` before the glow.
  struct Surface {
    gfx::Rect rect;
    SkColor color;
    bool operator==(const Surface&) const = default;
  };

  // A page card the glow comes from, in this view's coordinates.
  struct Card {
    gfx::Rect rect;
    float radius = 0;
    bool operator==(const Card&) const = default;
  };

  explicit ZephyrusAuroraView(BrowserView* browser_view);
  ZephyrusAuroraView(const ZephyrusAuroraView&) = delete;
  ZephyrusAuroraView& operator=(const ZephyrusAuroraView&) = delete;
  ~ZephyrusAuroraView() override;

  // The Appearance toggle. Off, the view is hidden and takes no captures.
  void SetAuroraEnabled(bool enabled);
  bool aurora_enabled() const { return enabled_; }

  // Whether the aurora is painting the ground right now, so the surfaces above
  // it must not paint their own fill.
  bool IsShowing() const { return enabled_ && GetVisible(); }

  // Where everything is, set by BrowserView after each layout. `ground` fills
  // the whole view; `surfaces` are painted over it in order.
  void SetGeometry(SkColor ground,
                   std::vector<Surface> surfaces,
                   std::vector<Card> cards);

  // The pages on show changed: one normally, two in a split view, in reading
  // order. Wear their colours now (each one's last picture, or its theme-color
  // until a new one is taken) and look at them once they have painted.
  void OnPanesChanged(const std::vector<content::WebContents*>& panes);

  // A page finished loading (or changed what it shows): take a fresh look at
  // what is on show once things have settled.
  void OnPageSettled(content::WebContents* contents);

 private:
  class Morph;
  using Palette = std::array<SkColor, aurora::kSlots>;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;

  bool IsDarkChrome() const;
  SkColor FallbackColor(content::WebContents* contents) const;

  // One palette per page on show.
  std::vector<Palette> Compose() const;

  // Shows `palettes`, crossfading from what is showing when `animate`.
  void ShowColors(const std::vector<Palette>& palettes, bool animate);
  void OnMorphFrame();

  void ScheduleCapture(base::TimeDelta delay);
  void CapturePages();
  void OnCaptured(base::WeakPtr<content::WebContents> contents,
                  const content::CopyFromSurfaceResult& result);

  const raw_ptr<BrowserView> browser_view_;
  bool enabled_ = true;

  SkColor ground_ = SK_ColorTRANSPARENT;
  std::vector<Surface> surfaces_;
  std::vector<Card> cards_;

  // What is showing, what it is leaving, and what it is heading for. Same
  // length, one palette per page; a change in length snaps instead of fading.
  std::vector<Palette> current_;
  std::vector<Palette> from_;
  std::vector<Palette> target_;
  std::unique_ptr<Morph> morph_;

  // The pages whose colours are showing, in reading order.
  std::vector<base::WeakPtr<content::WebContents>> panes_;

  base::OneShotTimer capture_timer_;
  base::TimeTicks last_capture_;

  base::WeakPtrFactory<ZephyrusAuroraView> weak_ptr_factory_{this};
};

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AURORA_VIEW_H_
