// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/zephyrus_engine_pill.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_search_engine_picker.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/search_engines/template_url.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/border.h"
#include "ui/views/controls/highlight_path_generator.h"

namespace {

// A 24dp target holding a 16dp mark -- M3 icon-button proportions, and the same
// mark size the omnibox leading icon uses, so the two ends of the field weigh
// the same.
constexpr int kButtonSize = 24;
constexpr int kFaviconSize = 16;

}  // namespace

ZephyrusEnginePill::ZephyrusEnginePill(Profile* profile)
    : views::LabelButton(base::BindRepeating(&ZephyrusEnginePill::OpenPicker,
                                             base::Unretained(this)),
                         std::u16string()),
      profile_(profile) {
  // NO text, no container.
  //
  // This was a named chip at the LEADING edge: favicon, engine name and a tonal
  // fill, up to 120dp of it. That is a lot of furniture for something read once
  // and then never again -- and at the leading edge it pushed the address off
  // centre and had to hide itself entirely below a 260dp field, which is the
  // width where knowing your engine matters most.
  //
  // The mark alone carries it: a search engine IS its logo, the tooltip and the
  // accessible name still say which one, and 24dp fits at any width the field
  // can be.
  SetImageLabelSpacing(0);
  SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  SetImageCentered(true);
  // A round target for the focus ring: the state layer painted below is a
  // circle, and a square highlight would show at its corners.
  views::InstallCircleHighlightPathGenerator(this);
  views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);

  if (TemplateURLService* service =
          profile_ ? TemplateURLServiceFactory::GetForProfile(profile_)
                   : nullptr) {
    observation_.Observe(service);
  }
  Refresh();
}

ZephyrusEnginePill::~ZephyrusEnginePill() = default;

void ZephyrusEnginePill::OnTemplateURLServiceChanged() {
  Refresh();
}

void ZephyrusEnginePill::OnTemplateURLServiceShuttingDown() {
  // Drop the observation here: the service outlives neither the profile nor
  // necessarily this view, and ScopedObservation would otherwise try to
  // unregister from a destroyed service.
  observation_.Reset();
}

void ZephyrusEnginePill::OnThemeChanged() {
  views::LabelButton::OnThemeChanged();
  // The fallback glyph is drawn in a themed role, which cannot be read until
  // this view is in a widget -- so the colour is applied here, not in the
  // constructor, and again on every theme change.
  Refresh();
}

void ZephyrusEnginePill::OnPaintBackground(gfx::Canvas* canvas) {
  const views::Button::ButtonState state = GetState();
  if (state != views::Button::STATE_HOVERED &&
      state != views::Button::STATE_PRESSED) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(zephyrus::m3::StateLayer(
      zephyrus::m3::Role(*this, kColorZephyrusOnSurface),
      state == views::Button::STATE_PRESSED ? zephyrus::m3::kPressed
                                            : zephyrus::m3::kHover));
  canvas->DrawCircle(GetLocalBounds().CenterPoint(),
                     GetLocalBounds().width() / 2.f, flags);
}

void ZephyrusEnginePill::OpenPicker() {
  ZephyrusSearchEnginePicker::Show(profile_, this, base::DoNothing());
}

void ZephyrusEnginePill::Refresh() {
  const std::u16string name = zephyrus::GetDefaultSearchEngineName(profile_);

  // Generic glyph first so the button is never blank, then the engine own
  // favicon if one is cached locally. Cancel any previous lookup so a stale
  // engine icon cannot land after a newer one.
  favicon_tracker_.TryCancelAll();
  SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(
          vector_icons::kSearchIcon,
          GetWidget()
              ? zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant)
              : gfx::kPlaceholderColor,
          kFaviconSize));

  TemplateURLService* service =
      profile_ ? TemplateURLServiceFactory::GetForProfile(profile_) : nullptr;
  const TemplateURL* def =
      service ? service->GetDefaultSearchProvider() : nullptr;
  if (favicon::FaviconService* favicons =
          profile_ ? FaviconServiceFactory::GetForProfile(
                         profile_, ServiceAccessType::EXPLICIT_ACCESS)
                   : nullptr;
      favicons && def && def->favicon_url().is_valid()) {
    favicons->GetFaviconImage(
        def->favicon_url(),
        base::BindOnce(&ZephyrusEnginePill::OnFaviconReady,
                       weak_factory_.GetWeakPtr()),
        &favicon_tracker_);
  }

  // The name lives HERE now, and in the accessible name below -- nothing else
  // on the control says it, so neither may be dropped.
  const std::u16string description =
      name.empty() ? u"Choose a search engine"
                   : u"Searching with " + name;
  SetTooltipText(description);
  GetViewAccessibility().SetName(description);
  PreferredSizeChanged();
}

void ZephyrusEnginePill::OnFaviconReady(
    const favicon_base::FaviconImageResult& result) {
  if (result.image.IsEmpty()) {
    return;
  }
  gfx::ImageSkia icon = result.image.AsImageSkia();
  if (icon.width() != kFaviconSize || icon.height() != kFaviconSize) {
    icon = gfx::ImageSkiaOperations::CreateResizedImage(
        icon, skia::ImageOperations::RESIZE_BEST,
        gfx::Size(kFaviconSize, kFaviconSize));
  }
  SetImageModel(views::Button::STATE_NORMAL,
                ui::ImageModel::FromImageSkia(icon));
  PreferredSizeChanged();
}

BEGIN_METADATA(ZephyrusEnginePill)
END_METADATA
