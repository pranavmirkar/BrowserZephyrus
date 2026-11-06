// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_site_info.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/ui/page_info/chrome_page_info_delegate.h"
#include "chrome/browser/ui/page_info/chrome_page_info_ui_delegate.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3_switch.h"
#include "chrome/browser/ui/views/page_info/page_info_view_factory.h"
#include "chrome/grit/generated_resources.h"
#include "components/content_settings/browser/ui/cookie_controls_util.h"
#include "components/content_settings/core/browser/permission_settings_registry.h"
#include "components/page_info/page_info.h"
#include "components/permissions/object_permission_context_base.h"
#include "components/permissions/permission_util.h"
#include "components/page_info/page_info_ui.h"
#include "components/strings/grit/components_strings.h"
#include "components/strings/grit/privacy_sandbox_strings.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "net/cert/x509_certificate.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"

namespace zephyrus_site {
namespace {

// The rail's list shape: tonal rows 52dp tall, large corners on the outside of a
// list, small where two rows meet, 2dp between them.
constexpr int kRowHeight = 44;
constexpr float kOuterRadius = 16.f;
constexpr float kInnerRadius = 4.f;
constexpr int kRowGap = 2;
constexpr int kIconSize = 20;
constexpr int kTrailSize = 18;
constexpr int kSectionGap = 8;
// The Shield popup's own inset on each side of its column.
constexpr int kColumnInset = 12;

using ActionCallback = base::RepeatingCallback<void(const ui::Event&)>;

// A label that takes its colour from an M3 role once it is in a window.
class ThemedLabel : public views::Label {
  METADATA_HEADER(ThemedLabel, views::Label)

 public:
  ThemedLabel(std::u16string text, ui::ColorId color, zephyrus::m3::Type type)
      : views::Label(std::move(text)), color_(color) {
    SetFontList(zephyrus::m3::Font(type));
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetAutoColorReadabilityEnabled(false);
    SetSubpixelRenderingEnabled(false);
  }
  void OnThemeChanged() override {
    views::Label::OnThemeChanged();
    SetEnabledColor(zephyrus::m3::Role(*this, color_));
  }

 private:
  ui::ColorId color_;
};

BEGIN_METADATA(ThemedLabel)
END_METADATA

// A rounded plate in an M3 container colour.
class TonePlate : public views::View {
  METADATA_HEADER(TonePlate, views::View)

 public:
  TonePlate(ui::ColorId color, float radius) : color_(color), radius_(radius) {}
  void OnPaintBackground(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(zephyrus::m3::Role(*this, color_));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), radius_, flags);
  }

 private:
  const ui::ColorId color_;
  const float radius_;
};

BEGIN_METADATA(TonePlate)
END_METADATA

enum class Trailing { kNone, kChevron, kLaunch, kSwitch, kClose };

// One row of a list: icon, title with optional supporting text, and a trailing
// chevron (opens a sub-page), launch arrow (opens something outside the popup)
// or switch.
class SiteRow : public views::Button {
  METADATA_HEADER(SiteRow, views::Button)

 public:
  SiteRow(ui::ImageModel icon,
          std::u16string title,
          std::u16string subtitle,
          Trailing trailing,
          ActionCallback action)
      : views::Button(base::BindRepeating(&SiteRow::OnPressed,
                                          base::Unretained(this))),
        trailing_(trailing),
        action_(std::move(action)) {
    auto* box = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(0, 14, 0, 10), 12));
    box->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    box->set_minimum_cross_axis_size(kRowHeight);

    auto* icon_view = AddChildView(std::make_unique<views::ImageView>());
    icon_view->SetImageSize(gfx::Size(kIconSize, kIconSize));
    icon_view->SetImage(std::move(icon));
    icon_view->SetCanProcessEventsWithinSubtree(false);

    auto* texts = AddChildView(std::make_unique<views::View>());
    texts->SetCanProcessEventsWithinSubtree(false);
    texts->SetLayoutManager(std::make_unique<views::BoxLayout>(
                                views::BoxLayout::Orientation::kVertical))
        ->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kStretch);
    auto* title_label = texts->AddChildView(std::make_unique<ThemedLabel>(
        title, kColorZephyrusOnSurface, zephyrus::m3::Type::kBodyMedium));
    // Wraps rather than cutting a long name short next to a switch.
    title_label->SetMultiLine(true);
    title_label->SetMaxLines(2);
    auto* subtitle_label = texts->AddChildView(std::make_unique<ThemedLabel>(
        subtitle, kColorZephyrusOnSurfaceVariant,
        zephyrus::m3::Type::kBodySmall));
    subtitle_label->SetMultiLine(true);
    subtitle_label->SetVisible(!subtitle.empty());
    box->SetFlexForView(texts, 1);

    if (trailing_ == Trailing::kSwitch) {
      switch_ = AddChildView(std::make_unique<zephyrus::m3::Switch>(
          base::BindRepeating(&SiteRow::OnSwitched, base::Unretained(this))));
      switch_->SetScale(0.75f);
    } else if (trailing_ != Trailing::kNone) {
      // Only reserves the room: the arrow is painted in PaintButtonContents,
      // turned as it needs to be.
      trail_ = AddChildView(std::make_unique<views::View>());
      trail_->SetPreferredSize(gfx::Size(kTrailSize, kTrailSize));
      trail_->SetCanProcessEventsWithinSubtree(false);
    }

    GetViewAccessibility().SetName(
        subtitle.empty() ? title : title + u", " + subtitle);
    SetFocusBehavior(FocusBehavior::ALWAYS);
    views::FocusRing::Install(this);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kInnerRadius);
  }
  SiteRow(const SiteRow&) = delete;
  SiteRow& operator=(const SiteRow&) = delete;
  ~SiteRow() override = default;

  // Where the row sits in its list: decides which corners are the large ones.
  void SetPosition(bool first, bool last) {
    first_ = first;
    last_ = last;
    SchedulePaint();
  }

  // A row with a switch whose BODY opens something (a permission's page) rather
  // than flipping the switch: the switch is then the only thing that flips it.
  void SetBodyAction(ActionCallback action) { body_action_ = std::move(action); }

  void SetSwitch(bool on,
                 bool enabled,
                 base::RepeatingCallback<void(bool)> changed) {
    if (!switch_) {
      return;
    }
    switch_->SetIsOn(on);
    switch_->SetEnabled(enabled);
    changed_ = std::move(changed);
  }

  void StateChanged(views::Button::ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    SkColor fill =
        zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHighest);
    if (GetState() == STATE_PRESSED || GetState() == STATE_HOVERED) {
      fill = zephyrus::m3::WithStateLayer(
          fill, zephyrus::m3::Role(*this, kColorZephyrusOnSurface),
          GetState() == STATE_PRESSED ? zephyrus::m3::kPressed
                                      : zephyrus::m3::kHover);
    }
    const float top = first_ ? kOuterRadius : kInnerRadius;
    const float bottom = last_ ? kOuterRadius : kInnerRadius;
    const SkVector radii[4] = {{top, top}, {top, top}, {bottom, bottom},
                               {bottom, bottom}};
    SkRRect rrect;
    rrect.setRectRadii(gfx::RectToSkRect(GetLocalBounds()), radii);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(fill);
    canvas->sk_canvas()->drawRRect(rrect, flags);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (!trail_) {
      return;
    }
    const gfx::Rect box = trail_->bounds();
    const SkColor ink =
        zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant);
    if (trailing_ == Trailing::kClose) {
      canvas->Save();
      canvas->Translate(gfx::Vector2d(box.x(), box.y()));
      gfx::PaintVectorIcon(canvas, vector_icons::kCloseIcon, kTrailSize, ink);
      canvas->Restore();
      return;
    }
    if (trailing_ == Trailing::kLaunch) {
      canvas->Save();
      canvas->Translate(gfx::Vector2d(box.x(), box.y()));
      gfx::PaintVectorIcon(canvas, kOpenInNewIcon, kTrailSize, ink);
      canvas->Restore();
      return;
    }
    // The chevron points right: the expand-more chevron, turned.
    canvas->Save();
    cc::PaintCanvas* const paint = canvas->sk_canvas();
    paint->translate(box.CenterPoint().x(), box.CenterPoint().y());
    paint->rotate(-90.f);
    paint->translate(-kTrailSize / 2.0f, -kTrailSize / 2.0f);
    gfx::PaintVectorIcon(canvas, kZephyrusExpandMoreIcon, kTrailSize, ink);
    canvas->Restore();
  }

 private:
  void OnPressed(const ui::Event& event) {
    if (switch_) {
      if (body_action_) {
        body_action_.Run(event);
        return;
      }
      // The whole row is the switch, as in M3 list items.
      if (switch_->GetEnabled()) {
        switch_->SetIsOn(!switch_->GetIsOn());
        OnSwitched(event);
      }
      return;
    }
    if (action_) {
      action_.Run(event);
    }
  }
  void OnSwitched(const ui::Event&) {
    if (changed_) {
      changed_.Run(switch_->GetIsOn());
    }
  }

  const Trailing trailing_;
  ActionCallback action_;
  ActionCallback body_action_;
  base::RepeatingCallback<void(bool)> changed_;
  bool first_ = true;
  bool last_ = true;
  raw_ptr<views::View> trail_ = nullptr;
  raw_ptr<zephyrus::m3::Switch> switch_ = nullptr;
};

BEGIN_METADATA(SiteRow)
END_METADATA

// A column of rows as one list.
std::unique_ptr<views::View> MakeGroup(
    std::vector<std::unique_ptr<SiteRow>> rows) {
  auto group = std::make_unique<views::View>();
  group->SetLayoutManager(std::make_unique<views::BoxLayout>(
                              views::BoxLayout::Orientation::kVertical,
                              gfx::Insets(), kRowGap))
      ->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  const size_t n = rows.size();
  for (size_t i = 0; i < n; ++i) {
    rows[i]->SetPosition(i == 0, i + 1 == n);
    group->AddChildView(std::move(rows[i]));
  }
  return group;
}

// The round back arrow at the head of a sub-page.
class BackButton : public views::Button {
  METADATA_HEADER(BackButton, views::Button)

 public:
  explicit BackButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    SetPreferredSize(gfx::Size(40, 40));
    const std::u16string name = u"Back";
    GetViewAccessibility().SetName(name);
    SetTooltipText(name);
    SetFocusBehavior(FocusBehavior::ALWAYS);
    views::FocusRing::Install(this);
    views::InstallCircleHighlightPathGenerator(this);
  }
  void StateChanged(views::Button::ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    SchedulePaint();
  }
  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (GetState() != STATE_HOVERED && GetState() != STATE_PRESSED) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(zephyrus::m3::StateLayer(
        zephyrus::m3::Role(*this, kColorZephyrusOnSurface),
        GetState() == STATE_PRESSED ? zephyrus::m3::kPressed
                                    : zephyrus::m3::kHover));
    canvas->DrawCircle(gfx::PointF(GetLocalBounds().CenterPoint()),
                       GetLocalBounds().width() / 2.0f, flags);
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const int size = 24;
    canvas->Save();
    canvas->Translate(gfx::Vector2d((width() - size) / 2, (height() - size) / 2));
    gfx::PaintVectorIcon(canvas, kZephyrusBackArrowIcon, size,
                         zephyrus::m3::Role(*this, kColorZephyrusOnSurface));
    canvas->Restore();
  }
};

BEGIN_METADATA(BackButton)
END_METADATA

enum class Page { kNone, kConnection, kCookies, kPermission };

// A device (USB, Bluetooth, serial...) the page has been given access to, kept
// as plain data: the presenter's own objects go out of scope with its call.
struct ChosenData {
  ChosenData(const PageInfo::ChooserUIInfo& ui,
             std::u16string display_name,
             base::DictValue value,
             ui::ImageModel image,
             std::u16string description_text,
             bool by_policy)
      : ui_info(ui),
        name(std::move(display_name)),
        object(std::move(value)),
        icon(std::move(image)),
        description(std::move(description_text)),
        policy(by_policy) {}
  raw_ref<const PageInfo::ChooserUIInfo> ui_info;
  std::u16string name;
  base::DictValue object;
  ui::ImageModel icon;
  std::u16string description;
  bool policy;
};

// The site info: the rows, and the pages they open. It is the page's
// PageInfoUI -- Chromium's presenter hands it the identity, permissions and
// cookie state, and every change the user makes goes back through the
// presenter -- so it says what the page-info bubble would.
class SiteInfo : public views::View, public PageInfoUI {
  METADATA_HEADER(SiteInfo, views::View)

 public:
  SiteInfo(content::WebContents* contents, views::View* column, int width)
      : column_(column), width_(width) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
                         views::BoxLayout::Orientation::kVertical, gfx::Insets(),
                         kSectionGap))
        ->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kStretch);

    const GURL url = contents->GetLastCommittedURL();
    ui_delegate_ =
        std::make_unique<ChromePageInfoUiDelegate>(contents, url);
    presenter_ = std::make_unique<PageInfo>(
        std::make_unique<ChromePageInfoDelegate>(
            contents, ChromePageInfoDelegate::DefaultGetBrowserCallback()),
        contents, url);
    initializing_ = true;
    presenter_->InitializeUiState(this, base::DoNothing());
    initializing_ = false;
    RebuildNow();
  }
  SiteInfo(const SiteInfo&) = delete;
  SiteInfo& operator=(const SiteInfo&) = delete;

  ~SiteInfo() override {
    // The presenter is told once, and it makes the reload prompt itself when a
    // permission was changed.
    bool reload_prompt = false;
    presenter_->OnUIClosing(&reload_prompt);
  }

  // Once this is a child of the column. The column's size is not set here: the
  // popup sets it itself when it has added everything that follows, and an
  // explicit size set now would hide the rows added after this.
  void OnAttached() { attached_ = true; }

  // PageInfoUI:
  void SetIdentityInfo(const IdentityInfo& info) override {
    std::unique_ptr<SecurityDescription> description =
        GetSecurityDescription(info);
    if (!description) {
      return;
    }
    description_ = *description;
    identity_status_ = info.identity_status;
    connection_status_ = info.connection_status;
    safe_browsing_status_ = info.safe_browsing_status;
    certificate_ = info.certificate;
    show_revoke_ = info.show_ssl_decision_revoke_button;
    show_password_buttons_ = info.show_change_password_buttons;
    has_identity_ = true;
    Changed();
  }
  void SetPermissionInfo(const PermissionInfoList& list,
                         ChosenObjectInfoList chosen) override {
    permissions_ = list;
    chosen_.clear();
    for (const std::unique_ptr<ChosenObjectInfo>& object : chosen) {
      // Read now: the object goes out of scope with this call.
      const std::u16string name =
          presenter_->GetChooserContextFromUIInfo(*object->ui_info)
              ->GetObjectDisplayNameForUI(object->chooser_object->value);
      const bool policy = object->chooser_object->source ==
                          content_settings::SettingSource::kPolicy;
      chosen_.emplace_back(
          *object->ui_info, name, object->chooser_object->value.Clone(),
          PageInfoViewFactory::GetChosenObjectIcon(*object, false),
          l10n_util::GetStringUTF16(
              policy ? object->ui_info->allowed_by_policy_description_string_id
                     : object->ui_info->description_string_id),
          policy);
    }
    Changed();
  }
  void SetCookieInfo(const CookiesInfo& info) override {
    allowed_sites_ = info.allowed_sites_count;
    enforcement_ = info.enforcement;
    controls_state_ = info.controls_state;
    rws_ = info.rws_info;
    has_cookies_ = true;
    Changed();
  }

 private:
  void Changed() {
    if (initializing_) {
      return;
    }
    // Never from inside the callback of the row that caused it: the rebuild
    // deletes that row.
    if (rebuild_pending_) {
      return;
    }
    rebuild_pending_ = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&SiteInfo::RebuildNow,
                                  weak_factory_.GetWeakPtr()));
  }

  void RebuildNow() {
    rebuild_pending_ = false;
    RemoveAllChildViews();
    std::vector<std::unique_ptr<SiteRow>> rows;

    if (has_identity_) {
      const bool bad = description_.summary_style ==
                       SecuritySummaryColor::RED;
      rows.push_back(std::make_unique<SiteRow>(
          ui::ImageModel::FromVectorIcon(
              bad ? vector_icons::kWarningFilledIcon : vector_icons::kLockIcon,
              bad ? kColorZephyrusError : kColorZephyrusOnSurfaceVariant,
              kIconSize),
          description_.summary, std::u16string(), Trailing::kChevron,
          base::BindRepeating(&SiteInfo::OpenPage, base::Unretained(this),
                              Page::kConnection)));
    }
    if (has_cookies_) {
      rows.push_back(std::make_unique<SiteRow>(
          ui::ImageModel::FromVectorIcon(vector_icons::kCookieIcon,
                                         kColorZephyrusOnSurfaceVariant,
                                         kIconSize),
          l10n_util::GetStringUTF16(IDS_PAGE_INFO_COOKIES_HEADER),
          std::u16string(), Trailing::kChevron,
          base::BindRepeating(&SiteInfo::OpenPage, base::Unretained(this),
                              Page::kCookies)));
    }
    for (size_t i = 0; i < permissions_.size(); ++i) {
      const PageInfo::PermissionInfo& permission = permissions_[i];
      auto row = std::make_unique<SiteRow>(
          PageInfoViewFactory::GetPermissionIcon(permission),
          PermissionTypeToUIString(permission.type),
          PermissionMainPageStateToUIString(ui_delegate_.get(), permission),
          Trailing::kSwitch, ActionCallback());
      row->SetSwitch(
          IsToggleOn(permission),
          permission.source == content_settings::SettingSource::kUser,
          base::BindRepeating(&SiteInfo::TogglePermission,
                              base::Unretained(this), i));
      // A permission that can be granted once, or remembered, has a page of its
      // own; the row opens it and the switch is the switch.
      if (HasPage(permission)) {
        row->SetBodyAction(base::BindRepeating(&SiteInfo::OpenPermission,
                                               base::Unretained(this),
                                               permission.type));
      }
      rows.push_back(std::move(row));
    }
    // The devices the page may use, each revocable.
    for (size_t i = 0; i < chosen_.size(); ++i) {
      const ChosenData& device = chosen_[i];
      rows.push_back(std::make_unique<SiteRow>(
          device.icon, device.name, device.description,
          device.policy ? Trailing::kNone : Trailing::kClose,
          base::BindRepeating(&SiteInfo::RevokeDevice, base::Unretained(this),
                              i)));
    }
    // Put everything back to what it was, when anything was changed.
    const int resettable = ResettableCount();
    if (resettable > 0) {
      rows.push_back(std::make_unique<SiteRow>(
          ui::ImageModel::FromVectorIcon(vector_icons::kRefreshIcon,
                                         kColorZephyrusOnSurfaceVariant,
                                         kIconSize),
          l10n_util::GetPluralStringFUTF16(IDS_PAGE_INFO_RESET_PERMISSIONS,
                                           resettable),
          std::u16string(), Trailing::kNone,
          base::BindRepeating(
              [](SiteInfo* self, const ui::Event&) {
                self->ResetPermissions();
              },
              base::Unretained(this))));
    }
    rows.push_back(std::make_unique<SiteRow>(
        ui::ImageModel::FromVectorIcon(vector_icons::kSettingsIcon,
                                       kColorZephyrusOnSurfaceVariant,
                                       kIconSize),
        l10n_util::GetStringUTF16(IDS_PAGE_INFO_SITE_SETTINGS_LINK),
        std::u16string(), Trailing::kLaunch,
        base::BindRepeating(
            [](SiteInfo* self, const ui::Event&) {
              self->presenter_->OpenSiteSettingsView();
            },
            base::Unretained(this))));
    AddChildView(MakeGroup(std::move(rows)));

    // A page that is open follows the new data (a toggle on it just changed
    // something).
    if (page_ != Page::kNone) {
      ShowPage(page_);
    }
    Refit();
  }

  static bool HasPage(const PageInfo::PermissionInfo& permission) {
    return permission.is_one_time ||
           permissions::PermissionUtil::DoesSupportTemporaryGrants(
               permission.type);
  }

  // How many things "reset" would put back: the permissions the user changed and
  // the devices they allowed.
  int ResettableCount() const {
    int count = 0;
    for (const PageInfo::PermissionInfo& permission : permissions_) {
      const bool user_managed =
          permission.source == content_settings::SettingSource::kUser &&
          (ui_delegate_->ShouldShowAllow(permission.type) ||
           ui_delegate_->ShouldShowAsk(permission.type));
      if (user_managed && permission.setting) {
        ++count;
      }
    }
    for (const ChosenData& device : chosen_) {
      if (!device.policy) {
        ++count;
      }
    }
    return count;
  }

  void ResetPermissions() {
    for (PageInfo::PermissionInfo permission : permissions_) {
      const bool user_managed =
          permission.source == content_settings::SettingSource::kUser &&
          (ui_delegate_->ShouldShowAllow(permission.type) ||
           ui_delegate_->ShouldShowAsk(permission.type));
      if (!user_managed || !permission.setting) {
        continue;
      }
      permission.setting.reset();
      permission.is_one_time = false;
      permission.is_in_use = false;
      presenter_->OnSitePermissionChanged(permission.type, permission.setting,
                                          permission.requesting_origin,
                                          permission.is_one_time);
    }
    for (const ChosenData& device : chosen_) {
      if (!device.policy) {
        presenter_->OnSiteChosenObjectDeleted(
            *device.ui_info, base::Value(device.object.Clone()));
      }
    }
    presenter_->RecordPageInfoAction(page_info::PAGE_INFO_PERMISSIONS_CLEARED);
  }

  void RevokeDevice(size_t index, const ui::Event&) {
    if (index >= chosen_.size() || chosen_[index].policy) {
      return;
    }
    presenter_->OnSiteChosenObjectDeleted(
        *chosen_[index].ui_info, base::Value(chosen_[index].object.Clone()));
  }

  void TogglePermission(size_t index, bool) {
    if (index >= permissions_.size()) {
      return;
    }
    PageInfo::PermissionInfo permission = permissions_[index];
    ToggleBetweenAllowAndBlock(permission);
    presenter_->OnSitePermissionChanged(permission.type, permission.setting,
                                        permission.requesting_origin,
                                        permission.is_one_time);
  }

  // ---- Pages ---------------------------------------------------------------

  void OpenPage(Page page, const ui::Event&) {
    page_ = page;
    ShowPage(page);
    Refit();
  }

  void OpenPermission(ContentSettingsType type, const ui::Event&) {
    page_ = Page::kPermission;
    permission_type_ = type;
    ShowPage(page_);
    Refit();
  }

  void Back() {
    // Posted: it deletes the button being pressed.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&SiteInfo::LeavePage, weak_factory_.GetWeakPtr()));
  }

  void LeavePage() {
    page_ = Page::kNone;
    if (page_holder_) {
      page_holder_->RemoveAllChildViews();
      page_holder_->SetVisible(false);
    }
    for (views::View* view : hidden_) {
      view->SetVisible(true);
    }
    hidden_.clear();
    Refit();
  }

  void ShowPage(Page page) {
    std::u16string title;
    std::unique_ptr<views::View> body;
    switch (page) {
      case Page::kConnection:
        title = u"Connection";
        body = BuildConnectionPage();
        break;
      case Page::kCookies:
        title = l10n_util::GetStringUTF16(IDS_PAGE_INFO_COOKIES_HEADER);
        body = BuildCookiesPage();
        break;
      case Page::kPermission:
        title = PermissionTypeToUIString(permission_type_);
        body = BuildPermissionPage();
        break;
      case Page::kNone:
        return;
    }
    if (!page_holder_) {
      page_holder_ = column_->AddChildView(std::make_unique<views::View>());
      page_holder_->SetLayoutManager(std::make_unique<views::BoxLayout>(
                                         views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(), kSectionGap))
          ->set_cross_axis_alignment(
              views::BoxLayout::CrossAxisAlignment::kStretch);
    }
    page_holder_->RemoveAllChildViews();

    // The head: the back arrow and the page's name.
    auto* head = page_holder_->AddChildView(std::make_unique<views::View>());
    auto* hl = head->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
    hl->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    head->AddChildView(std::make_unique<BackButton>(
        base::BindRepeating(&SiteInfo::OnBack, base::Unretained(this))));
    auto* name = head->AddChildView(std::make_unique<ThemedLabel>(
        title, kColorZephyrusOnSurface, zephyrus::m3::Type::kTitleMedium));
    name->SetElideBehavior(gfx::ELIDE_TAIL);
    hl->SetFlexForView(name, 1);
    page_holder_->AddChildView(std::move(body));
    page_holder_->SetVisible(true);

    // Everything else in the column gives way to it.
    if (hidden_.empty()) {
      for (views::View* child : column_->children()) {
        if (child != page_holder_ && child->GetVisible()) {
          hidden_.push_back(child);
          child->SetVisible(false);
        }
      }
    }
  }

  void OnBack(const ui::Event&) { Back(); }

  // A paragraph of plain text on the popup's surface.
  std::unique_ptr<views::Label> Paragraph(const std::u16string& text,
                                          ui::ColorId color,
                                          zephyrus::m3::Type type,
                                          int max_width) {
    auto label = std::make_unique<ThemedLabel>(text, color, type);
    label->SetMultiLine(true);
    label->SetMaximumWidth(max_width);
    return label;
  }

  std::unique_ptr<views::View> BuildConnectionPage() {
    const int inner = width_ - 2 * kColumnInset;
    auto body = std::make_unique<views::View>();
    body->SetLayoutManager(std::make_unique<views::BoxLayout>(
                               views::BoxLayout::Orientation::kVertical,
                               gfx::Insets(), kSectionGap))
        ->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kStretch);

    const bool bad = description_.summary_style == SecuritySummaryColor::RED;
    const ui::ColorId on_card = bad ? kColorZephyrusOnErrorContainer
                                    : kColorZephyrusOnSecondaryContainer;
    auto* hero = body->AddChildView(std::make_unique<TonePlate>(
        bad ? kColorZephyrusErrorContainer : kColorZephyrusSecondaryContainer,
        24.f));
    hero->SetLayoutManager(std::make_unique<views::BoxLayout>(
                               views::BoxLayout::Orientation::kVertical,
                               gfx::Insets(16), 8))
        ->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kStretch);
    auto* icon = hero->AddChildView(std::make_unique<views::ImageView>());
    icon->SetImageSize(gfx::Size(28, 28));
    icon->SetImage(ui::ImageModel::FromVectorIcon(
        bad ? vector_icons::kWarningFilledIcon : vector_icons::kLockIcon,
        on_card, 28));
    icon->SetHorizontalAlignment(views::ImageView::Alignment::kLeading);
    hero->AddChildView(Paragraph(description_.summary, on_card,
                                 zephyrus::m3::Type::kTitleMedium, inner - 32));
    if (!description_.details.empty()) {
      hero->AddChildView(Paragraph(description_.details, on_card,
                                   zephyrus::m3::Type::kBodyMedium, inner - 32));
    }

    std::vector<std::unique_ptr<SiteRow>> rows;
    // Where the description is explained.
    rows.push_back(std::make_unique<SiteRow>(
        ui::ImageModel::FromVectorIcon(vector_icons::kInfoIcon,
                                       kColorZephyrusOnSurfaceVariant,
                                       kIconSize),
        l10n_util::GetStringUTF16(IDS_LEARN_MORE), std::u16string(),
        Trailing::kLaunch,
        base::BindRepeating(
            [](SiteInfo* self, const ui::Event& event) {
              switch (self->description_.type) {
                case SecurityDescriptionType::SAFETY_TIP:
                  self->presenter_->OpenSafetyTipHelpCenterPage();
                  break;
                case SecurityDescriptionType::SAFE_BROWSING:
                  self->presenter_->OpenSafeBrowsingHelpCenterPage(&event);
                  break;
                default:
                  self->presenter_->OpenConnectionHelpCenterPage(event);
                  break;
              }
            },
            base::Unretained(this))));
    if (certificate_) {
      const bool valid = identity_status_ != PageInfo::SITE_IDENTITY_STATUS_ERROR;
      std::u16string subtitle;
      if (identity_status_ == PageInfo::SITE_IDENTITY_STATUS_EV_CERT &&
          connection_status_ == PageInfo::SITE_CONNECTION_STATUS_ENCRYPTED &&
          !certificate_->subject().organization_names.empty() &&
          !certificate_->subject().country_name.empty()) {
        subtitle = l10n_util::GetStringFUTF16(
            IDS_PAGE_INFO_SECURITY_TAB_SECURE_IDENTITY_EV_VERIFIED,
            base::UTF8ToUTF16(certificate_->subject().organization_names[0]),
            base::UTF8ToUTF16(certificate_->subject().country_name));
      }
      rows.push_back(std::make_unique<SiteRow>(
          ui::ImageModel::FromVectorIcon(
              valid ? vector_icons::kDomainVerificationIcon
                    : vector_icons::kDomainVerificationOffIcon,
              valid ? kColorZephyrusOnSurfaceVariant : kColorZephyrusError,
              kIconSize),
          l10n_util::GetStringUTF16(valid ? IDS_PAGE_INFO_CERTIFICATE_IS_VALID
                                          : IDS_PAGE_INFO_CERTIFICATE_DETAILS),
          subtitle, Trailing::kLaunch,
          base::BindRepeating(
              [](SiteInfo* self, const ui::Event&) {
                if (self->certificate_) {
                  self->presenter_->OpenCertificateDialog(
                      self->certificate_.get());
                }
              },
              base::Unretained(this))));
    }
    if (show_password_buttons_) {
      // The page asked for a password this browser knows: what to do about it.
      int change_id = IDS_PAGE_INFO_CHANGE_PASSWORD_BUTTON;
      switch (safe_browsing_status_) {
        case PageInfo::SAFE_BROWSING_STATUS_SAVED_PASSWORD_REUSE:
          change_id = IDS_PAGE_INFO_CHECK_PASSWORDS_BUTTON;
          break;
        case PageInfo::SAFE_BROWSING_STATUS_SIGNED_IN_NON_SYNC_PASSWORD_REUSE:
        case PageInfo::SAFE_BROWSING_STATUS_SIGNED_IN_SYNC_PASSWORD_REUSE:
          change_id = IDS_PAGE_INFO_PROTECT_ACCOUNT_BUTTON;
          break;
        default:
          break;
      }
      rows.push_back(std::make_unique<SiteRow>(
          ui::ImageModel::FromVectorIcon(vector_icons::kWarningFilledIcon,
                                         kColorZephyrusError, kIconSize),
          l10n_util::GetStringUTF16(change_id), std::u16string(),
          Trailing::kNone,
          base::BindRepeating(
              [](SiteInfo* self, const ui::Event&) {
                self->presenter_->OnChangePasswordButtonPressed();
              },
              base::Unretained(this))));
      rows.push_back(std::make_unique<SiteRow>(
          ui::ImageModel::FromVectorIcon(vector_icons::kCheckCircleFilledIcon,
                                         kColorZephyrusOnSurfaceVariant,
                                         kIconSize),
          l10n_util::GetStringUTF16(IDS_PAGE_INFO_ALLOWLIST_PASSWORD_REUSE_BUTTON),
          std::u16string(), Trailing::kNone,
          base::BindRepeating(
              [](SiteInfo* self, const ui::Event&) {
                if (self->GetWidget()) {
                  self->GetWidget()->Close();
                }
                self->presenter_->OnAllowlistPasswordReuseButtonPressed();
              },
              base::Unretained(this))));
    }
    if (show_revoke_) {
      rows.push_back(std::make_unique<SiteRow>(
          ui::ImageModel::FromVectorIcon(vector_icons::kWarningFilledIcon,
                                         kColorZephyrusError, kIconSize),
          l10n_util::GetStringUTF16(
              IDS_PAGE_INFO_RESET_INVALID_CERTIFICATE_DECISIONS_BUTTON),
          l10n_util::GetStringUTF16(IDS_PAGE_INFO_INVALID_CERTIFICATE_DESCRIPTION),
          Trailing::kNone,
          base::BindRepeating(
              [](SiteInfo* self, const ui::Event&) {
                self->presenter_->OnRevokeSSLErrorBypassButtonPressed();
                if (self->GetWidget()) {
                  self->GetWidget()->Close();
                }
              },
              base::Unretained(this))));
    }
    body->AddChildView(MakeGroup(std::move(rows)));
    return body;
  }

  // One permission: allow or block it, and whether to keep the decision.
  std::unique_ptr<views::View> BuildPermissionPage() {
    const int inner = width_ - 2 * kColumnInset;
    auto body = std::make_unique<views::View>();
    body->SetLayoutManager(std::make_unique<views::BoxLayout>(
                               views::BoxLayout::Orientation::kVertical,
                               gfx::Insets(), kSectionGap))
        ->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kStretch);
    size_t index = permissions_.size();
    for (size_t i = 0; i < permissions_.size(); ++i) {
      if (permissions_[i].type == permission_type_) {
        index = i;
      }
    }
    if (index == permissions_.size()) {
      // The permission is gone (reset from elsewhere): nothing to show.
      return body;
    }
    const PageInfo::PermissionInfo& permission = permissions_[index];

    std::vector<std::unique_ptr<SiteRow>> rows;
    auto allow = std::make_unique<SiteRow>(
        PageInfoViewFactory::GetPermissionIcon(permission),
        PermissionTypeToUIString(permission.type),
        PermissionMainPageStateToUIString(ui_delegate_.get(), permission),
        Trailing::kSwitch, ActionCallback());
    allow->SetSwitch(
        IsToggleOn(permission),
        permission.source == content_settings::SettingSource::kUser,
        base::BindRepeating(&SiteInfo::TogglePermission, base::Unretained(this),
                            index));
    rows.push_back(std::move(allow));

    // "Remember this setting": only for a permission that can be granted once,
    // and not while it is blocked.
    const auto* registry =
        content_settings::PermissionSettingsRegistry::GetInstance()->Get(
            permission.type);
    const bool can_remember =
        permissions::PermissionUtil::IsPermission(permission.type) &&
        permissions::PermissionUtil::DoesSupportTemporaryGrants(
            permission.type) &&
        permission.setting && registry &&
        !registry->delegate().IsBlocked(*permission.setting);
    if (can_remember) {
      auto remember = std::make_unique<SiteRow>(
          ui::ImageModel::FromVectorIcon(vector_icons::kCheckCircleFilledIcon,
                                         kColorZephyrusOnSurfaceVariant,
                                         kIconSize),
          l10n_util::GetStringUTF16(
              IDS_PAGE_INFO_PERMISSIONS_SUBPAGE_REMEMBER_THIS_SETTING),
          std::u16string(), Trailing::kSwitch, ActionCallback());
      remember->SetSwitch(
          !permission.is_one_time && permission.setting.has_value(), true,
          base::BindRepeating(&SiteInfo::RememberPermission,
                              base::Unretained(this), index));
      rows.push_back(std::move(remember));
    }
    body->AddChildView(MakeGroup(std::move(rows)));

    const std::u16string detail =
        ui_delegate_->GetPermissionDetail(permission.type);
    if (!detail.empty()) {
      body->AddChildView(Paragraph(detail, kColorZephyrusOnSurfaceVariant,
                                   zephyrus::m3::Type::kBodySmall, inner));
    }
    return body;
  }

  void RememberPermission(size_t index, bool) {
    if (index >= permissions_.size()) {
      return;
    }
    PageInfo::PermissionInfo permission = permissions_[index];
    ToggleBetweenRememberAndForget(permission);
    presenter_->OnSitePermissionChanged(permission.type, permission.setting,
                                        permission.requesting_origin,
                                        permission.is_one_time);
  }

  std::unique_ptr<views::View> BuildCookiesPage() {
    const int inner = width_ - 2 * kColumnInset;
    auto body = std::make_unique<views::View>();
    body->SetLayoutManager(std::make_unique<views::BoxLayout>(
                               views::BoxLayout::Orientation::kVertical,
                               gfx::Insets(), kSectionGap))
        ->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kStretch);

    // What cookies are, with the settings one step away.
    body->AddChildView(Paragraph(
        l10n_util::GetStringFUTF16(
            IDS_PAGE_INFO_COOKIES_DESCRIPTION,
            l10n_util::GetStringUTF16(IDS_PAGE_INFO_COOKIES_SETTINGS_LINK)),
        kColorZephyrusOnSurfaceVariant, zephyrus::m3::Type::kBodyMedium,
        inner));

    // Third-party cookies: the one control that is the page's to change.
    if (controls_state_ != CookieControlsState::kHidden) {
      std::vector<std::unique_ptr<SiteRow>> rows;
      const bool blocked = controls_state_ == CookieControlsState::kBlocked3pc;
      const std::u16string subtitle = l10n_util::GetStringUTF16(
          blocked ? IDS_TRACKING_PROTECTION_BUBBLE_3PC_BLOCKED_SUBTITLE
                  : IDS_TRACKING_PROTECTION_BUBBLE_3PC_ALLOWED_SUBTITLE);
      const bool enforced =
          enforcement_ != CookieControlsEnforcement::kNoEnforcement;
      auto row = std::make_unique<SiteRow>(
          ui::ImageModel::FromVectorIcon(vector_icons::kCookieIcon,
                                         kColorZephyrusOnSurfaceVariant,
                                         kIconSize),
          l10n_util::GetStringUTF16(
              IDS_PAGE_INFO_COOKIES_THIRD_PARTY_COOKIES_LABEL),
          enforced ? content_settings::CookieControlsUtil::GetEnforcedTooltip(
                         enforcement_)
                   : subtitle,
          enforced ? Trailing::kNone : Trailing::kSwitch, ActionCallback());
      row->SetSwitch(
          !blocked, !enforced,
          base::BindRepeating(
              [](SiteInfo* self, bool on) {
                // The switch reads "allowed": blocking is its opposite.
                self->presenter_->OnThirdPartyToggleClicked(
                    /*block_third_party_cookies=*/!on);
              },
              base::Unretained(this)));
      rows.push_back(std::move(row));
      body->AddChildView(MakeGroup(std::move(rows)));
      if (blocked && !enforced) {
        // The way back for a page the blocking broke.
        body->AddChildView(Paragraph(
            l10n_util::GetStringUTF16(
                IDS_PAGE_INFO_COOKIES_SITE_NOT_WORKING_TITLE) +
                u" " +
                l10n_util::GetStringUTF16(
                    IDS_PAGE_INFO_COOKIES_SITE_NOT_WORKING_DESCRIPTION_PERMANENT),
            kColorZephyrusOnSurfaceVariant, zephyrus::m3::Type::kBodySmall,
            inner));
      }
    }

    std::vector<std::unique_ptr<SiteRow>> rows;
    rows.push_back(std::make_unique<SiteRow>(
        ui::ImageModel::FromVectorIcon(vector_icons::kCookieIcon,
                                       kColorZephyrusOnSurfaceVariant,
                                       kIconSize),
        l10n_util::GetStringUTF16(IDS_PAGE_INFO_COOKIES_DIALOG_BUTTON_TITLE),
        allowed_sites_ >= 0
            ? l10n_util::GetPluralStringFUTF16(
                  IDS_PAGE_INFO_COOKIES_ALLOWED_SITES_COUNT, allowed_sites_)
            : std::u16string(),
        Trailing::kLaunch,
        base::BindRepeating(
            [](SiteInfo* self, const ui::Event&) {
              self->presenter_->OpenCookiesDialog();
            },
            base::Unretained(this))));
    if (rws_) {
      rows.push_back(std::make_unique<SiteRow>(
          ui::ImageModel::FromVectorIcon(vector_icons::kTenancyFilledIcon,
                                         kColorZephyrusOnSurfaceVariant,
                                         kIconSize),
          l10n_util::GetStringUTF16(IDS_PAGE_INFO_RWS_BUTTON_TITLE),
          l10n_util::GetStringFUTF16(IDS_PAGE_INFO_RWS_BUTTON_SUBTITLE,
                                     rws_->owner_name),
          Trailing::kLaunch,
          base::BindRepeating(
              [](SiteInfo* self, const ui::Event&) {
                self->presenter_->OpenAllSitesViewFilteredToRws();
              },
              base::Unretained(this))));
    }
    rows.push_back(std::make_unique<SiteRow>(
        ui::ImageModel::FromVectorIcon(vector_icons::kSettingsIcon,
                                       kColorZephyrusOnSurfaceVariant,
                                       kIconSize),
        u"Cookie settings",
        std::u16string(), Trailing::kLaunch,
        base::BindRepeating(
            [](SiteInfo* self, const ui::Event&) {
              self->presenter_->OpenCookiesSettingsView();
            },
            base::Unretained(this))));
    body->AddChildView(MakeGroup(std::move(rows)));
    return body;
  }

  // ---- Size ----------------------------------------------------------------

  // The Shield popup's column has a fixed size set from what it holds; this
  // sets it again when what it holds changes, and the bubble follows.
  void Refit() {
    if (!attached_ && !parent()) {
      return;
    }
    attached_ = true;
    // An explicit preferred size answers GetHeightForWidth by itself, so it is
    // cleared to measure what the column holds now.
    column_->SetPreferredSize(std::nullopt);
    const int height = column_->GetHeightForWidth(width_);
    column_->SetPreferredSize(gfx::Size(width_, height));
    if (views::Widget* widget = column_->GetWidget()) {
      if (views::BubbleDialogDelegate* bubble =
              widget->widget_delegate()
                  ? widget->widget_delegate()->AsBubbleDialogDelegate()
                  : nullptr) {
        bubble->SizeToContents();
      }
    }
  }

  const raw_ptr<views::View> column_;
  const int width_;
  std::unique_ptr<ChromePageInfoUiDelegate> ui_delegate_;
  std::unique_ptr<PageInfo> presenter_;
  bool initializing_ = false;
  bool rebuild_pending_ = false;
  bool attached_ = false;

  // What the presenter last said.
  bool has_identity_ = false;
  SecurityDescription description_;
  PageInfo::SiteIdentityStatus identity_status_ =
      PageInfo::SITE_IDENTITY_STATUS_UNKNOWN;
  PageInfo::SiteConnectionStatus connection_status_ =
      PageInfo::SITE_CONNECTION_STATUS_UNKNOWN;
  PageInfo::SafeBrowsingStatus safe_browsing_status_ =
      PageInfo::SAFE_BROWSING_STATUS_NONE;
  scoped_refptr<net::X509Certificate> certificate_;
  bool show_revoke_ = false;
  PermissionInfoList permissions_;
  std::vector<ChosenData> chosen_;
  bool show_password_buttons_ = false;
  bool has_cookies_ = false;
  int allowed_sites_ = -1;
  CookieControlsEnforcement enforcement_ =
      CookieControlsEnforcement::kNoEnforcement;
  CookieControlsState controls_state_ = CookieControlsState::kHidden;
  std::optional<CookiesRwsInfo> rws_;

  Page page_ = Page::kNone;
  ContentSettingsType permission_type_ = ContentSettingsType::DEFAULT;
  raw_ptr<views::View> page_holder_ = nullptr;
  std::vector<raw_ptr<views::View>> hidden_;

  base::WeakPtrFactory<SiteInfo> weak_factory_{this};
};

BEGIN_METADATA(SiteInfo)
END_METADATA

}  // namespace

void AttachSiteInfo(views::View* content,
                    content::WebContents* contents,
                    int width) {
  if (!content || !contents) {
    return;
  }
  const GURL url = contents->GetLastCommittedURL();
  if (!url.SchemeIsHTTPOrHTTPS() && !url.SchemeIsFile()) {
    return;
  }
  auto* info =
      content->AddChildView(std::make_unique<SiteInfo>(contents, content, width));
  info->OnAttached();
}

}  // namespace zephyrus_site
