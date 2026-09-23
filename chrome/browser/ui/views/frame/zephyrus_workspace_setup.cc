// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_workspace_setup.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_customize_panel.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3_switch.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_icons.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_image.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/dynamic_color/palette.h"
#include "ui/color/dynamic_color/palette_factory.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/text_elider.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_delegate_views.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

// ============================================================================
// The workspace setup card, in Material 3 Expressive.
//
// The card's centre of gravity is a LIVE PREVIEW: a hero tile painted in the
// real tonal palette of the colour being chosen (libmonet, the same generator
// the browser itself will use), wearing the chosen icon in that icon's own
// expressive shape. Every choice below it updates the hero immediately -- you
// see the workspace before it exists, instead of reading a list of settings.
//
// Expressive, concretely:
//   - shape carries selection: a chosen icon or colour MORPHS from a circle
//     into its shape, on a spatial spring that overshoots and settles;
//   - colour moves on the effects spring when the hero re-tints;
//   - light/dark is a connected button group whose selected segment springs
//     fully round;
//   - containers are large and soft (24 / 16 / full), and type is the M3
//     scale with the emphasized weight where something must carry.
// ============================================================================

namespace {

using zephyrus::AllWorkspaceIcons;
using zephyrus::FindWorkspaceIcon;
using zephyrus::PaintWorkspaceGlyph;
using zephyrus::Shape;
using zephyrus::ShapePath;
using zephyrus::WorkspaceIcon;
namespace m3 = zephyrus::m3;

constexpr int kCardWidth = 404;
// Rule 2. The hero is the only element that reaches the dialog's corners, so
// it is the one that must nest: it sits kHeroInset from the edge and takes the
// dialog radius minus that. At the body's 20dp it would have had to be 8 to be
// concentric -- so instead the hero comes out to the edge, the way an M3
// header image does, and everything else keeps the body's padding.
constexpr int kHeroInset = 8;
constexpr int kBodyInset = 20;
constexpr float kHeroCorner = static_cast<float>(
    zephyrus::m3::ConcentricInner(zephyrus::kRadiusPopup, kHeroInset));
constexpr float kCardCorner = 24.f;
constexpr float kFieldCorner = 16.f;

struct Seed {
  SkColor color;
  const char16_t* name;
};
constexpr std::array<Seed, 9> kSeeds = {{
    {SkColorSetRGB(0x1E, 0x6F, 0xD9), u"Blue"},
    {SkColorSetRGB(0x00, 0x89, 0x7B), u"Teal"},
    {SkColorSetRGB(0x2E, 0x7D, 0x32), u"Green"},
    {SkColorSetRGB(0xF9, 0xA8, 0x25), u"Amber"},
    {SkColorSetRGB(0xEF, 0x6C, 0x00), u"Orange"},
    {SkColorSetRGB(0xD8, 0x1B, 0x60), u"Rose"},
    {SkColorSetRGB(0x7B, 0x1F, 0xA2), u"Purple"},
    {SkColorSetRGB(0x39, 0x49, 0xAB), u"Indigo"},
    {SkColorSetRGB(0x54, 0x6E, 0x7A), u"Slate"},
}};

// ---------------------------------------------------------------------------
// Tones
// ---------------------------------------------------------------------------

// The four roles the preview paints with, for one seed in one mode.
struct Tones {
  SkColor container;     // primaryContainer
  SkColor on_container;  // onPrimaryContainer
  SkColor accent;        // primary
  SkColor on_accent;     // onPrimary
};

Tones Blend(const Tones& from, const Tones& to, double t) {
  const float a = std::clamp(static_cast<float>(t), 0.f, 1.f);
  return {color_utils::AlphaBlend(to.container, from.container, a),
          color_utils::AlphaBlend(to.on_container, from.on_container, a),
          color_utils::AlphaBlend(to.accent, from.accent, a),
          color_utils::AlphaBlend(to.on_accent, from.on_accent, a)};
}

// The roles as the browser will actually draw them. No seed means Zephyrus's
// own hand-tuned Pomegranate ramp (ref_color_mixer.cc), which libmonet would
// not reproduce from the brand colour -- so those tones are the ramp's own.
Tones TonesFor(std::optional<SkColor> seed, bool dark) {
  if (!seed) {
    return dark ? Tones{SkColorSetRGB(0x92, 0x01, 0x1E),
                        SkColorSetRGB(0xFF, 0xDA, 0xD6),
                        SkColorSetRGB(0xFF, 0xB3, 0xAD),
                        SkColorSetRGB(0x68, 0x01, 0x12)}
                : Tones{SkColorSetRGB(0xFF, 0xDA, 0xD6),
                        SkColorSetRGB(0x40, 0x02, 0x01),
                        SkColorSetRGB(0xC6, 0x10, 0x2E), SK_ColorWHITE};
  }
  const std::unique_ptr<ui::Palette> palette = ui::GeneratePalette(
      *seed, ui::ColorProviderKey::SchemeVariant::kTonalSpot);
  const ui::TonalPalette& primary = palette->primary();
  return dark ? Tones{primary.get(30), primary.get(90), primary.get(80),
                      primary.get(20)}
              : Tones{primary.get(90), primary.get(10), primary.get(40),
                      primary.get(100)};
}

// ---------------------------------------------------------------------------
// Motion
// ---------------------------------------------------------------------------

// One M3 spring-driven value: a linear clock through m3::Curve(), so spatial
// springs overshoot and effects springs do not.
class SpringValue {
 public:
  SpringValue(gfx::AnimationDelegate* delegate, m3::Spring spring)
      : spring_(spring), clock_(delegate) {
    clock_.SetDuration(m3::Duration(spring));
  }
  void Restart() {
    clock_.End();
    clock_.Start();
  }
  bool Owns(const gfx::Animation* animation) const {
    return animation == &clock_;
  }
  // 1 at rest, so a value nobody animated reads as "arrived".
  double Get() const {
    return clock_.is_animating()
               ? m3::Curve(spring_).Solve(clock_.GetCurrentValue())
               : 1.0;
  }

 private:
  const m3::Spring spring_;
  gfx::LinearAnimation clock_;
};

// ---------------------------------------------------------------------------
// Hero preview
// ---------------------------------------------------------------------------

class HeroPreview : public views::View, public views::AnimationDelegateViews {
  METADATA_HEADER(HeroPreview, views::View)

 public:
  explicit HeroPreview(std::u16string overline)
      : views::AnimationDelegateViews(this), overline_(std::move(overline)) {
    SetPreferredSize(
        gfx::Size(kCardWidth + 2 * (kBodyInset - kHeroInset), 128));
    GetViewAccessibility().SetRole(ax::mojom::Role::kImage);
  }

  void SetLook(const Tones& tones, bool animate) {
    from_ = animate ? Current() : tones;
    to_ = tones;
    if (animate) {
      tint_.Restart();
    }
    SchedulePaint();
  }

  void SetMark(const WorkspaceIcon* icon, std::u16string numeral) {
    const bool changed = icon != icon_.get() || numeral != numeral_;
    icon_ = icon;
    numeral_ = std::move(numeral);
    if (changed) {
      morph_.Restart();  // Springs from a circle into the new shape.
    }
    SchedulePaint();
  }

  void SetText(std::u16string name, std::u16string supporting) {
    name_ = std::move(name);
    supporting_ = std::move(supporting);
    GetViewAccessibility().SetName(u"Preview: " + name_);
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const Tones tones = Current();
    const gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(tones.container);
    canvas->DrawRoundRect(bounds, kHeroCorner, flags);

    // The mark: the icon's own expressive shape, sprung in from a circle.
    constexpr float kMark = 72.f;
    const gfx::RectF mark(20.f, (bounds.height() - kMark) / 2.f, kMark, kMark);
    const Shape shape = icon_ ? icon_->shape : Shape::kCookie;
    flags.setColor(tones.accent);
    canvas->DrawPath(
        ShapePath(shape, mark, static_cast<float>(morph_.Get())), flags);
    if (icon_) {
      gfx::RectF glyph = mark;
      glyph.Inset(20.f);
      PaintWorkspaceGlyph(canvas, *icon_, glyph, tones.on_accent, 2.25f);
    } else {
      canvas->DrawStringRectWithFlags(
          numeral_, m3::Font(m3::Type::kHeadlineMedium, /*emphasized=*/true),
          tones.on_accent, gfx::ToEnclosingRect(mark),
          gfx::Canvas::TEXT_ALIGN_CENTER);
    }

    // Text column: overline, headline, supporting line.
    const int x = static_cast<int>(mark.right()) + 18;
    const int text_width = std::max(0, width() - x - 20);
    const gfx::FontList over = m3::Font(m3::Type::kLabelMedium, true);
    const gfx::FontList headline = m3::Font(m3::Type::kHeadlineSmall, true);
    const gfx::FontList body = m3::Font(m3::Type::kBodyMedium);
    const int block = over.GetHeight() + 2 + headline.GetHeight() + 4 +
                      body.GetHeight();
    int y = (height() - block) / 2;
    canvas->DrawStringRect(overline_, over,
                           SkColorSetA(tones.on_container, 0xB3),
                           gfx::Rect(x, y, text_width, over.GetHeight()));
    y += over.GetHeight() + 2;
    canvas->DrawStringRect(
        gfx::ElideText(name_, headline, text_width, gfx::ELIDE_TAIL), headline,
        tones.on_container, gfx::Rect(x, y, text_width, headline.GetHeight()));
    y += headline.GetHeight() + 4;
    canvas->DrawStringRect(
        gfx::ElideText(supporting_, body, text_width, gfx::ELIDE_TAIL), body,
        SkColorSetA(tones.on_container, 0xCC),
        gfx::Rect(x, y, text_width, body.GetHeight()));
  }

  void AnimationProgressed(const gfx::Animation* animation) override {
    SchedulePaint();
  }

 private:
  Tones Current() const { return Blend(from_, to_, tint_.Get()); }

  const std::u16string overline_;
  Tones from_{};
  Tones to_{};
  raw_ptr<const WorkspaceIcon> icon_ = nullptr;
  std::u16string numeral_;
  std::u16string name_;
  std::u16string supporting_;
  SpringValue tint_{this, m3::Spring::kSlowEffects};
  SpringValue morph_{this, m3::Spring::kDefaultSpatial};
};

BEGIN_METADATA(HeroPreview)
END_METADATA

// ---------------------------------------------------------------------------
// Pickers
// ---------------------------------------------------------------------------

// A selectable cell whose selection is a SHAPE. An icon: unselected it is
// just the glyph; selected it grows a container that springs from a circle
// into the icon's own shape. A colour: a round two-tone chip that springs into
// the cookie and takes a check when chosen.
// views::Button is already an AnimationDelegateViews; its hover animation
// and ours arrive at the same AnimationProgressed.
class ShapeChoice : public views::Button {
  METADATA_HEADER(ShapeChoice, views::Button)

 public:
  enum class Kind { kIcon, kNumber, kColor };

  ShapeChoice(Kind kind,
              std::u16string label,
              Shape shape,
              int size,
              PressedCallback callback)
      : views::Button(std::move(callback)),
        kind_(kind),
        shape_(shape) {
    SetAnimateOnStateChange(false);
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetPreferredSize(gfx::Size(size, size));
    GetViewAccessibility().SetName(label);
    GetViewAccessibility().SetRole(ax::mojom::Role::kRadioButton);
    SetTooltipText(label);
    views::InstallCircleHighlightPathGenerator(this);
    views::FocusRing::Install(this);
  }

  void set_icon(const WorkspaceIcon* icon) { icon_ = icon; }
  void set_numeral(std::u16string numeral) { numeral_ = std::move(numeral); }
  void set_faces(SkColor top, SkColor bottom) {
    top_ = top;
    bottom_ = bottom;
  }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    GetViewAccessibility().SetCheckedState(
        selected ? ax::mojom::CheckedState::kTrue
                 : ax::mojom::CheckedState::kFalse);
    if (selected) {
      grow_.Restart();
    }
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::RectF bounds(GetLocalBounds());
    const bool hot =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    const SkColor on_surface = m3::Role(*this, kColorZephyrusOnSurface);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    const float grow = static_cast<float>(grow_.Get());

    if (kind_ == Kind::kColor) {
      gfx::RectF chip = bounds;
      chip.Inset(hot && !selected_ ? 3.f : 4.f);
      const SkPath outline =
          ShapePath(Shape::kCookie, chip, selected_ ? grow : 0.f);
      canvas->Save();
      canvas->ClipPath(outline, /*do_anti_alias=*/true);
      flags.setColor(top_);
      canvas->DrawRect(chip, flags);
      flags.setColor(bottom_);
      canvas->DrawRect(gfx::RectF(chip.x(), chip.CenterPoint().y(),
                                  chip.width(), chip.height() / 2.f),
                       flags);
      canvas->Restore();
      if (selected_) {
        PaintCheck(canvas, chip, color_utils::GetColorWithMaxContrast(top_));
      }
      return;
    }

    const SkColor container = m3::Role(*this, kColorZephyrusSecondaryContainer);
    const SkColor on_container =
        m3::Role(*this, kColorZephyrusOnSecondaryContainer);
    if (selected_) {
      flags.setColor(container);
      canvas->DrawPath(ShapePath(shape_, bounds, grow), flags);
    } else if (hot) {
      flags.setColor(m3::StateLayer(on_surface, m3::kHover));
      canvas->DrawCircle(bounds.CenterPoint(), bounds.width() / 2.f, flags);
    }
    const SkColor ink = selected_ ? on_container : on_surface;
    if (kind_ == Kind::kIcon && icon_) {
      gfx::RectF glyph = bounds;
      glyph.Inset(bounds.width() * 0.26f);
      PaintWorkspaceGlyph(canvas, *icon_, glyph, ink, 1.75f);
    } else {
      canvas->DrawStringRectWithFlags(
          numeral_, m3::Font(m3::Type::kTitleMedium, /*emphasized=*/true), ink,
          GetLocalBounds(), gfx::Canvas::TEXT_ALIGN_CENTER);
    }
  }

  void AnimationProgressed(const gfx::Animation* animation) override {
    if (grow_.Owns(animation)) {
      SchedulePaint();
      return;
    }
    views::Button::AnimationProgressed(animation);
  }

 private:
  static void PaintCheck(gfx::Canvas* canvas,
                         const gfx::RectF& box,
                         SkColor color) {
    const gfx::PointF c = box.CenterPoint();
    const float s = box.width() / 24.f;
    SkPathBuilder check;
    check.moveTo(c.x() - 5.f * s, c.y() + 0.5f * s);
    check.lineTo(c.x() - 1.5f * s, c.y() + 4.f * s);
    check.lineTo(c.x() + 5.5f * s, c.y() - 4.f * s);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(2.25f);
    flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
    flags.setStrokeJoin(cc::PaintFlags::kRound_Join);
    flags.setColor(color);
    canvas->DrawPath(check.detach(), flags);
  }

  const Kind kind_;
  const Shape shape_;
  raw_ptr<const WorkspaceIcon> icon_ = nullptr;
  std::u16string numeral_;
  SkColor top_ = SK_ColorTRANSPARENT;
  SkColor bottom_ = SK_ColorTRANSPARENT;
  bool selected_ = false;
  SpringValue grow_{this, m3::Spring::kDefaultSpatial};
};

BEGIN_METADATA(ShapeChoice)
END_METADATA

// One segment of a connected button group. Outer corners are always full;
// the inner corners are small at rest and spring fully round when this
// segment is selected -- the M3 Expressive button group's signature.
// views::Button is already an AnimationDelegateViews; its hover animation
// and ours arrive at the same AnimationProgressed.
class Segment : public views::Button {
  METADATA_HEADER(Segment, views::Button)

 public:
  enum class Glyph { kMoon, kSun };

  Segment(std::u16string label,
          Glyph glyph,
          bool leading,
          PressedCallback callback)
      : views::Button(std::move(callback)),
        label_(std::move(label)),
        glyph_(glyph),
        leading_(leading) {
    SetAnimateOnStateChange(false);
    SetFocusBehavior(FocusBehavior::ALWAYS);
    GetViewAccessibility().SetName(label_);
    GetViewAccessibility().SetRole(ax::mojom::Role::kRadioButton);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(), 24);
    views::FocusRing::Install(this);
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(
        gfx::GetStringWidth(label_, m3::Font(m3::Type::kLabelLarge, true)) +
            kGlyph + kGap + 48,
        48);
  }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    GetViewAccessibility().SetCheckedState(
        selected ? ax::mojom::CheckedState::kTrue
                 : ax::mojom::CheckedState::kFalse);
    round_.Restart();
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::RectF bounds(GetLocalBounds());
    const float full = bounds.height() / 2.f;
    constexpr float kInner = 8.f;
    const float t = static_cast<float>(round_.Get());
    // The inner radius travels between 8 and full. The spatial spring may
    // overshoot; clamped so a corner can never exceed the pill.
    const float inner = std::clamp(selected_ ? kInner + (full - kInner) * t
                                             : full - (full - kInner) * t,
                                   kInner, full);
    const SkScalar l = leading_ ? full : inner;
    const SkScalar r = leading_ ? inner : full;
    // Top-left, top-right, bottom-right, bottom-left.
    const SkVector radii[4] = {{l, l}, {r, r}, {r, r}, {l, l}};
    SkRRect rrect;
    rrect.setRectRadii(gfx::RectFToSkRect(bounds), radii);

    const SkColor fill =
        selected_ ? m3::Role(*this, kColorZephyrusPrimary)
                  : m3::Role(*this, kColorZephyrusSurfaceContainerHighest);
    const SkColor ink = selected_ ? m3::Role(*this, kColorZephyrusOnPrimary)
                                  : m3::Role(*this, kColorZephyrusOnSurface);
    const bool hot =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(hot ? m3::WithStateLayer(fill, ink, m3::kHover) : fill);
    canvas->sk_canvas()->drawRRect(rrect, flags);

    // Glyph + label, centred as one unit.
    const gfx::FontList font = m3::Font(m3::Type::kLabelLarge, true);
    const int text_w = gfx::GetStringWidth(label_, font);
    const int x0 = (width() - (kGlyph + kGap + text_w)) / 2;
    PaintGlyph(canvas,
               gfx::RectF(x0, (height() - kGlyph) / 2.f, kGlyph, kGlyph), ink);
    canvas->DrawStringRect(label_, font, ink,
                           gfx::Rect(x0 + kGlyph + kGap, 0, text_w, height()));
  }

  void AnimationProgressed(const gfx::Animation* animation) override {
    if (round_.Owns(animation)) {
      SchedulePaint();
      return;
    }
    views::Button::AnimationProgressed(animation);
  }

 private:
  static constexpr int kGlyph = 20;
  static constexpr int kGap = 8;

  void PaintGlyph(gfx::Canvas* canvas, const gfx::RectF& box, SkColor color) {
    if (glyph_ == Glyph::kMoon) {
      // The workspace set's own crescent, so the two stay one family.
      if (const WorkspaceIcon* moon =
              FindWorkspaceIcon(std::string_view("focus"))) {
        PaintWorkspaceGlyph(canvas, *moon, box, color, 1.75f);
      }
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.75f);
    flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
    flags.setColor(color);
    const gfx::PointF c = box.CenterPoint();
    canvas->DrawCircle(c, box.width() * 0.2f, flags);
    for (int i = 0; i < 8; ++i) {
      const double a = i * 3.14159265358979 / 4.0;
      const float cs = static_cast<float>(std::cos(a));
      const float sn = static_cast<float>(std::sin(a));
      canvas->DrawLine(gfx::PointF(c.x() + cs * 6.8f, c.y() + sn * 6.8f),
                       gfx::PointF(c.x() + cs * 9.2f, c.y() + sn * 9.2f),
                       flags);
    }
  }

  const std::u16string label_;
  const Glyph glyph_;
  const bool leading_;
  bool selected_ = false;
  SpringValue round_{this, m3::Spring::kFastSpatial};
};

BEGIN_METADATA(Segment)
END_METADATA

// ---------------------------------------------------------------------------
// Containers
// ---------------------------------------------------------------------------

// A tonal card: the container a group of controls sits on.
class Card : public views::View {
  METADATA_HEADER(Card, views::View)

 public:
  explicit Card(const gfx::Insets& padding) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, padding, 0));
  }
  void OnPaintBackground(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(m3::Role(*this, kColorZephyrusSurfaceContainerHigh));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kCardCorner, flags);
  }
};

BEGIN_METADATA(Card)
END_METADATA

class FilledField;

// The editable text inside a FilledField; tells the field to repaint its
// focus outline.
class FieldText : public views::Textfield {
  METADATA_HEADER(FieldText, views::Textfield)

 public:
  explicit FieldText(views::View* owner) : owner_(owner) {}
  void OnFocus() override {
    views::Textfield::OnFocus();
    owner_->SchedulePaint();
  }
  void OnBlur() override {
    views::Textfield::OnBlur();
    owner_->SchedulePaint();
  }

 private:
  raw_ptr<views::View> owner_;
};

BEGIN_METADATA(FieldText)
END_METADATA

// An M3 filled text field, rounded the Expressive way: a soft container, a
// small label inside it, and a 2dp primary outline while focused.
class FilledField : public views::View {
  METADATA_HEADER(FilledField, views::View)

 public:
  explicit FilledField(std::u16string label) : label_(std::move(label)) {
    SetLayoutManager(std::make_unique<views::FillLayout>());
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(24, 16, 8, 16)));
    field_ = AddChildView(std::make_unique<FieldText>(this));
    field_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
    field_->SetBackgroundEnabled(false);
    field_->SetFontList(m3::Font(m3::Type::kBodyLarge));
    field_->GetViewAccessibility().SetName(label_);
    SetPreferredSize(gfx::Size(kCardWidth, 60));
  }

  views::Textfield* field() { return field_; }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const gfx::RectF bounds(GetLocalBounds());
    const bool focused = field_->HasFocus();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(m3::Role(*this, kColorZephyrusSurfaceContainerHighest));
    canvas->DrawRoundRect(bounds, kFieldCorner, flags);
    const SkColor primary = m3::Role(*this, kColorZephyrusPrimary);
    if (focused) {
      gfx::RectF ring = bounds;
      ring.Inset(1.f);
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(2.f);
      flags.setColor(primary);
      canvas->DrawRoundRect(ring, kFieldCorner - 1.f, flags);
    }
    canvas->DrawStringRect(
        label_, m3::Font(m3::Type::kBodySmall),
        focused ? primary : m3::Role(*this, kColorZephyrusOnSurfaceVariant),
        gfx::Rect(16, 8, width() - 32, 16));
  }

 private:
  const std::u16string label_;
  raw_ptr<FieldText> field_ = nullptr;
};

BEGIN_METADATA(FilledField)
END_METADATA

// A list row: headline + supporting text, and a trailing switch. Clicking
// anywhere on the row toggles it, as M3 list items do.
class SwitchRow : public views::Button {
  METADATA_HEADER(SwitchRow, views::Button)

 public:
  SwitchRow(std::u16string headline, std::u16string supporting, bool on)
      : views::Button(base::BindRepeating(&SwitchRow::Toggle,
                                          base::Unretained(this))) {
    SetAnimateOnStateChange(false);
    SetFocusBehavior(FocusBehavior::NEVER);  // The switch takes focus.
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(10, 16),
        16));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* text = AddChildView(std::make_unique<views::View>());
    text->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    headline_ = text->AddChildView(std::make_unique<views::Label>(headline));
    headline_->SetFontList(m3::Font(m3::Type::kBodyLarge));
    headline_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    supporting_ =
        text->AddChildView(std::make_unique<views::Label>(supporting));
    supporting_->SetFontList(m3::Font(m3::Type::kBodyMedium));
    supporting_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    supporting_->SetMultiLine(true);
    supporting_->SetMaximumWidth(270);
    layout->SetFlexForView(text, 1);
    switch_ = AddChildView(std::make_unique<m3::Switch>(base::BindRepeating(
        &SwitchRow::Changed, base::Unretained(this))));
    switch_->SetIsOn(on);
    switch_->GetViewAccessibility().SetName(headline);
  }

  bool is_on() const { return switch_->GetIsOn(); }
  void set_on_change(base::RepeatingClosure on_change) {
    on_change_ = std::move(on_change);
  }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    headline_->SetEnabledColor(m3::Role(*this, kColorZephyrusOnSurface));
    supporting_->SetEnabledColor(
        m3::Role(*this, kColorZephyrusOnSurfaceVariant));
  }

 private:
  // A press on the row body: flip the switch, which then reports it.
  void Toggle() {
    switch_->SetIsOn(!switch_->GetIsOn());
    Changed();
  }
  void Changed() {
    if (on_change_) {
      on_change_.Run();
    }
  }

  raw_ptr<views::Label> headline_ = nullptr;
  raw_ptr<views::Label> supporting_ = nullptr;
  raw_ptr<m3::Switch> switch_ = nullptr;
  base::RepeatingClosure on_change_;
};

BEGIN_METADATA(SwitchRow)
END_METADATA

class SectionLabel : public views::Label {
  METADATA_HEADER(SectionLabel, views::Label)

 public:
  explicit SectionLabel(const std::u16string& text) : views::Label(text) {
    SetFontList(m3::Font(m3::Type::kLabelLarge, /*emphasized=*/true));
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetProperty(views::kMarginsKey, gfx::Insets::TLBR(18, 4, 8, 0));
  }
  void OnThemeChanged() override {
    views::Label::OnThemeChanged();
    SetEnabledColor(m3::Role(*this, kColorZephyrusOnSurfaceVariant));
  }
};

BEGIN_METADATA(SectionLabel)
END_METADATA

}  // namespace

// At global scope, not in a namespace: BubbleDialogDelegateView's constructor
// is private and its friend list names this class as ::ZephyrusWorkspaceSetup.
class ZephyrusWorkspaceSetup : public views::BubbleDialogDelegateView,
                               public views::TextfieldController {
  METADATA_HEADER(ZephyrusWorkspaceSetup, views::BubbleDialogDelegateView)

 public:
  ZephyrusWorkspaceSetup(BrowserView* browser_view,
                         views::View* anchor,
                         int workspace_id)
      : views::BubbleDialogDelegateView(anchor,
                                        views::BubbleBorder::TOP_CENTER),
        browser_view_(browser_view),
        workspace_id_(workspace_id) {
    zephyrus::ConfigureBubble(this);
    // The hero's inset; the body adds its own padding on top (see kHeroInset).
    set_margins(gfx::Insets(kHeroInset));
    set_fixed_width(kCardWidth + 2 * kBodyInset);
    SetBackgroundColor(m3::Role(*anchor, kColorZephyrusSurfaceContainer));
    const bool creating = workspace_id_ == 0;
    // The hero IS the header. The title is kept for the window's accessible
    // name and not drawn a second time.
    SetTitle(creating ? u"New workspace" : u"Edit workspace");
    SetShowTitle(false);
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
               static_cast<int>(ui::mojom::DialogButton::kCancel));
    SetButtonLabel(ui::mojom::DialogButton::kOk,
                   creating ? u"Create" : u"Save");
    SetAcceptCallback(base::BindOnce(&ZephyrusWorkspaceSetup::Commit,
                                     base::Unretained(this)));

    LoadInitialState();

    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));

    // -- Hero --
    hero_ = AddChildView(std::make_unique<HeroPreview>(
        creating ? u"New workspace" : u"Edit workspace"));

    views::View* body = AddChildView(std::make_unique<views::View>());
    body->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(0, kBodyInset - kHeroInset, 0,
                          kBodyInset - kHeroInset),
        0));

    // -- Name --
    name_field_ = body->AddChildView(std::make_unique<FilledField>(u"Name"));
    name_field_->SetProperty(views::kMarginsKey,
                             gfx::Insets::TLBR(16, 0, 0, 0));
    name_field_->field()->SetText(initial_name_);
    name_field_->field()->SetPlaceholderText(u"Workspace " + number_);
    name_field_->field()->set_controller(this);

    // -- Icon --
    body->AddChildView(std::make_unique<SectionLabel>(u"Icon"));
    {
      auto* card = body->AddChildView(std::make_unique<Card>(gfx::Insets(8)));
      constexpr int kCell = 42;
      constexpr int kPerRow = 9;
      views::View* row = nullptr;
      int in_row = kPerRow;
      auto add = [&](std::unique_ptr<ShapeChoice> choice) -> ShapeChoice* {
        if (in_row == kPerRow) {
          row = card->AddChildView(std::make_unique<views::View>());
          row->SetLayoutManager(std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 2));
          in_row = 0;
        }
        ++in_row;
        return row->AddChildView(std::move(choice));
      };
      ShapeChoice* number = add(std::make_unique<ShapeChoice>(
          ShapeChoice::Kind::kNumber, u"Number", Shape::kCookie, kCell,
          base::BindRepeating(&ZephyrusWorkspaceSetup::PickGlyph,
                              base::Unretained(this), std::u16string())));
      number->set_numeral(number_);
      glyph_choices_.emplace_back(std::u16string(), number);
      for (const WorkspaceIcon& icon : AllWorkspaceIcons()) {
        const std::u16string key = base::ASCIIToUTF16(std::string(icon.key));
        ShapeChoice* choice = add(std::make_unique<ShapeChoice>(
            ShapeChoice::Kind::kIcon, std::u16string(icon.label), icon.shape,
            kCell,
            base::BindRepeating(&ZephyrusWorkspaceSetup::PickGlyph,
                                base::Unretained(this), key)));
        choice->set_icon(&icon);
        glyph_choices_.emplace_back(key, choice);
      }
    }

    // -- Colour --
    body->AddChildView(std::make_unique<SectionLabel>(u"Colour"));
    {
      auto* row = body->AddChildView(std::make_unique<views::View>());
      row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
      constexpr int kChip = 40;
      auto add_seed = [&](std::optional<SkColor> seed,
                          const std::u16string& name) {
        auto* chip = row->AddChildView(std::make_unique<ShapeChoice>(
            ShapeChoice::Kind::kColor, name, Shape::kCookie, kChip,
            base::BindRepeating(&ZephyrusWorkspaceSetup::PickSeed,
                                base::Unretained(this), seed)));
        // Two tones: the colour's primary as it will be in light mode and in
        // dark, so the chip shows the range rather than one flat swatch.
        chip->set_faces(TonesFor(seed, /*dark=*/false).accent,
                        TonesFor(seed, /*dark=*/true).accent);
        seed_choices_.emplace_back(seed, chip);
      };
      add_seed(std::nullopt, u"Zephyrus red (default)");
      for (const Seed& seed : kSeeds) {
        add_seed(seed.color, std::u16string(seed.name));
      }
    }

    // -- Light or dark: a connected button group --
    body->AddChildView(std::make_unique<SectionLabel>(u"Appearance"));
    {
      auto* group = body->AddChildView(std::make_unique<views::View>());
      group->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 2));
      dark_segment_ = group->AddChildView(std::make_unique<Segment>(
          u"Dark", Segment::Glyph::kMoon, /*leading=*/true,
          base::BindRepeating(&ZephyrusWorkspaceSetup::PickDark,
                              base::Unretained(this), true)));
      light_segment_ = group->AddChildView(std::make_unique<Segment>(
          u"Light", Segment::Glyph::kSun, /*leading=*/false,
          base::BindRepeating(&ZephyrusWorkspaceSetup::PickDark,
                              base::Unretained(this), false)));
    }

    // -- Options --
    if (creating) {
      auto* card = body->AddChildView(std::make_unique<Card>(gfx::Insets::VH(4, 0)));
      card->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(20, 0, 0, 0));
      separate_ = card->AddChildView(std::make_unique<SwitchRow>(
          u"Separate sign-ins",
          u"Its own cookies and logins. Off shares the first workspace's.",
          /*on=*/true));
      separate_->set_on_change(base::BindRepeating(
          &ZephyrusWorkspaceSetup::Refresh, base::Unretained(this), false));
      wallpaper_next_ = card->AddChildView(std::make_unique<SwitchRow>(
          u"Choose a wallpaper next", u"Opens Customize once it's created.",
          /*on=*/false));
    } else {
      auto* photo = body->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&ZephyrusWorkspaceSetup::ChoosePhoto,
                              base::Unretained(this)),
          has_photo_ ? u"Change photo…" : u"Use a photo instead…"));
      photo->SetStyle(ui::ButtonStyle::kTonal);
      photo->SetCornerRadius(20);
      photo->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(20, 0, 0, 0));
    }

    Refresh(/*animate=*/false);
  }

  views::View* GetInitiallyFocusedView() override {
    return name_field_->field();
  }

  // Styles the dialog's own buttons once they exist. Theirs rather than
  // hand-built ones, because they bring Enter-to-create and Esc-to-cancel.
  void OnWidgetInitialized() override {
    views::BubbleDialogDelegateView::OnWidgetInitialized();
    if (auto* ok = views::AsViewClass<views::MdTextButton>(GetOkButton())) {
      ok->SetStyle(ui::ButtonStyle::kProminent);
      ok->SetCornerRadius(20);
      ok->SetCustomPadding(gfx::Insets::VH(10, 24));
    }
    if (auto* cancel =
            views::AsViewClass<views::MdTextButton>(GetCancelButton())) {
      cancel->SetStyle(ui::ButtonStyle::kText);
      cancel->SetCornerRadius(20);
      cancel->SetCustomPadding(gfx::Insets::VH(10, 16));
    }
  }

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override {
    Refresh(/*animate=*/false);
  }

 private:
  ZephyrusWorkspaceManager* Manager() const {
    return browser_view_ ? browser_view_->zephyrus_workspace_manager()
                         : nullptr;
  }

  void LoadInitialState() {
    ZephyrusWorkspaceManager* manager = Manager();
    const size_t count = manager ? manager->workspaces().size() : 0;
    size_t position = count;
    if (workspace_id_ != 0 && manager) {
      const auto& all = manager->workspaces();
      for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].id == workspace_id_) {
          position = i;
        }
      }
      if (const ZephyrusWorkspace* workspace =
              manager->GetWorkspace(workspace_id_)) {
        initial_name_ = workspace->name;
        glyph_ = FindWorkspaceIcon(workspace->emoji) ? workspace->emoji
                                                     : std::u16string();
        has_photo_ = !workspace->image.empty();
      }
      const ZephyrusWorkspaceLook look =
          manager->GetWorkspaceLook(workspace_id_);
      seed_ = look.seed;
      dark_ = look.dark.value_or(true);
    } else {
      // A colour no neighbour is using, so even a one-click Create is told
      // apart at a glance.
      seed_ = kSeeds[count % kSeeds.size()].color;
      dark_ = manager
                  ? manager->GetWorkspaceLook(manager->current_workspace_id())
                        .dark.value_or(true)
                  : true;
    }
    initial_glyph_ = glyph_;
    initial_seed_ = seed_;
    initial_dark_ = dark_;
    number_ = base::NumberToString16(position + 1);
  }

  void PickGlyph(std::u16string glyph) {
    glyph_ = std::move(glyph);
    glyph_touched_ = true;
    Refresh(/*animate=*/true);
  }
  void PickSeed(std::optional<SkColor> seed) {
    seed_ = seed;
    Refresh(/*animate=*/true);
  }
  void PickDark(bool dark) {
    dark_ = dark;
    Refresh(/*animate=*/true);
  }

  // Pushes the current choices to every control and to the preview.
  void Refresh(bool animate) {
    for (auto& [key, choice] : glyph_choices_) {
      // With a photo set, nothing is selected until the user picks: picking
      // replaces the photo, which is exactly what picking means.
      choice->SetSelected((!has_photo_ || glyph_touched_) && key == glyph_);
    }
    for (auto& [seed, chip] : seed_choices_) {
      chip->SetSelected(seed == seed_);
    }
    dark_segment_->SetSelected(dark_);
    light_segment_->SetSelected(!dark_);

    const std::u16string typed = std::u16string(
        base::TrimWhitespace(name_field_->field()->GetText(), base::TRIM_ALL));
    std::u16string supporting = dark_ ? u"Dark" : u"Light";
    if (workspace_id_ == 0) {
      supporting += (separate_ && !separate_->is_on())
                        ? u" · Shares sign-ins"
                        : u" · Separate sign-ins";
    }
    hero_->SetText(typed.empty() ? u"Workspace " + number_ : typed,
                   supporting);
    hero_->SetMark(FindWorkspaceIcon(glyph_), number_);
    hero_->SetLook(TonesFor(seed_, dark_), animate);
  }

  void Commit() {
    ZephyrusWorkspaceManager* manager = Manager();
    if (!manager) {
      return;
    }
    const std::u16string name = std::u16string(
        base::TrimWhitespace(name_field_->field()->GetText(), base::TRIM_ALL));
    if (workspace_id_ == 0) {
      ZephyrusWorkspaceOptions options;
      options.name = name;
      options.glyph = glyph_;
      options.seed = seed_;
      options.dark = dark_;
      options.separate_sign_ins = !separate_ || separate_->is_on();
      manager->AddWorkspace(options);
      if (wallpaper_next_ && wallpaper_next_->is_on() && browser_view_ &&
          browser_view_->zephyrus_customize_panel()) {
        browser_view_->zephyrus_customize_panel()->Open();
      }
      return;
    }
    if (!manager->GetWorkspace(workspace_id_)) {
      return;  // Deleted from another window while this was open.
    }
    if (name != initial_name_) {
      manager->RenameWorkspace(workspace_id_, name);
    }
    // Picking anything while a photo is set -- the number included -- means
    // "not the photo", so that counts as a change too.
    if (glyph_touched_ && (glyph_ != initial_glyph_ || has_photo_)) {
      manager->SetWorkspaceEmoji(workspace_id_, glyph_);
    }
    if (seed_ != initial_seed_ || dark_ != initial_dark_) {
      manager->SetWorkspaceLook(workspace_id_, seed_, dark_);
    }
  }

  // Closes first, then opens the file dialog: a bubble left open behind a
  // modal file dialog loses activation and can destroy itself mid-callback.
  void ChoosePhoto() {
    ZephyrusWorkspaceManager* manager = Manager();
    if (!manager || !browser_view_ || !browser_view_->GetWidget()) {
      return;
    }
    Profile* profile = browser_view_->browser()->profile();
    gfx::NativeWindow parent = browser_view_->GetWidget()->GetNativeWindow();
    base::WeakPtr<ZephyrusWorkspaceManager> weak = manager->GetWeakPtr();
    const int id = workspace_id_;
    GetWidget()->CloseWithReason(views::Widget::ClosedReason::kUnspecified);
    zephyrus::PickWorkspaceImage(
        profile, id, parent,
        base::BindOnce(
            [](base::WeakPtr<ZephyrusWorkspaceManager> manager, int id,
               const std::string& name) {
              if (manager && !name.empty()) {
                manager->SetWorkspaceImage(id, name);
              }
            },
            weak, id));
  }

  raw_ptr<BrowserView> browser_view_;
  const int workspace_id_;
  std::u16string number_;

  std::u16string initial_name_;
  std::u16string glyph_;
  std::u16string initial_glyph_;
  bool glyph_touched_ = false;
  bool has_photo_ = false;
  std::optional<SkColor> seed_;
  std::optional<SkColor> initial_seed_;
  bool dark_ = true;
  bool initial_dark_ = true;

  raw_ptr<HeroPreview> hero_ = nullptr;
  raw_ptr<FilledField> name_field_ = nullptr;
  raw_ptr<SwitchRow> separate_ = nullptr;
  raw_ptr<SwitchRow> wallpaper_next_ = nullptr;
  raw_ptr<Segment> dark_segment_ = nullptr;
  raw_ptr<Segment> light_segment_ = nullptr;
  std::vector<std::pair<std::u16string, raw_ptr<ShapeChoice>>> glyph_choices_;
  std::vector<std::pair<std::optional<SkColor>, raw_ptr<ShapeChoice>>>
      seed_choices_;
};

BEGIN_METADATA(ZephyrusWorkspaceSetup)
END_METADATA

namespace zephyrus {

void ShowWorkspaceSetup(BrowserView* browser_view,
                        views::View* anchor,
                        int workspace_id) {
  if (!browser_view || !anchor || !anchor->GetWidget() ||
      !browser_view->zephyrus_workspace_manager()) {
    return;
  }
  views::BubbleDialogDelegateView::CreateBubble(
      std::make_unique<ZephyrusWorkspaceSetup>(browser_view, anchor,
                                               workspace_id))
      ->Show();
}

}  // namespace zephyrus
