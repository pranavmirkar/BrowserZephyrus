// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_extension_toggle.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3_switch.h"
#include "components/vector_icons/vector_icons.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/image_loader.h"
#include "extensions/browser/management_policy.h"
#include "extensions/browser/ui_util.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/icons/extension_icon_set.h"
#include "extensions/common/extension_resource.h"
#include "extensions/common/manifest_handlers/icons_handler.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"

namespace zephyrus {

namespace {

// Long enough to watch the switch travel.
constexpr base::TimeDelta kToggleDelay = base::Milliseconds(240);

void ApplyEnabled(base::WeakPtr<Profile> profile,
                  extensions::ExtensionId id,
                  bool enabled) {
  if (!profile || !UserMayToggleExtension(profile.get(), id)) {
    return;
  }
  extensions::ExtensionRegistrar* registrar =
      extensions::ExtensionRegistrar::Get(profile.get());
  if (!registrar) {
    return;
  }
  if (enabled) {
    registrar->EnableExtension(id);
  } else {
    registrar->DisableExtension(
        id, {extensions::disable_reason::DISABLE_USER_ACTION});
  }
}

}  // namespace

bool UserMayToggleExtension(Profile* profile,
                            const extensions::ExtensionId& id) {
  Profile* original = profile->GetOriginalProfile();
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(original)->GetExtensionById(
          id, extensions::ExtensionRegistry::EVERYTHING);
  if (!extension) {
    return false;
  }
  extensions::ManagementPolicy* policy =
      extensions::ExtensionSystem::Get(original)->management_policy();
  return policy && policy->UserMayModifySettings(extension, nullptr) &&
         !policy->MustRemainEnabled(extension, nullptr);
}

void SetExtensionEnabledSoon(Profile* profile,
                             const extensions::ExtensionId& id,
                             bool enabled) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ApplyEnabled,
                     profile->GetOriginalProfile()->GetWeakPtr(), id, enabled),
      kToggleDelay);
}

}  // namespace zephyrus

namespace {

// One extension that is turned off: its icon (dimmed), its name, and a switch
// that turns it back on.
class TurnedOffRow : public views::View {
  METADATA_HEADER(TurnedOffRow, views::View)

 public:
  TurnedOffRow(Profile* profile, const extensions::Extension* extension)
      : profile_(profile), id_(extension->id()) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(2, 0), 12));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    icon_ = AddChildView(std::make_unique<views::ImageView>());
    icon_->SetImageSize(gfx::Size(20, 20));
    icon_->SetPreferredSize(gfx::Size(20, 20));
    icon_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kChromeExtensionIcon, kColorZephyrusOnSurfaceVariant,
        20));
    const extensions::ExtensionResource resource =
        extensions::IconsInfo::GetIconResource(
            extension, extension_misc::EXTENSION_ICON_SMALL,
            ExtensionIconSet::Match::kBigger);
    if (!resource.empty()) {
      extensions::ImageLoader::Get(profile_)->LoadImageAsync(
          extension, resource, gfx::Size(48, 48),
          base::BindOnce(&TurnedOffRow::OnIconLoaded,
                         weak_factory_.GetWeakPtr()));
    }

    auto name = std::make_unique<views::Label>(
        base::UTF8ToUTF16(extension->name()));
    name->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    name->SetElideBehavior(gfx::ELIDE_TAIL);
    name->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kBodyMedium));
    name->SetEnabledColor(kColorZephyrusOnSurfaceVariant);
    name_ = AddChildView(std::move(name));
    layout->SetFlexForView(name_, 1);

    switch_ = AddChildView(std::make_unique<zephyrus::m3::Switch>(
        base::BindRepeating(&TurnedOffRow::OnSwitched,
                            base::Unretained(this))));
    switch_->SetScale(0.7f);
    switch_->SetIsOn(false);
    switch_->SetEnabled(zephyrus::UserMayToggleExtension(profile_, id_));
    const std::u16string label =
        u"Turn on " + base::UTF8ToUTF16(extension->name());
    switch_->GetViewAccessibility().SetName(label);
    switch_->SetTooltipText(u"Turn on");
  }
  TurnedOffRow(const TurnedOffRow&) = delete;
  TurnedOffRow& operator=(const TurnedOffRow&) = delete;
  ~TurnedOffRow() override = default;

  std::u16string sort_key() const { return base::i18n::ToLower(name_->GetText()); }

 private:
  void OnIconLoaded(const gfx::Image& image) {
    if (image.IsEmpty()) {
      return;
    }
    // Dimmed: it is off.
    icon_->SetImage(ui::ImageModel::FromImageSkia(
        gfx::ImageSkiaOperations::CreateTransparentImage(image.AsImageSkia(),
                                                         0.6)));
  }

  void OnSwitched(const ui::Event& event) {
    zephyrus::SetExtensionEnabledSoon(profile_, id_, switch_->GetIsOn());
  }

  raw_ptr<Profile> profile_;
  const extensions::ExtensionId id_;
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> name_ = nullptr;
  raw_ptr<zephyrus::m3::Switch> switch_ = nullptr;
  base::WeakPtrFactory<TurnedOffRow> weak_factory_{this};
};

BEGIN_METADATA(TurnedOffRow)
END_METADATA

}  // namespace

ZephyrusTurnedOffSection::ZephyrusTurnedOffSection(Profile* profile)
    : profile_(profile->GetOriginalProfile()) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  registry_observation_.Observe(extensions::ExtensionRegistry::Get(profile_));
  Rebuild();
}

ZephyrusTurnedOffSection::~ZephyrusTurnedOffSection() = default;

void ZephyrusTurnedOffSection::OnExtensionLoaded(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension) {
  ScheduleRebuild();
}

void ZephyrusTurnedOffSection::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    extensions::UnloadedExtensionReason reason) {
  ScheduleRebuild();
}

void ZephyrusTurnedOffSection::OnExtensionUninstalled(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    extensions::UninstallReason reason) {
  ScheduleRebuild();
}

void ZephyrusTurnedOffSection::ScheduleRebuild() {
  if (rebuild_pending_) {
    return;
  }
  rebuild_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ZephyrusTurnedOffSection::Rebuild,
                                weak_factory_.GetWeakPtr()));
}

void ZephyrusTurnedOffSection::Rebuild() {
  rebuild_pending_ = false;
  RemoveAllChildViews();

  extensions::ExtensionPrefs* prefs = extensions::ExtensionPrefs::Get(profile_);
  std::vector<std::unique_ptr<TurnedOffRow>> rows;
  for (const auto& extension :
       extensions::ExtensionRegistry::Get(profile_)->disabled_extensions()) {
    if (!extension->is_extension() ||
        !extensions::ui_util::ShouldDisplayInExtensionSettings(*extension)) {
      continue;
    }
    // Only what the user turned off. Anything else (blocklisted, a permission
    // waiting to be approved, set by policy) has its own way back.
    const extensions::DisableReasonSet reasons =
        prefs->GetDisableReasons(extension->id());
    if (reasons.size() != 1 ||
        !reasons.contains(extensions::disable_reason::DISABLE_USER_ACTION)) {
      continue;
    }
    rows.push_back(std::make_unique<TurnedOffRow>(profile_, extension.get()));
  }
  std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
    return a->sort_key() < b->sort_key();
  });

  auto resize_bubble = [this]() {
    PreferredSizeChanged();
    // The popup is a bubble: it does not grow or shrink by itself when what it
    // holds does.
    if (views::Widget* widget = GetWidget()) {
      if (views::BubbleDialogDelegate* bubble =
              widget->widget_delegate()->AsBubbleDialogDelegate()) {
        bubble->SizeToContents();
      }
    }
  };
  if (rows.empty()) {
    SetVisible(false);
    resize_bubble();
    return;
  }
  auto header = std::make_unique<views::Label>(u"Turned off");
  header->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  header->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelLarge,
                                         /*emphasized=*/true));
  header->SetEnabledColor(kColorZephyrusOnSurface);
  header->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(12, 0, 4, 0)));
  AddChildView(std::move(header));
  for (auto& row : rows) {
    AddChildView(std::move(row));
  }
  SetVisible(true);
  resize_bubble();
}

BEGIN_METADATA(ZephyrusTurnedOffSection)
END_METADATA
