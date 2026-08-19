// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/zephyrus_engine_pill.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_search_engine_picker.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/vector_icons/vector_icons.h"
#include "components/search_engines/template_url.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/border.h"

namespace {

// Short enough to leave the URL room in a compact pill; long engine names are
// elided rather than allowed to eat the omnibox.
constexpr int kMaxPillWidth = 120;
constexpr int kPillHeight = 22;
constexpr int kFaviconSize = 14;

}  // namespace

ZephyrusEnginePill::ZephyrusEnginePill(Profile* profile)
    : views::LabelButton(base::BindRepeating(&ZephyrusEnginePill::OpenPicker,
                                             base::Unretained(this)),
                         std::u16string()),
      profile_(profile) {
  SetTextColor(views::Button::STATE_NORMAL, SkColorSetRGB(0xC8, 0xC8, 0xD0));
  SetTextColor(views::Button::STATE_HOVERED, SK_ColorWHITE);
  SetBackground(views::CreateRoundedRectBackground(
      SkColorSetARGB(0x1F, 0xFF, 0xFF, 0xFF), kPillHeight / 2));
  SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(2, 9)));
  SetMaxSize(gfx::Size(kMaxPillWidth, kPillHeight));

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

void ZephyrusEnginePill::OpenPicker() {
  ZephyrusSearchEnginePicker::Show(profile_, this, base::DoNothing());
}

void ZephyrusEnginePill::Refresh() {
  const std::u16string name = zephyrus::GetDefaultSearchEngineName(profile_);
  // No engine configured is reachable (enterprise policy, a half-migrated
  // profile), so fall back to a label rather than an empty pill.
  SetText(name.empty() ? u"Search" : name);

  // Generic glyph first so the pill is never iconless, then the engine's own
  // favicon if one is cached locally. Cancel any previous lookup so a stale
  // engine's icon cannot land after a newer one.
  favicon_tracker_.TryCancelAll();
  SetImageModel(views::Button::STATE_NORMAL,
                ui::ImageModel::FromVectorIcon(vector_icons::kSearchIcon,
                                               SkColorSetRGB(0x9A, 0x9A, 0xA5),
                                               kFaviconSize));
  SetImageLabelSpacing(6);

  TemplateURLService* service =
      profile_ ? TemplateURLServiceFactory::GetForProfile(profile_) : nullptr;
  const TemplateURL* def = service ? service->GetDefaultSearchProvider() : nullptr;
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
  SetTooltipText(name.empty()
                     ? u"Choose a search engine"
                     : u"Searching with " + name + u" — click to change");
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
