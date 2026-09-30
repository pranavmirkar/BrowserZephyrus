// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_mascot_bubble.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"

namespace zephyrus::agent {

namespace {

constexpr int kPadding = 14;
constexpr int kVerticalPadding = 12;
constexpr int kTail = 8;
constexpr float kCorner = 16.0f;
constexpr int kTextWidth = ZephyrusMascotBubble::kMaxWidth - 2 * kPadding;
constexpr base::TimeDelta kAnswerShownFor = base::Seconds(16);

// A pill button in the bubble's own style: the first is filled, the second is
// tonal. Styled here rather than borrowed from the toolbar's private classes.
// A subclass only because LabelButton's label() is protected and the M3 type
// scale is a FontList: there is no public way to set it.
class BubbleButton : public views::LabelButton {
  METADATA_HEADER(BubbleButton, views::LabelButton)

 public:
  explicit BubbleButton(PressedCallback callback)
      : views::LabelButton(std::move(callback), std::u16string()) {
    label()->SetFontList(m3::Font(m3::Type::kLabelLarge));
  }
  BubbleButton(const BubbleButton&) = delete;
  BubbleButton& operator=(const BubbleButton&) = delete;
  ~BubbleButton() override = default;
};

BEGIN_METADATA(BubbleButton)
END_METADATA

std::unique_ptr<views::LabelButton> MakeButton(
    views::Button::PressedCallback callback) {
  auto button = std::make_unique<BubbleButton>(std::move(callback));
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(6, 16)));
  button->SetMinSize(gfx::Size(0, 32));
  button->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  button->SetAnimateOnStateChange(false);
  return button;
}

void StyleButton(views::LabelButton* button, bool filled) {
  const SkColor fill = m3::Role(
      *button, filled ? kColorZephyrusPrimary : kColorZephyrusSecondaryContainer);
  const SkColor ink = m3::Role(
      *button,
      filled ? kColorZephyrusOnPrimary : kColorZephyrusOnSecondaryContainer);
  button->SetBackground(views::CreateRoundedRectBackground(fill, 16));
  button->SetEnabledTextColors(ink);
  button->SetTextColor(views::Button::STATE_HOVERED, ink);
  button->SetTextColor(views::Button::STATE_PRESSED, ink);
}

}  // namespace

ZephyrusMascotBubble::ZephyrusMascotBubble() {
  // Clicks reach the page unless there is a question to answer.
  SetCanProcessEventsWithinSubtree(false);
  SetProperty(views::kViewIgnoredByLayoutKey, true);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetVisible(false);
  GetViewAccessibility().SetRole(ax::mojom::Role::kAlert);

  auto* column = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
  column->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStart);

  title_ = AddChildView(std::make_unique<views::Label>());
  title_->SetFontList(m3::Font(m3::Type::kTitleSmall, /*emphasized=*/true));
  title_->SetMultiLine(true);
  title_->SetMaximumWidth(kTextWidth);
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetVisible(false);

  body_ = AddChildView(std::make_unique<views::Label>());
  body_->SetFontList(m3::Font(m3::Type::kBodyMedium));
  body_->SetMultiLine(true);
  body_->SetMaximumWidth(kTextWidth);
  body_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  buttons_ = AddChildView(std::make_unique<views::View>());
  auto* row = buttons_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::TLBR(4, 0, 0, 0),
      8));
  row->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kEnd);
  no_ = buttons_->AddChildView(MakeButton(base::BindRepeating(
      &ZephyrusMascotBubble::Answer, base::Unretained(this), false)));
  yes_ = buttons_->AddChildView(MakeButton(base::BindRepeating(
      &ZephyrusMascotBubble::Answer, base::Unretained(this), true)));
  buttons_->SetVisible(false);

  SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(kVerticalPadding, kPadding, kVerticalPadding + kTail,
                        kPadding)));
}

ZephyrusMascotBubble::~ZephyrusMascotBubble() = default;

void ZephyrusMascotBubble::ShowStatus(const std::u16string& text) {
  // A new step means the last answer is history.
  answer_timer_.Stop();
  answer_text_.clear();
  status_text_ = text;
  Refresh();
}

void ZephyrusMascotBubble::ShowAnswer(const std::u16string& text) {
  status_text_.clear();
  answer_text_ = text;
  answer_timer_.Start(FROM_HERE, kAnswerShownFor,
                      base::BindOnce(&ZephyrusMascotBubble::Clear,
                                     base::Unretained(this)));
  Refresh();
}

void ZephyrusMascotBubble::ShowQuestion(const std::u16string& title,
                                        const std::u16string& reason,
                                        const std::u16string& yes,
                                        const std::u16string& no,
                                        AnswerCallback on_answer) {
  answer_timer_.Stop();
  has_question_ = true;
  on_answer_ = std::move(on_answer);
  title_->SetText(title);
  body_->SetText(reason);
  yes_->SetText(yes);
  no_->SetText(no);
  Refresh();
}

void ZephyrusMascotBubble::ClearQuestion() {
  if (!has_question_) {
    return;
  }
  has_question_ = false;
  on_answer_.Reset();
  Refresh();
}

void ZephyrusMascotBubble::Clear() {
  answer_timer_.Stop();
  status_text_.clear();
  answer_text_.clear();
  has_question_ = false;
  on_answer_.Reset();
  Refresh();
}

void ZephyrusMascotBubble::Answer(bool yes) {
  // Run once: cleared before the callback, which may itself clear or replace.
  AnswerCallback callback = std::move(on_answer_);
  on_answer_.Reset();
  has_question_ = false;
  Refresh();
  if (callback) {
    callback.Run(yes);
  }
}

void ZephyrusMascotBubble::PressForTesting(bool yes) {
  if (has_question_) {
    Answer(yes);
  }
}

void ZephyrusMascotBubble::Refresh() {
  if (has_question_) {
    title_->SetVisible(true);
    buttons_->SetVisible(true);
    body_->SetMaxLines(8);
    body_->SetFontList(m3::Font(m3::Type::kBodyMedium));
    has_content_ = true;
    GetViewAccessibility().SetName(std::u16string(title_->GetText()));
  } else if (!answer_text_.empty()) {
    title_->SetVisible(false);
    buttons_->SetVisible(false);
    body_->SetText(answer_text_);
    body_->SetMaxLines(9);
    body_->SetFontList(m3::Font(m3::Type::kBodyMedium));
    has_content_ = true;
    GetViewAccessibility().SetName(answer_text_);
  } else if (!status_text_.empty()) {
    title_->SetVisible(false);
    buttons_->SetVisible(false);
    body_->SetText(status_text_);
    body_->SetMaxLines(2);
    body_->SetFontList(m3::Font(m3::Type::kBodySmall));
    has_content_ = true;
    GetViewAccessibility().SetName(status_text_);
  } else {
    has_content_ = false;
  }
  SetCanProcessEventsWithinSubtree(has_question_);
  SetVisible(has_content_);
  ApplyColors();
  InvalidateLayout();
  SchedulePaint();
}

gfx::Size ZephyrusMascotBubble::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  gfx::Size size = views::View::CalculatePreferredSize(available_size);
  size.set_width(std::min(size.width(), kMaxWidth));
  return size;
}

void ZephyrusMascotBubble::PlaceNear(const gfx::Rect& mascot,
                                     const gfx::Rect& area) {
  if (!has_content_) {
    return;
  }
  // Above by default. With no room above -- the mascot is up at the address bar
  // -- below it: the tail then hangs off the top.
  gfx::Size size = GetPreferredSize();
  bool on_bottom = mascot.y() - size.height() - 2 >= area.y() + 4;
  if (on_bottom != tail_on_bottom_) {
    tail_on_bottom_ = on_bottom;
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(
        kVerticalPadding + (on_bottom ? 0 : kTail), kPadding,
        kVerticalPadding + (on_bottom ? kTail : 0), kPadding)));
    size = GetPreferredSize();
  }
  const int inset = 8;
  int x = mascot.CenterPoint().x() - size.width() / 2;
  x = std::clamp(x, area.x() + inset,
                 std::max(area.x() + inset, area.right() - inset - size.width()));
  const int y = tail_on_bottom_ ? mascot.y() - size.height() - 2
                                : mascot.bottom() + 2;
  const int tail_x = std::clamp(mascot.CenterPoint().x() - x, 24,
                                std::max(24, size.width() - 24));
  if (tail_x != tail_x_) {
    tail_x_ = tail_x;
    SchedulePaint();
  }
  const gfx::Rect bounds(x, std::max(y, area.y() + 2), size.width(),
                         size.height());
  if (bounds != this->bounds()) {
    SetBoundsRect(bounds);
  }
}

void ZephyrusMascotBubble::OnPaintBackground(gfx::Canvas* canvas) {
  const SkColor fill = m3::Role(*this, kColorZephyrusSurfaceContainerHigh);
  gfx::RectF box(GetLocalBounds());
  if (tail_on_bottom_) {
    box.set_height(box.height() - kTail);
  } else {
    box.Inset(gfx::InsetsF::TLBR(kTail, 0, 0, 0));
  }

  // A soft shadow: two faint copies underneath, cheaper than a real blur and
  // enough to lift it off a white page as well as a dark one.
  cc::PaintFlags shadow;
  shadow.setAntiAlias(true);
  for (int i = 2; i >= 1; --i) {
    shadow.setColor(SkColorSetA(SK_ColorBLACK, 22));
    gfx::RectF r = box;
    r.Offset(0, static_cast<float>(i));
    canvas->DrawRoundRect(r, kCorner, shadow);
  }

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(fill);
  canvas->DrawRoundRect(box, kCorner, flags);

  SkPathBuilder tail;
  const float tx = static_cast<float>(tail_x_);
  if (tail_on_bottom_) {
    tail.moveTo(tx - kTail, box.bottom() - 1);
    tail.lineTo(tx + kTail, box.bottom() - 1);
    tail.lineTo(tx, box.bottom() + kTail);
  } else {
    tail.moveTo(tx - kTail, box.y() + 1);
    tail.lineTo(tx + kTail, box.y() + 1);
    tail.lineTo(tx, box.y() - kTail);
  }
  tail.close();
  canvas->DrawPath(tail.detach(), flags);
}

void ZephyrusMascotBubble::OnThemeChanged() {
  views::View::OnThemeChanged();
  ApplyColors();
}

void ZephyrusMascotBubble::ApplyColors() {
  if (!GetColorProvider()) {
    return;
  }
  StyleButton(yes_, /*filled=*/true);
  StyleButton(no_, /*filled=*/false);
  title_->SetEnabledColor(m3::Role(*this, kColorZephyrusOnSurface));
  body_->SetEnabledColor(m3::Role(
      *this, has_question_ || !answer_text_.empty()
                 ? kColorZephyrusOnSurface
                 : kColorZephyrusOnSurfaceVariant));
}

BEGIN_METADATA(ZephyrusMascotBubble)
END_METADATA

}  // namespace zephyrus::agent
