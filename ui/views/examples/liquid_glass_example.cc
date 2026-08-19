// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/examples/liquid_glass_example.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkScalar.h"
#include "third_party/skia/include/core/SkTileMode.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/examples/examples_window.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/liquid_glass.h"
#include "ui/views/view.h"

namespace views::examples {

namespace {

// --- Small helpers ---------------------------------------------------------

// A white label tuned for legibility over glass/vibrant content: no subpixel
// (translucent backdrops can't back LCD text) and no auto-recoloring.
std::unique_ptr<Label> MakeGlassLabel(const std::u16string& text,
                                      int size_delta,
                                      bool bold) {
  auto label = std::make_unique<Label>(text);
  label->SetAutoColorReadabilityEnabled(false);
  label->SetEnabledColor(SK_ColorWHITE);
  label->SetSubpixelRenderingEnabled(false);
  if (size_delta != 0 || bold) {
    label->SetFontList(gfx::FontList().Derive(
        size_delta, gfx::Font::NORMAL,
        bold ? gfx::Font::Weight::BOLD : gfx::Font::Weight::NORMAL));
  }
  return label;
}

// Maps a slider value in [0,1] into [min,max].
float Lerp(float value, float min, float max) {
  return min + std::clamp(value, 0.f, 1.f) * (max - min);
}

// --- Colorful content that scrolls behind the glass ------------------------

// Paints a top-to-bottom gradient rounded rect. Used for the page backdrop and
// for the little "album art" cards.
class GradientBackground : public Background {
 public:
  GradientBackground(SkColor top, SkColor bottom, float radius)
      : top_(top), bottom_(bottom), radius_(radius) {}
  GradientBackground(const GradientBackground&) = delete;
  GradientBackground& operator=(const GradientBackground&) = delete;
  ~GradientBackground() override = default;

  void Paint(gfx::Canvas* canvas, View* view) const override {
    const gfx::RectF b(view->GetLocalBounds());
    if (b.IsEmpty()) {
      return;
    }
    const SkPoint pts[2] = {SkPoint::Make(b.x(), b.y()),
                            SkPoint::Make(b.x(), b.bottom())};
    const SkColor4f colors[2] = {SkColor4f::FromColor(top_),
                                 SkColor4f::FromColor(bottom_)};
    const SkScalar pos[2] = {0.f, 1.f};
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setShader(cc::PaintShader::MakeLinearGradient(pts, colors, pos, 2,
                                                        SkTileMode::kClamp));
    canvas->DrawRoundRect(b, radius_, flags);
  }

 private:
  SkColor top_;
  SkColor bottom_;
  float radius_;
};

// Vivid Apple-ish gradient pairs for the cards.
struct ColorPair {
  SkColor a;
  SkColor b;
};
const ColorPair kPalette[] = {
    {SkColorSetRGB(0xFF, 0x5E, 0x62), SkColorSetRGB(0xFF, 0x99, 0x66)},
    {SkColorSetRGB(0x4E, 0x54, 0xC8), SkColorSetRGB(0x8F, 0x94, 0xFB)},
    {SkColorSetRGB(0x11, 0x99, 0x8E), SkColorSetRGB(0x38, 0xEF, 0x7D)},
    {SkColorSetRGB(0xFC, 0x5C, 0x7D), SkColorSetRGB(0x6A, 0x82, 0xFB)},
    {SkColorSetRGB(0xF7, 0x97, 0x1E), SkColorSetRGB(0xFF, 0xD2, 0x00)},
    {SkColorSetRGB(0xC3, 0x37, 0x64), SkColorSetRGB(0x1D, 0x26, 0x71)},
    {SkColorSetRGB(0x00, 0xC6, 0xFF), SkColorSetRGB(0x00, 0x72, 0xFF)},
    {SkColorSetRGB(0xF8, 0x57, 0xA6), SkColorSetRGB(0xFF, 0x58, 0x58)},
    {SkColorSetRGB(0x43, 0xCE, 0xA2), SkColorSetRGB(0x18, 0x5A, 0x9D)},
    {SkColorSetRGB(0x7F, 0x00, 0xFF), SkColorSetRGB(0xE1, 0x00, 0xFF)},
    {SkColorSetRGB(0x21, 0x93, 0xB0), SkColorSetRGB(0x6D, 0xD5, 0xED)},
    {SkColorSetRGB(0xBA, 0x53, 0x70), SkColorSetRGB(0xF4, 0x9C, 0x7A)},
};

std::unique_ptr<View> MakeGradientCard(const std::u16string& caption,
                                       const ColorPair& colors) {
  auto card = std::make_unique<View>();
  card->SetPreferredSize(gfx::Size(150, 116));
  card->SetBackground(
      std::make_unique<GradientBackground>(colors.a, colors.b, 18.f));
  auto* box = card->SetLayoutManager(std::make_unique<BoxLayout>(
      BoxLayout::Orientation::kVertical, gfx::Insets(12), 0));
  box->set_main_axis_alignment(BoxLayout::MainAxisAlignment::kEnd);
  box->set_cross_axis_alignment(BoxLayout::CrossAxisAlignment::kStart);
  card->AddChildView(MakeGlassLabel(caption, 0, true));
  return card;
}

std::unique_ptr<View> MakeCardRow(int start_index) {
  auto row = std::make_unique<View>();
  auto* box = row->SetLayoutManager(std::make_unique<BoxLayout>(
      BoxLayout::Orientation::kHorizontal, gfx::Insets(), 14));
  box->set_cross_axis_alignment(BoxLayout::CrossAxisAlignment::kStart);
  constexpr int kCount = 4;
  const auto palette = base::span(kPalette);
  const int n = static_cast<int>(palette.size());
  for (int i = 0; i < kCount; ++i) {
    const size_t idx = static_cast<size_t>((start_index + i) % n);
    row->AddChildView(MakeGradientCard(
        base::UTF8ToUTF16(base::StringPrintf("Track %d", idx + 1)),
        palette[idx]));
  }
  return row;
}

std::unique_ptr<View> MakeSectionLabel(const std::u16string& text) {
  auto label = MakeGlassLabel(text, 6, true);
  return label;
}

// --- iOS-style toggle switch (animated, paint-based) -----------------------
//
// Painted (not layer-backed) so it renders reliably as a descendant of the
// scrolling content. A SlideAnimation drives both the knob position and the
// track color, giving the iOS knob-slide + green-fill on toggle.

class ToggleSwitch : public View, public gfx::AnimationDelegate {
  METADATA_HEADER(ToggleSwitch, View)

 public:
  explicit ToggleSwitch(bool on) : on_(on), anim_(this) {
    SetPreferredSize(gfx::Size(kWidth, kHeight));
    anim_.SetSlideDuration(base::Milliseconds(220));
    anim_.Reset(on_ ? 1.0 : 0.0);
  }
  ToggleSwitch(const ToggleSwitch&) = delete;
  ToggleSwitch& operator=(const ToggleSwitch&) = delete;
  ~ToggleSwitch() override = default;

  void OnPaint(gfx::Canvas* canvas) override {
    const float t = static_cast<float>(anim_.GetCurrentValue());
    const gfx::RectF b(GetLocalBounds());
    // Track: gray -> iOS green.
    cc::PaintFlags track;
    track.setAntiAlias(true);
    track.setColor(gfx::Tween::ColorValueBetween(t, kOffColor, kOnColor));
    canvas->DrawRoundRect(b, b.height() / 2.f, track);
    // Knob: slides left -> right.
    const float knob = b.height() - 2 * kMargin;
    const float x = b.x() + kMargin + t * (b.width() - knob - 2 * kMargin);
    cc::PaintFlags kf;
    kf.setAntiAlias(true);
    kf.setColor(SK_ColorWHITE);
    canvas->DrawRoundRect(gfx::RectF(x, b.y() + kMargin, knob, knob),
                          knob / 2.f, kf);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    return event.IsOnlyLeftMouseButton();
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (!GetLocalBounds().Contains(event.location())) {
      return;
    }
    on_ = !on_;
    if (!gfx::Animation::ShouldRenderRichAnimation()) {
      anim_.Reset(on_ ? 1.0 : 0.0);
      SchedulePaint();
    } else if (on_) {
      anim_.Show();
    } else {
      anim_.Hide();
    }
  }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    SchedulePaint();
  }

 private:
  static constexpr int kWidth = 48;
  static constexpr int kHeight = 30;
  static constexpr int kMargin = 3;
  static constexpr SkColor kOnColor = SkColorSetRGB(0x34, 0xC7, 0x59);
  static constexpr SkColor kOffColor = SkColorSetRGB(0x55, 0x55, 0x5C);

  bool on_;
  gfx::SlideAnimation anim_;
};

BEGIN_METADATA(ToggleSwitch)
END_METADATA

std::unique_ptr<View> MakeToggleRow(const std::u16string& text, bool on) {
  auto row = std::make_unique<View>();
  row->SetBackground(CreateRoundedRectBackground(
      SkColorSetARGB(0x1F, 0xFF, 0xFF, 0xFF), 14.f));
  auto* box = row->SetLayoutManager(std::make_unique<BoxLayout>(
      BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(14, 22), 12));
  box->set_cross_axis_alignment(BoxLayout::CrossAxisAlignment::kCenter);
  row->AddChildView(std::make_unique<ToggleSwitch>(on));
  auto* label = row->AddChildView(MakeGlassLabel(text, 1, false));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  box->SetFlexForView(label, 1);
  return row;
}

// --- Apple-style segmented control with a sliding glass indicator ----------

class SegmentedControl : public View {
  METADATA_HEADER(SegmentedControl, View)

 public:
  explicit SegmentedControl(const std::vector<std::u16string>& items) {
    SetPreferredSize(
        gfx::Size(static_cast<int>(items.size()) * 92 + 6, kHeight));
    SetBackground(CreateRoundedRectBackground(
        SkColorSetARGB(0x50, 0x00, 0x00, 0x0A), kRadius));

    // Selection pill sits behind the labels (added first). It's a near-opaque
    // white so the selected label reads as dark text on a bright chip.
    indicator_ = AddChildView(std::make_unique<View>());
    indicator_->SetPaintToLayer(ui::LAYER_SOLID_COLOR);
    indicator_->layer()->SetColor(SkColorSetARGB(0xF0, 0xFF, 0xFF, 0xFF));
    indicator_->layer()->SetRoundedCornerRadius(
        gfx::RoundedCornersF(kRadius - 3));
    indicator_->layer()->SetIsFastRoundedCorner(true);

    for (const auto& item : items) {
      // Labels are layer-backed so they composite above the indicator layer.
      auto label = MakeGlassLabel(item, 0, true);
      label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
      auto* raw = AddChildView(std::move(label));
      raw->SetPaintToLayer();
      raw->layer()->SetFillsBoundsOpaquely(false);
      labels_.push_back(raw);
    }
    UpdateLabelColors();
  }
  SegmentedControl(const SegmentedControl&) = delete;
  SegmentedControl& operator=(const SegmentedControl&) = delete;
  ~SegmentedControl() override = default;

  void Layout(PassKey) override {
    const int n = static_cast<int>(labels_.size());
    if (n == 0) {
      return;
    }
    const float seg_w = static_cast<float>(width()) / n;
    for (int i = 0; i < n; ++i) {
      labels_[i]->SetBounds(static_cast<int>(i * seg_w), 0,
                            static_cast<int>(seg_w), height());
    }
    indicator_->SetBounds(kMargin, kMargin, static_cast<int>(seg_w) - 2 * kMargin,
                          height() - 2 * kMargin);
    indicator_->layer()->SetTransform(TransformForIndex(selected_, seg_w));
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    const int n = static_cast<int>(labels_.size());
    if (n == 0 || width() == 0) {
      return false;
    }
    const int idx = std::clamp(event.location().x() * n / width(), 0, n - 1);
    Select(idx, /*animate=*/true);
    return true;
  }

 private:
  static constexpr int kHeight = 34;
  static constexpr int kRadius = 15;
  static constexpr int kMargin = 3;

  gfx::Transform TransformForIndex(int index, float seg_w) const {
    gfx::Transform t;
    t.Translate(index * seg_w, 0);
    return t;
  }

  void Select(int index, bool animate) {
    selected_ = index;
    const int n = std::max(1, static_cast<int>(labels_.size()));
    const float seg_w = static_cast<float>(width()) / n;
    const gfx::Transform target = TransformForIndex(index, seg_w);
    if (!animate || !gfx::Animation::ShouldRenderRichAnimation()) {
      indicator_->layer()->SetTransform(target);
    } else {
      // Apple response ~0.3s with a touch of settle.
      AnimationBuilder().Once().SetDuration(base::Milliseconds(300)).SetTransform(
          indicator_->layer(), target, gfx::Tween::FAST_OUT_SLOW_IN_3);
    }
    UpdateLabelColors();
  }

  void UpdateLabelColors() {
    for (size_t i = 0; i < labels_.size(); ++i) {
      labels_[i]->SetEnabledColor(
          static_cast<int>(i) == selected_
              ? SkColorSetRGB(0x1A, 0x1A, 0x22)
              : SkColorSetARGB(0xCC, 0xFF, 0xFF, 0xFF));
    }
  }

  std::vector<raw_ptr<Label>> labels_;
  raw_ptr<View> indicator_ = nullptr;
  int selected_ = 0;
};

BEGIN_METADATA(SegmentedControl)
END_METADATA

// --- The lab: scrolling page behind, floating glass chrome on top ----------

class GlassLab : public View {
  METADATA_HEADER(GlassLab, View)

 public:
  GlassLab() = default;
  GlassLab(const GlassLab&) = delete;
  GlassLab& operator=(const GlassLab&) = delete;
  ~GlassLab() override = default;

  void Layout(PassKey) override {
    const gfx::Rect c = GetContentsBounds();
    if (scroll) {
      scroll->SetBoundsRect(c);
    }
    constexpr int kMargin = 16;
    if (toolbar) {
      const int w = std::min(c.width() - 2 * kMargin, 460);
      toolbar->SetBounds(c.x() + (c.width() - w) / 2, c.y() + kMargin, w, 60);
    }
    if (controls) {
      const int w = std::min(c.width() - 2 * kMargin, 560);
      constexpr int kH = 208;
      controls->SetBounds(c.x() + (c.width() - w) / 2,
                          c.bottom() - kH - kMargin, w, kH);
    }
    if (!entrance_played_ && !c.IsEmpty()) {
      entrance_played_ = true;
      if (toolbar) {
        AnimateLiquidGlassIn(toolbar, base::Milliseconds(0));
      }
      if (controls) {
        AnimateLiquidGlassIn(controls, base::Milliseconds(90));
      }
    }
  }

  raw_ptr<ScrollView> scroll = nullptr;
  raw_ptr<View> toolbar = nullptr;
  raw_ptr<View> controls = nullptr;
  bool entrance_played_ = false;
};

BEGIN_METADATA(GlassLab)
END_METADATA

std::unique_ptr<View> BuildApplePage() {
  auto page = std::make_unique<View>();
  page->SetBackground(std::make_unique<GradientBackground>(
      SkColorSetRGB(0x0E, 0x10, 0x2A), SkColorSetRGB(0x2A, 0x0E, 0x38), 0.f));
  auto* box = page->SetLayoutManager(std::make_unique<BoxLayout>(
      BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(120, 22, 160, 22),
      18));
  box->set_cross_axis_alignment(BoxLayout::CrossAxisAlignment::kStretch);

  page->AddChildView(MakeGlassLabel(u"Liquid Glass", 22, true));
  page->AddChildView(MakeGlassLabel(
      u"Scroll the page — watch the glass refract what passes behind it.", 1,
      false));

  page->AddChildView(MakeSectionLabel(u"Featured"));
  page->AddChildView(MakeCardRow(0));
  page->AddChildView(MakeCardRow(4));

  page->AddChildView(MakeSectionLabel(u"Settings"));
  page->AddChildView(MakeToggleRow(u"Frosted blur", true));
  page->AddChildView(MakeToggleRow(u"Edge refraction", true));
  page->AddChildView(MakeToggleRow(u"Specular highlight", false));

  page->AddChildView(MakeSectionLabel(u"More to scroll"));
  page->AddChildView(MakeCardRow(8));
  page->AddChildView(MakeCardRow(2));
  page->AddChildView(MakeCardRow(6));
  page->AddChildView(MakeCardRow(10));
  return page;
}

}  // namespace

LiquidGlassExample::LiquidGlassExample() : ExampleBase("Liquid Glass") {}

LiquidGlassExample::~LiquidGlassExample() = default;

void LiquidGlassExample::CreateExampleView(View* container) {
  // The toolbar is the surface the sliders tune; start params from its preset
  // so the sliders begin at matching positions.
  params_ = LiquidGlassParams::Dark();
  params_.corner_radius = 26.f;
  params_.blur_sigma = 24.f;

  container->SetLayoutManager(std::make_unique<FillLayout>());
  auto* lab = container->AddChildView(std::make_unique<GlassLab>());

  // The scrolling Apple-style page sits behind everything. ClipHeightTo() makes
  // the ScrollView "bounded", so it sizes the content to the viewport width and
  // scrolls it vertically (otherwise the content keeps its 0x0 initial size).
  // Layered scrolling: composited scrolling, and (crucially) layer-backed
  // descendants like the toggle switches stay glued to their scrolled position.
  auto* scroll = lab->AddChildView(
      std::make_unique<ScrollView>(ScrollView::ScrollWithLayers::kEnabled));
  scroll->ClipHeightTo(0, 100000);
  scroll->SetBackgroundColor(
      std::optional<ui::ColorVariant>(SkColorSetRGB(0x0E, 0x10, 0x2A)));
  scroll->SetContents(BuildApplePage());
  lab->scroll = scroll;

  // Floating glass toolbar with brand + segmented control.
  {
    auto toolbar = std::make_unique<View>();
    auto* box = toolbar->SetLayoutManager(std::make_unique<BoxLayout>(
        BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 18), 14));
    box->set_cross_axis_alignment(BoxLayout::CrossAxisAlignment::kCenter);
    auto* brand = toolbar->AddChildView(MakeGlassLabel(u"Zephyrus", 2, true));
    box->SetFlexForView(brand, 1);
    toolbar->AddChildView(std::make_unique<SegmentedControl>(
        std::vector<std::u16string>{u"Home", u"Discover", u"Library"}));
    tunable_panel_ = lab->AddChildView(std::move(toolbar));
    lab->toolbar = tunable_panel_;
  }

  // Floating glass controls: readout + four sliders that tune the toolbar.
  auto controls = std::make_unique<View>();
  controls->SetLayoutManager(std::make_unique<BoxLayout>(
      BoxLayout::Orientation::kVertical, gfx::Insets(16), 2));
  auto* readout =
      controls->AddChildView(MakeGlassLabel(u"Tune the glass ↓", 1, true));
  readout_label_ = readout;

  auto add_slider = [&](const std::u16string& name, float initial) -> Slider* {
    controls->AddChildView(MakeGlassLabel(name, 0, false));
    auto* slider = controls->AddChildView(std::make_unique<Slider>(this));
    slider->GetViewAccessibility().SetName(name);
    slider->SetValue(initial);
    return slider;
  };
  blur_slider_ = add_slider(u"Blur", params_.blur_sigma / 40.f);
  zoom_slider_ =
      add_slider(u"Refraction", (params_.refraction_zoom - 1.f) / 0.4f);
  radius_slider_ = add_slider(u"Corner radius", params_.corner_radius / 40.f);
  tint_slider_ =
      add_slider(u"Tint", SkColorGetA(params_.tint_color) / 255.f / 0.5f);

  auto* controls_view = lab->AddChildView(std::move(controls));
  lab->controls = controls_view;

  // Apply the material to the floating chrome.
  ApplyLiquidGlass(tunable_panel_, params_);

  LiquidGlassParams controls_glass = LiquidGlassParams::Dark();
  controls_glass.corner_radius = 24.f;
  controls_glass.blur_sigma = 28.f;
  ApplyLiquidGlass(controls_view, controls_glass);
}

void LiquidGlassExample::SliderValueChanged(Slider* sender,
                                            float value,
                                            float old_value,
                                            SliderChangeReason reason) {
  ApplyFromSliders();
}

void LiquidGlassExample::ApplyFromSliders() {
  // During setup, Slider::SetValue() notifies this listener synchronously
  // before all slider pointers are assigned. Bail until fully constructed.
  if (!tunable_panel_ || !blur_slider_ || !zoom_slider_ || !radius_slider_ ||
      !tint_slider_) {
    return;
  }
  params_.blur_sigma = Lerp(blur_slider_->GetValue(), 0.f, 40.f);
  params_.refraction_zoom = Lerp(zoom_slider_->GetValue(), 1.f, 1.4f);
  params_.refraction_inset = 12;
  params_.corner_radius = Lerp(radius_slider_->GetValue(), 0.f, 40.f);
  const int tint_alpha =
      static_cast<int>(Lerp(tint_slider_->GetValue(), 0.f, 0.5f) * 255.f);
  params_.tint_color = SkColorSetA(SK_ColorWHITE, tint_alpha);

  ApplyLiquidGlass(tunable_panel_, params_);

  if (readout_label_) {
    readout_label_->SetText(base::UTF8ToUTF16(base::StringPrintf(
        "blur=%.0f  zoom=%.2f  radius=%.0f  tintA=%d", params_.blur_sigma,
        params_.refraction_zoom, params_.corner_radius, tint_alpha)));
  }
}

}  // namespace views::examples
