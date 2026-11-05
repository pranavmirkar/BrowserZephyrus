// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/zephyrus_omnibox_shield.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"

namespace {

// The count's badge, the one the title-bar shield wore: M3's numeric badge, 12dp
// tall, hung off the glyph's top-right corner.
constexpr int kBadgeHeight = 12;
constexpr int kBadgeFontSize = 8;
constexpr int kBadgePadH = 2;

}  // namespace

ZephyrusOmniboxShield::ZephyrusOmniboxShield(PressedCallback callback)
    : views::Button(std::move(callback)) {
  SetFocusBehavior(FocusBehavior::ALWAYS);
  views::FocusRing::Install(this);
  views::InstallCircleHighlightPathGenerator(this);
  UpdateCount();
}

ZephyrusOmniboxShield::~ZephyrusOmniboxShield() = default;

void ZephyrusOmniboxShield::SetContents(content::WebContents* contents) {
  if (contents_ == contents) {
    return;
  }
  contents_ = contents;
  subscription_ = base::CallbackListSubscription();
  if (contents) {
    if (auto* helper =
            zephyrus_adblock::ZephyrusAdblockTabHelper::FromWebContents(
                contents)) {
      // Repaints are cheap here (a glyph and a number); the helper calls in
      // bursts during a load, which is fine for a view this small.
      subscription_ = helper->AddChangedCallback(base::BindRepeating(
          &ZephyrusOmniboxShield::UpdateCount, base::Unretained(this)));
    }
  }
  UpdateCount();
}

void ZephyrusOmniboxShield::SetInk(SkColor ink) {
  if (ink_ == ink) {
    return;
  }
  ink_ = ink;
  SchedulePaint();
}

void ZephyrusOmniboxShield::OnThemeChanged() {
  views::Button::OnThemeChanged();
  SchedulePaint();
}

void ZephyrusOmniboxShield::UpdateCount() {
  int blocked = 0;
  if (contents_) {
    if (auto* helper =
            zephyrus_adblock::ZephyrusAdblockTabHelper::FromWebContents(
                contents_)) {
      blocked = helper->blocked_this_page();
    }
  }
  // The count is painted, so it is also spoken and shown on hover.
  const std::u16string name =
      blocked == 0 ? u"Shield: site info and blocking"
                   : base::StrCat({u"Shield: ", base::NumberToString16(blocked),
                                   u" blocked on this page. Site info and "
                                   u"blocking"});
  SetTooltipText(name);
  GetViewAccessibility().SetName(name);
  if (blocked != count_) {
    count_ = blocked;
    SchedulePaint();
  }
}

void ZephyrusOmniboxShield::PaintButtonContents(gfx::Canvas* canvas) {
  const int icon = GetLayoutConstant(LayoutConstant::kLocationBarIconSize);
  gfx::Rect box = GetLocalBounds();
  box.ClampToCenteredSize(gfx::Size(icon, icon));
  canvas->Save();
  canvas->Translate(gfx::Vector2d(box.x(), box.y()));
  gfx::PaintVectorIcon(canvas, kZephyrusShieldIcon, icon, ink_);
  canvas->Restore();

  if (count_ <= 0) {
    return;
  }
  const std::u16string text =
      count_ > 99 ? u"99+" : base::NumberToString16(count_);
  const gfx::FontList font({"Segoe UI"}, gfx::Font::NORMAL, kBadgeFontSize,
                           gfx::Font::Weight::MEDIUM);
  const int chip_width =
      std::max(kBadgeHeight, gfx::GetStringWidth(text, font) + 2 * kBadgePadH);
  // Hung off the glyph's corner, then kept inside the view: painting is clipped
  // to the view's bounds.
  const int chip_x = std::clamp(box.right() - chip_width / 2, 0,
                                std::max(0, width() - chip_width));
  const int chip_y = std::clamp(box.y() - kBadgeHeight / 2, 0,
                                std::max(0, height() - kBadgeHeight));
  const gfx::RectF chip(chip_x, chip_y, chip_width, kBadgeHeight);
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setStyle(cc::PaintFlags::kFill_Style);
  // PRIMARY: the blocker doing its job, not an alarm.
  fill.setColor(zephyrus::m3::Role(*this, kColorZephyrusPrimary));
  canvas->DrawRoundRect(chip, kBadgeHeight / 2.f, fill);
  gfx::RectF text_box = chip;
  text_box.Outset(gfx::OutsetsF::VH(kBadgeHeight, 0));
  canvas->DrawStringRectWithFlags(
      text, font, zephyrus::m3::Role(*this, kColorZephyrusOnPrimary),
      gfx::ToEnclosingRect(text_box), gfx::Canvas::TEXT_ALIGN_CENTER);
}

BEGIN_METADATA(ZephyrusOmniboxShield)
END_METADATA
