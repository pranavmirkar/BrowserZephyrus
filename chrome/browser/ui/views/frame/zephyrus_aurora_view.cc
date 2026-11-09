// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_aurora_view.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_filter.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/animation/animation_delegate_views.h"
#include "ui/views/widget/widget.h"

namespace zephyrus {

namespace {

using Palette = std::array<SkColor, aurora::kSlots>;

// How big the picture of the page is, and how long a page gets to settle before
// it is read. Pages keep painting for a moment after "loaded" (fonts, hero
// images), and a picture of the half-built page would be the wrong colour for
// the whole time the page is open.
constexpr int kCaptureWidth = 128;
constexpr int kCaptureHeight = 80;
constexpr int kReadWidth = 32;
constexpr int kReadHeight = 20;
constexpr base::TimeDelta kSettleDelay = base::Milliseconds(1100);
constexpr base::TimeDelta kTabSwitchDelay = base::Milliseconds(450);
// Never more often than this, however a page behaves. A page that reloads in a
// loop must not become a screen-capture loop.
constexpr base::TimeDelta kMinCaptureGap = base::Seconds(3);
// A tab's picture is trusted this long when switching back to it.
constexpr base::TimeDelta kPictureTrusted = base::Minutes(5);

constexpr base::TimeDelta kMorphDuration = base::Milliseconds(700);

// The glow is a rounded rectangle the size of the card, filled with the page's
// colours and blurred. The blur is what makes it light and not a border: the
// card hides the inner half, and the outer half falls away over about twice the
// sigma. The rectangle is pushed out a little so the glow is brightest right at
// the card's edge instead of half-strength there.
constexpr float kHaloSigma = 12.0f;
constexpr float kHaloOutset = 8.0f;
// Opacity of the fill before the blur takes half of it away at the edge. The
// chrome has text and icons over it, so this is a tint and never a fill: light
// chrome takes less than dark, because colour on a light surface reads as
// stronger than the same colour on a dark one.
constexpr float kHaloOpacityOnDark = 0.95f;
constexpr float kHaloOpacityOnLight = 0.70f;

// The last picture of a page, kept with the tab so switching back to it does
// not wait on a new one.
class AuroraTabData : public content::WebContentsUserData<AuroraTabData> {
 public:
  ~AuroraTabData() override = default;

  Palette colors{};
  base::TimeTicks taken;

 private:
  friend class content::WebContentsUserData<AuroraTabData>;
  explicit AuroraTabData(content::WebContents* contents)
      : content::WebContentsUserData<AuroraTabData>(*contents) {}
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};
WEB_CONTENTS_USER_DATA_KEY_IMPL(AuroraTabData);

}  // namespace

// The crossfade between two colour sets. A thin AnimationDelegateViews, the way
// the omnibox's focus animation is, so frames follow the compositor.
class ZephyrusAuroraView::Morph : public views::AnimationDelegateViews {
 public:
  Morph(views::View* owner, base::RepeatingClosure on_frame)
      : views::AnimationDelegateViews(owner), on_frame_(std::move(on_frame)) {}
  ~Morph() override = default;

  void Start() {
    animation_.SetDuration(kMorphDuration);
    animation_.Start();
  }
  void Stop() { animation_.Stop(); }
  bool running() const { return animation_.is_animating(); }
  // 0 at the start, 1 once settled, eased.
  double progress() const {
    return gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT,
                                      animation_.GetCurrentValue());
  }

 private:
  void AnimationProgressed(const gfx::Animation*) override { on_frame_.Run(); }
  void AnimationEnded(const gfx::Animation*) override { on_frame_.Run(); }

  base::RepeatingClosure on_frame_;
  gfx::LinearAnimation animation_{kMorphDuration,
                                  gfx::LinearAnimation::kDefaultFrameRate,
                                  this};
};

ZephyrusAuroraView::ZephyrusAuroraView(BrowserView* browser_view)
    : browser_view_(browser_view) {
  // Behind everything, and out of the pointer's way entirely.
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetIsIgnored(true);
  morph_ = std::make_unique<Morph>(
      this, base::BindRepeating(&ZephyrusAuroraView::OnMorphFrame,
                                base::Unretained(this)));
}

ZephyrusAuroraView::~ZephyrusAuroraView() {
  // The timer dies with this view; a pending capture's callback holds a weak
  // pointer, so a late result finds nobody.
  capture_timer_.Stop();
}

void ZephyrusAuroraView::SetAuroraEnabled(bool enabled) {
  if (enabled == enabled_) {
    return;
  }
  enabled_ = enabled;
  if (!enabled_) {
    capture_timer_.Stop();
    morph_->Stop();
    SetVisible(false);
    return;
  }
  SetVisible(true);
  std::vector<content::WebContents*> panes;
  for (const auto& pane : panes_) {
    if (pane) {
      panes.push_back(pane.get());
    }
  }
  OnPanesChanged(panes);
}

void ZephyrusAuroraView::SetGeometry(SkColor ground,
                                     std::vector<Surface> surfaces,
                                     std::vector<Card> cards) {
  if (ground == ground_ && surfaces == surfaces_ && cards == cards_) {
    return;
  }
  ground_ = ground;
  surfaces_ = std::move(surfaces);
  cards_ = std::move(cards);
  SchedulePaint();
}

void ZephyrusAuroraView::OnPanesChanged(
    const std::vector<content::WebContents*>& panes) {
  panes_.clear();
  for (content::WebContents* pane : panes) {
    if (pane) {
      panes_.push_back(pane->GetWeakPtr());
    }
  }
  if (!enabled_ || panes_.empty()) {
    return;
  }
  // Pages with a recent picture are shown as they were; the others stand in
  // with their own colour (or the theme's) until they have been looked at, so a
  // switch never leaves the last tab's colours on a new page.
  bool need_picture = false;
  for (const auto& pane : panes_) {
    AuroraTabData* data = AuroraTabData::GetOrCreateForWebContents(pane.get());
    const bool fresh = !data->taken.is_null() &&
                       base::TimeTicks::Now() - data->taken < kPictureTrusted;
    if (!fresh) {
      data->colors = aurora::FillSlots({}, FallbackColor(pane.get()));
      need_picture = true;
    }
  }
  ShowColors(Compose(), /*animate=*/true);
  if (need_picture) {
    ScheduleCapture(kTabSwitchDelay);
  }
}

void ZephyrusAuroraView::OnPageSettled(content::WebContents* contents) {
  if (!enabled_ || !contents) {
    return;
  }
  for (const auto& pane : panes_) {
    if (pane.get() == contents) {
      ScheduleCapture(kSettleDelay);
      return;
    }
  }
}

std::vector<ZephyrusAuroraView::Palette> ZephyrusAuroraView::Compose() const {
  std::vector<Palette> pages;
  for (const auto& pane : panes_) {
    if (!pane) {
      continue;
    }
    if (AuroraTabData* data = AuroraTabData::FromWebContents(pane.get())) {
      pages.push_back(data->colors);
    }
  }
  if (pages.empty()) {
    pages.push_back(aurora::FillSlots({}, FallbackColor(nullptr)));
  }
  return pages;
}

void ZephyrusAuroraView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SchedulePaint();
}

bool ZephyrusAuroraView::IsDarkChrome() const {
  const ui::ColorProvider* provider = GetColorProvider();
  return !provider || color_utils::IsDark(provider->GetColor(kColorToolbar));
}

SkColor ZephyrusAuroraView::FallbackColor(
    content::WebContents* contents) const {
  if (contents) {
    if (std::optional<SkColor> theme = contents->GetThemeColor()) {
      color_utils::HSL hsl;
      color_utils::SkColorToHSL(*theme, &hsl);
      // A white or grey theme-color is "no colour", not a colour.
      if (hsl.s > 0.25 && hsl.l > 0.12 && hsl.l < 0.9) {
        return *theme;
      }
    }
  }
  if (const ui::ColorProvider* provider = GetColorProvider()) {
    return provider->GetColor(ui::kColorSysPrimary);
  }
  return SkColorSetRGB(0x6e, 0x8c, 0xf0);
}

void ZephyrusAuroraView::ShowColors(const std::vector<Palette>& palettes,
                                    bool animate) {
  if (palettes.empty() || palettes == target_) {
    return;
  }
  if (current_.size() != palettes.size() || !animate ||
      !gfx::Animation::ShouldRenderRichAnimation() || !GetVisible()) {
    morph_->Stop();
    current_ = target_ = from_ = palettes;
    SchedulePaint();
    return;
  }
  // Leave from wherever the last fade had got to, so a quick second change does
  // not jump back to the old colours first.
  from_ = current_;
  target_ = palettes;
  morph_->Start();
}

void ZephyrusAuroraView::OnMorphFrame() {
  const double t = morph_->running() ? morph_->progress() : 1.0;
  for (size_t p = 0; p < current_.size() && p < target_.size(); ++p) {
    for (size_t i = 0; i < current_[p].size(); ++i) {
      current_[p][i] =
          gfx::Tween::ColorValueBetween(t, from_[p][i], target_[p][i]);
    }
  }
  SchedulePaint();
}

void ZephyrusAuroraView::OnPaint(gfx::Canvas* canvas) {
  const gfx::Rect bounds = GetLocalBounds();
  if (bounds.IsEmpty()) {
    return;
  }
  // The ground first: the colour of every surface the glow has to show through.
  // They stand aside from their own fill while this is showing, so this is what
  // the window looks like with no page colour in it at all.
  canvas->FillRect(bounds, ground_);
  for (const Surface& surface : surfaces_) {
    canvas->FillRect(surface.rect, surface.color);
  }
  if (current_.empty() || cards_.empty()) {
    return;
  }

  const float opacity =
      IsDarkChrome() ? kHaloOpacityOnDark : kHaloOpacityOnLight;
  gfx::ScopedCanvas scoped(canvas);
  canvas->ClipRect(bounds);
  for (size_t i = 0; i < cards_.size(); ++i) {
    const Card& card = cards_[i];
    if (card.rect.IsEmpty()) {
      continue;
    }
    const Palette& palette = current_[std::min(i, current_.size() - 1)];
    // The colours run left to right across the card, so the glow along the top
    // and bottom edges changes colour, and the left and right edges each wear
    // their own end of it.
    std::array<SkColor4f, aurora::kSlots> colors;
    for (size_t c = 0; c < aurora::kSlots; ++c) {
      const SkColor4f base = SkColor4f::FromColor(palette[c]);
      colors[c] = {base.fR, base.fG, base.fB, opacity};
    }
    const SkScalar stops[] = {0.0f, 0.5f, 1.0f};
    const SkPoint ends[] = {
        SkPoint::Make(static_cast<float>(card.rect.x()), 0),
        SkPoint::Make(static_cast<float>(card.rect.right()), 0)};
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setShader(cc::PaintShader::MakeLinearGradient(
        ends, colors.data(), stops, aurora::kSlots, SkTileMode::kClamp));
    flags.setImageFilter(sk_make_sp<cc::BlurPaintFilter>(
        kHaloSigma, kHaloSigma, SkTileMode::kDecal, nullptr));
    SkRect rect = gfx::RectToSkRect(card.rect);
    rect.outset(kHaloOutset, kHaloOutset);
    const float radius = card.radius + kHaloOutset;
    canvas->sk_canvas()->drawRRect(SkRRect::MakeRectXY(rect, radius, radius),
                                   flags);
  }
}

void ZephyrusAuroraView::ScheduleCapture(base::TimeDelta delay) {
  if (!enabled_ || panes_.empty()) {
    return;
  }
  // Later of "after the settle delay" and "not within the minimum gap of the
  // last one".
  const base::TimeTicks now = base::TimeTicks::Now();
  const base::TimeTicks earliest = last_capture_.is_null()
                                       ? now
                                       : last_capture_ + kMinCaptureGap;
  const base::TimeTicks when = std::max(now + delay, earliest);
  capture_timer_.Start(FROM_HERE, when - now,
                       base::BindOnce(&ZephyrusAuroraView::CapturePages,
                                      base::Unretained(this)));
}

void ZephyrusAuroraView::CapturePages() {
  if (!enabled_ || !GetWidget() || !GetWidget()->IsVisible()) {
    return;
  }
  last_capture_ = base::TimeTicks::Now();
  for (const auto& pane : panes_) {
    content::WebContents* contents = pane.get();
    if (!contents ||
        contents->GetVisibility() != content::Visibility::VISIBLE) {
      continue;
    }
    content::RenderWidgetHostView* view = contents->GetRenderWidgetHostView();
    if (!view) {
      continue;
    }
    view->CopyFromSurface(
        gfx::Rect(), gfx::Size(kCaptureWidth, kCaptureHeight),
        base::Seconds(3),
        base::BindOnce(&ZephyrusAuroraView::OnCaptured,
                       weak_ptr_factory_.GetWeakPtr(), pane));
  }
}

void ZephyrusAuroraView::OnCaptured(
    base::WeakPtr<content::WebContents> contents,
    const content::CopyFromSurfaceResult& result) {
  if (!enabled_ || !contents || !result.has_value() ||
      result->bitmap.drawsNothing()) {
    return;
  }
  SkBitmap small = result->bitmap;
  if (small.width() > kReadWidth || small.height() > kReadHeight) {
    small = skia::ImageOperations::Resize(
        small, skia::ImageOperations::RESIZE_BOX, kReadWidth, kReadHeight);
    if (small.drawsNothing()) {
      return;
    }
  }
  AuroraTabData* data =
      AuroraTabData::GetOrCreateForWebContents(contents.get());
  data->colors = aurora::FillSlots(aurora::ExtractVividColors(small),
                                   FallbackColor(contents.get()));
  data->taken = base::TimeTicks::Now();
  for (const auto& pane : panes_) {
    if (pane.get() == contents.get()) {
      ShowColors(Compose(), /*animate=*/true);
      break;
    }
  }
}

BEGIN_METADATA(ZephyrusAuroraView)
END_METADATA

}  // namespace zephyrus
