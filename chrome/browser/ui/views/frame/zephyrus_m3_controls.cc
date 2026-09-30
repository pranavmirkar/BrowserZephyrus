// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_m3_controls.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3_switch.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view_class_properties.h"

namespace zephyrus::m3 {

// ---- Card ------------------------------------------------------------------

Card::Card(const gfx::Insets& padding) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, padding, 0));
}

Card::~Card() = default;

void Card::OnPaintBackground(gfx::Canvas* canvas) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(Role(*this, kColorZephyrusSurfaceContainerHigh));
  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kM3CardCorner, flags);
}

// ---- FieldText ---------------------------------------------------------------

FieldText::FieldText(views::View* owner) : owner_(owner) {}

FieldText::~FieldText() = default;

void FieldText::OnFocus() {
  views::Textfield::OnFocus();
  owner_->SchedulePaint();
}

void FieldText::OnBlur() {
  views::Textfield::OnBlur();
  owner_->SchedulePaint();
}

// ---- FilledField -------------------------------------------------------------

FilledField::FilledField(std::u16string label, int width)
    : label_(std::move(label)) {
  SetLayoutManager(std::make_unique<views::FillLayout>());
  SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(24, 16, 8, 16)));
  field_ = AddChildView(std::make_unique<FieldText>(this));
  field_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  field_->SetBackgroundEnabled(false);
  field_->SetFontList(Font(Type::kBodyLarge));
  field_->GetViewAccessibility().SetName(label_);
  SetPreferredSize(gfx::Size(width, 60));
}

FilledField::~FilledField() = default;

void FilledField::SetInputEnabled(bool enabled) {
  field_->SetEnabled(enabled);
  // Hint text dims with the field, or a disabled "Paste your API key" still
  // looks like an invitation. Disabled is on-surface at 38% under M3.
  field_->SetPlaceholderTextColorId(enabled ? kColorZephyrusOnSurfaceVariant
                                            : kColorZephyrusLegacyFaint);
  SchedulePaint();
}

void FilledField::OnPaintBackground(gfx::Canvas* canvas) {
  const gfx::RectF bounds(GetLocalBounds());
  const bool focused = field_->HasFocus();
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  const bool enabled = field_->GetEnabled();
  // M3 disabled: container at a fraction of its strength, content at 38%.
  flags.setColor(SkColorSetA(Role(*this, kColorZephyrusSurfaceContainerHighest),
                             enabled ? 0xFF : 0x66));
  canvas->DrawRoundRect(bounds, kM3FieldCorner, flags);
  const SkColor primary = Role(*this, kColorZephyrusPrimary);
  if (focused) {
    gfx::RectF ring = bounds;
    ring.Inset(1.f);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(2.f);
    flags.setColor(primary);
    canvas->DrawRoundRect(ring, kM3FieldCorner - 1.f, flags);
  }
  canvas->DrawStringRect(
      label_, Font(Type::kBodySmall),
      !enabled ? SkColorSetA(Role(*this, kColorZephyrusOnSurfaceVariant), 0x61)
      : focused ? primary
                : Role(*this, kColorZephyrusOnSurfaceVariant),
      gfx::Rect(16, 8, width() - 32, 16));
}

// ---- SwitchRow ---------------------------------------------------------------

SwitchRow::SwitchRow(std::u16string headline,
                     std::u16string supporting,
                     bool on)
    : views::Button(
          base::BindRepeating(&SwitchRow::Toggle, base::Unretained(this))) {
  SetAnimateOnStateChange(false);
  SetFocusBehavior(FocusBehavior::NEVER);  // The switch takes focus.
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(10, 16), 16));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* text = AddChildView(std::make_unique<views::View>());
  text->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  headline_ = text->AddChildView(std::make_unique<views::Label>(headline));
  headline_->SetFontList(Font(Type::kBodyLarge));
  headline_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  supporting_ = text->AddChildView(std::make_unique<views::Label>(supporting));
  supporting_->SetFontList(Font(Type::kBodyMedium));
  supporting_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  supporting_->SetMultiLine(true);
  supporting_->SetMaximumWidth(270);
  supporting_->SetVisible(!supporting.empty());
  layout->SetFlexForView(text, 1);
  switch_ = AddChildView(std::make_unique<Switch>(
      base::BindRepeating(&SwitchRow::Changed, base::Unretained(this))));
  switch_->SetIsOn(on);
  switch_->GetViewAccessibility().SetName(headline);
}

SwitchRow::~SwitchRow() = default;

bool SwitchRow::is_on() const {
  return switch_->GetIsOn();
}

void SwitchRow::SetOn(bool on) {
  switch_->SetIsOn(on);
}

void SwitchRow::set_on_change(base::RepeatingClosure on_change) {
  on_change_ = std::move(on_change);
}

void SwitchRow::OnThemeChanged() {
  views::Button::OnThemeChanged();
  headline_->SetEnabledColor(Role(*this, kColorZephyrusOnSurface));
  supporting_->SetEnabledColor(Role(*this, kColorZephyrusOnSurfaceVariant));
}

void SwitchRow::Toggle() {
  switch_->SetIsOn(!switch_->GetIsOn());
  Changed();
}

void SwitchRow::Changed() {
  if (on_change_) {
    on_change_.Run();
  }
}

// ---- SectionLabel ------------------------------------------------------------

SectionLabel::SectionLabel(const std::u16string& text) : views::Label(text) {
  SetFontList(Font(Type::kLabelLarge, /*emphasized=*/true));
  SetHorizontalAlignment(gfx::ALIGN_LEFT);
  SetProperty(views::kMarginsKey, gfx::Insets::TLBR(18, 4, 8, 0));
}

SectionLabel::~SectionLabel() = default;

void SectionLabel::OnThemeChanged() {
  views::Label::OnThemeChanged();
  SetEnabledColor(Role(*this, kColorZephyrusOnSurfaceVariant));
}

BEGIN_METADATA(Card)
END_METADATA

BEGIN_METADATA(FieldText)
END_METADATA

BEGIN_METADATA(FilledField)
END_METADATA

BEGIN_METADATA(SwitchRow)
END_METADATA

BEGIN_METADATA(SectionLabel)
END_METADATA

}  // namespace zephyrus::m3
