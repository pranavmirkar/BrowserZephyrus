// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_search_engine_picker.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/search_engines/choice_made_location.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr int kPickerWidth = 240;
constexpr int kRowHeight = 32;
constexpr int kFaviconSize = 16;

// Only one at a time, so a double-click can't stack two.
views::Widget* g_picker_widget = nullptr;

TemplateURLService* GetService(Profile* profile) {
  return profile ? TemplateURLServiceFactory::GetForProfile(profile) : nullptr;
}

}  // namespace

namespace zephyrus {

std::u16string GetDefaultSearchEngineName(Profile* profile) {
  TemplateURLService* service = GetService(profile);
  if (!service) {
    return std::u16string();
  }
  const TemplateURL* def = service->GetDefaultSearchProvider();
  return def ? def->short_name() : std::u16string();
}

}  // namespace zephyrus

ZephyrusSearchEnginePicker::ZephyrusSearchEnginePicker(
    Profile* profile,
    views::View* anchor,
    FinishedCallback on_finished)
    : views::BubbleDialogDelegateView(anchor, views::BubbleBorder::TOP_LEFT),
      profile_(profile),
      on_finished_(std::move(on_finished)) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  set_margins(gfx::Insets(8));
  zephyrus::ConfigureBubble(this);
  // Opaque. A child-widget experiment (params->child = true) that would have
  // let a backdrop blur sample the browser's compositor CRASHED the browser on
  // 2026-08-10 — see zephyrus-menu-blur-impossible. Reverted; without the blur
  // an alpha here only washes the bubble out.
  SetBackgroundColor(zephyrus::Surface());

  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));

  auto* heading =
      AddChildView(std::make_unique<views::Label>(u"Search with"));
  heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  heading->SetEnabledColor(zephyrus::Muted());
  heading->SetFontList(gfx::FontList("Segoe UI, 11px"));
  heading->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(2, 8, 6, 8)));

  TemplateURLService* service = GetService(profile_);
  if (!service) {
    return;
  }
  const TemplateURL* current = service->GetDefaultSearchProvider();

  for (TemplateURL* turl : service->GetTemplateURLs()) {
    // ShowInDefaultList is the same filter Settings uses, so the two lists
    // never disagree about what is selectable as a default.
    if (!service->ShowInDefaultList(turl)) {
      continue;
    }
    const bool selected = current && turl->id() == current->id();

    // Bind by keyword rather than by pointer: setting the default mutates the
    // service, and a raw TemplateURL* captured in a callback can be invalidated
    // before the click is handled.
    auto row = std::make_unique<views::LabelButton>(
        base::BindRepeating(&ZephyrusSearchEnginePicker::Choose,
                            base::Unretained(this), turl->keyword()),
        turl->short_name());
    // Generic glyph up front so the row never renders iconless while the real
    // favicon is being fetched, and so engines with no cached favicon (never
    // visited, or bundled-but-unused) still line up with the others.
    row->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(vector_icons::kSearchIcon,
                                       zephyrus::Muted(),
                                       kFaviconSize));
    row->SetImageLabelSpacing(10);
    row->SetTextColor(views::Button::STATE_NORMAL,
                      selected ? zephyrus::Ink() : zephyrus::Muted());
    row->SetTextColor(views::Button::STATE_HOVERED, SK_ColorWHITE);
    // No font override: LabelButton exposes no public font setter (label() is
    // protected), and the Ctrl+T shortcut chips leave it default too, so this
    // matches them rather than reaching around the API.
    row->SetPreferredSize(gfx::Size(kPickerWidth, kRowHeight));
    row->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 8)));
    if (selected) {
      row->SetBackground(views::CreateRoundedRectBackground(
          zephyrus::Surface(), zephyrus::kRadiusCard));
    }
    views::LabelButton* row_ptr = AddChildView(std::move(row));

    // Real favicon, if we have one cached. This reads the local favicon
    // database only — it never hits the network, so an engine the user has
    // never visited simply keeps the generic glyph.
    if (favicon::FaviconService* favicons =
            FaviconServiceFactory::GetForProfile(
                profile_, ServiceAccessType::EXPLICIT_ACCESS);
        favicons && turl->favicon_url().is_valid()) {
      favicons->GetFaviconImage(
          turl->favicon_url(),
          base::BindOnce(&ZephyrusSearchEnginePicker::OnFaviconReady,
                         weak_factory_.GetWeakPtr(), row_ptr),
          &favicon_tracker_);
    }
  }
}

void ZephyrusSearchEnginePicker::OnFaviconReady(
    views::LabelButton* row,
    const favicon_base::FaviconImageResult& result) {
  if (!row || result.image.IsEmpty()) {
    return;
  }
  gfx::ImageSkia icon = result.image.AsImageSkia();
  if (icon.width() != kFaviconSize || icon.height() != kFaviconSize) {
    icon = gfx::ImageSkiaOperations::CreateResizedImage(
        icon, skia::ImageOperations::RESIZE_BEST,
        gfx::Size(kFaviconSize, kFaviconSize));
  }
  row->SetImageModel(views::Button::STATE_NORMAL,
                     ui::ImageModel::FromImageSkia(icon));
}

ZephyrusSearchEnginePicker::~ZephyrusSearchEnginePicker() {
  g_picker_widget = nullptr;
  // The one place guaranteed to run however the picker went away, so the opener
  // always gets to refresh itself and drop whatever it did to stay alive.
  if (on_finished_) {
    std::move(on_finished_).Run();
  }
}

// static
void ZephyrusSearchEnginePicker::Show(Profile* profile,
                                      views::View* anchor,
                                      FinishedCallback on_finished) {
  // Suppression is checked BEFORE g_picker_widget: when the picker is still up,
  // the guard is what closes it, so short-circuiting on the global here would
  // leave a click on the chip doing nothing at all.
  if (!profile || !anchor || zephyrus::ConsumeReopenSuppression(anchor) ||
      g_picker_widget) {
    // Just dismissed by this very click, already open, or nothing to anchor to:
    // run the callback now, otherwise a caller that loosened its own dismissal
    // to open this would stay loosened.
    if (on_finished) {
      std::move(on_finished).Run();
    }
    return;
  }
  auto picker = std::make_unique<ZephyrusSearchEnginePicker>(
      profile, anchor, std::move(on_finished));
  g_picker_widget =
      views::BubbleDialogDelegateView::CreateBubble(std::move(picker));
  g_picker_widget->Show();
}

void ZephyrusSearchEnginePicker::OnWidgetInitialized() {
  views::BubbleDialogDelegateView::OnWidgetInitialized();
  zephyrus::ApplyBubbleFrame(this);
}

void ZephyrusSearchEnginePicker::Choose(const std::u16string& keyword) {
  TemplateURLService* service = GetService(profile_);
  if (service) {
    // Re-resolve from the keyword: see the binding note above.
    TemplateURL* turl = service->GetTemplateURLForKeyword(keyword);
    if (turl) {
      service->SetUserSelectedDefaultSearchProvider(
          turl, search_engines::ChoiceMadeLocation::kOther);
    }
  }
  // on_finished_ deliberately not run here — the destructor owns that, so the
  // choose path and the dismiss path behave identically.
  if (views::Widget* widget = GetWidget()) {
    widget->Close();
  }
}

BEGIN_METADATA(ZephyrusSearchEnginePicker)
END_METADATA
