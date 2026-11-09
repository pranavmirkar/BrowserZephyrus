// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_aurora_view.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "skia/ext/image_operations.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/animation/animation_delegate_views.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace zephyrus {

namespace {

// How far down the page the picture is taken at, and how long a page gets to
// settle before it is read. Pages keep painting for a moment after "loaded"
// (fonts, hero images), and a picture of the half-built page would be the wrong
// colour for the whole time the page is open.
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
// Opacity at the brightest point of a curtain, as a fraction. The chrome has
// text and icons under it, so this is a tint, never a fill: a light chrome
// takes less than a dark one, because colour on a light surface reads as
// stronger than the same colour on a dark one.
constexpr float kPeakOnDark = 0.40f;
constexpr float kPeakOnLight = 0.28f;
// Each curtain's share of the peak: the front one is brightest, which is what
// gives the stack depth.
constexpr std::array<float, aurora::kSlots> kCurtainStrength = {1.0f, 0.8f,
                                                                0.9f};

// Where each curtain sits across the width (its left edge, as a fraction of
// the width) and how wide it is (a fraction too). They overlap, so there is
// never a bare gap between them. Each is brightest at the bottom edge of the
// bar, which is the top edge of the page card, and rises from there.
// How tall a curtain is, as a multiple of the bar: over 1 so a little light
// still reaches the top edge of the window.
constexpr std::array<float, aurora::kSlots> kCurtainLeft = {-0.06f, 0.26f,
                                                            0.56f};
constexpr std::array<float, aurora::kSlots> kCurtainWidth = {0.50f, 0.48f,
                                                             0.50f};
constexpr float kCurtainRise = 1.15f;
// The last picture of a page, kept with the tab so switching back to it does
// not wait on a new one.
class AuroraTabData : public content::WebContentsUserData<AuroraTabData> {
 public:
  ~AuroraTabData() override = default;

  std::array<SkColor, aurora::kSlots> colors{};
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
  // Behind the bar, and out of the pointer's way entirely.
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetIsIgnored(true);
  morph_ = std::make_unique<Morph>(
      this, base::BindRepeating(&ZephyrusAuroraView::OnMorphFrame,
                                base::Unretained(this)));
}

ZephyrusAuroraView::~ZephyrusAuroraView() {
  // The timers die with this view; the pending capture's callback holds a weak
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
    // The toolbar paints its own fill again; make it do so now.
    if (parent()) {
      parent()->SchedulePaint();
    }
    return;
  }
  SetVisible(true);
  if (parent()) {
    parent()->SchedulePaint();
  }
  std::vector<content::WebContents*> panes;
  for (const auto& pane : panes_) {
    if (pane) {
      panes.push_back(pane.get());
    }
  }
  OnPanesChanged(panes);
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

std::array<SkColor, aurora::kSlots> ZephyrusAuroraView::Compose() const {
  std::vector<std::array<SkColor, aurora::kSlots>> pages;
  for (const auto& pane : panes_) {
    if (!pane) {
      continue;
    }
    if (AuroraTabData* data = AuroraTabData::FromWebContents(pane.get())) {
      pages.push_back(data->colors);
    }
  }
  if (pages.empty()) {
    return aurora::FillSlots({}, FallbackColor(nullptr));
  }
  if (pages.size() == 1) {
    return pages[0];
  }
  // Two pages: the left curtain is the left page's, the right curtain is the
  // right page's, and the one between is where they meet.
  const SkColor left = pages[0][0];
  const SkColor right = pages[1][0];
  return {left,
          aurora::ToAuroraTone(SkColorSetRGB(
              (SkColorGetR(left) + SkColorGetR(right)) / 2,
              (SkColorGetG(left) + SkColorGetG(right)) / 2,
              (SkColorGetB(left) + SkColorGetB(right)) / 2)),
          right};
}

void ZephyrusAuroraView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SchedulePaint();
}

void ZephyrusAuroraView::ViewHierarchyChanged(
    const views::ViewHierarchyChangedDetails& details) {
  views::View::ViewHierarchyChanged(details);
  if (details.child != this) {
    return;
  }
  parent_observation_.Reset();
  if (details.is_add && details.parent) {
    parent_observation_.Observe(details.parent);
    FitToParent();
  }
}

void ZephyrusAuroraView::OnViewBoundsChanged(views::View* observed_view) {
  FitToParent();
}

void ZephyrusAuroraView::OnViewIsDeleting(views::View* observed_view) {
  // The parent goes before its children; drop the observation while it is still
  // there to drop, or the scoped observation would remove itself from a view
  // that is no longer.
  parent_observation_.Reset();
}

void ZephyrusAuroraView::FitToParent() {
  if (parent()) {
    SetBoundsRect(parent()->GetLocalBounds());
  }
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

void ZephyrusAuroraView::ShowColors(
    const std::array<SkColor, aurora::kSlots>& colors,
    bool animate) {
  if (has_colors_ && colors == target_) {
    return;
  }
  if (!has_colors_ || !animate || !gfx::Animation::ShouldRenderRichAnimation() ||
      !GetVisible()) {
    morph_->Stop();
    current_ = target_ = from_ = colors;
    has_colors_ = true;
    SchedulePaint();
    return;
  }
  // Leave from wherever the last fade had got to, so a quick second change
  // does not jump back to the old colours first.
  from_ = current_;
  target_ = colors;
  morph_->Start();
}

void ZephyrusAuroraView::OnMorphFrame() {
  const double t = morph_->running() ? morph_->progress() : 1.0;
  for (size_t i = 0; i < current_.size(); ++i) {
    current_[i] = gfx::Tween::ColorValueBetween(t, from_[i], target_[i]);
  }
  SchedulePaint();
}

void ZephyrusAuroraView::OnPaint(gfx::Canvas* canvas) {
  const gfx::Rect bounds = GetLocalBounds();
  if (bounds.IsEmpty()) {
    return;
  }
  // The bar's own fill first: the toolbar stands aside while this is showing, so
  // this is what the bar looks like with no page colour in it at all.
  const ui::ColorProvider* provider = GetColorProvider();
  canvas->FillRect(bounds, provider ? provider->GetColor(kColorToolbar)
                                    : SK_ColorDKGRAY);
  if (!has_colors_) {
    return;
  }

  const float peak = IsDarkChrome() ? kPeakOnDark : kPeakOnLight;
  const float width = static_cast<float>(bounds.width());
  const float height = static_cast<float>(bounds.height());
  gfx::ScopedCanvas scoped(canvas);
  canvas->ClipRect(bounds);
  for (size_t i = 0; i < current_.size(); ++i) {
    const float opacity = peak * kCurtainStrength[i];
    const float rx = std::max(1.0f, width * kCurtainWidth[i] / 2.0f);
    const float ry = std::max(1.0f, height * kCurtainRise);
    const float cx = width * (kCurtainLeft[i] + kCurtainWidth[i] / 2.0f);
    const SkColor4f base = SkColor4f::FromColor(current_[i]);
    // Three stops are what makes it a glow and not a disc: a bright core, a long
    // soft shoulder, then nothing.
    const SkColor4f colors[] = {
        {base.fR, base.fG, base.fB, opacity},
        {base.fR, base.fG, base.fB, opacity * 0.42f},
        {base.fR, base.fG, base.fB, 0.0f}};
    const SkScalar stops[] = {0.0f, 0.5f, 1.0f};
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    // A unit-radius gradient, stretched to the curtain and anchored on the
    // bottom edge of the bar: the same trick the onboarding haze uses, so the
    // ellipse is one draw and no mask.
    flags.setShader(cc::PaintShader::MakeRadialGradient(
        SkPoint::Make(0, 0), 1.0f, colors, stops, 3, SkTileMode::kClamp));
    gfx::ScopedCanvas curtain(canvas);
    canvas->Translate(gfx::Vector2d(static_cast<int>(cx), bounds.height()));
    canvas->sk_canvas()->scale(rx, ry);
    canvas->sk_canvas()->drawRect(SkRect::MakeLTRB(-1.0f, -1.0f, 1.0f, 0.0f),
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
  AuroraTabData* data = AuroraTabData::GetOrCreateForWebContents(contents.get());
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
