// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_search_engine_picker.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "base/i18n/case_conversion.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
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
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/widget/widget.h"
#include "chrome/grit/theme_resources.h"

namespace {

// A compact M3 MENU, not a dialog-sized popup. It was 240dp of 40dp rows in
// labelLarge with a 28dp popup radius -- a sheet's proportions for a five-item
// choice -- and read as oversized next to the field it hangs from.
constexpr int kPickerWidth = 196;
constexpr int kRowHeight = 32;
constexpr int kFaviconSize = 16;
// M3 menus: the menu radius, with items inset from the container and rounded
// so hover and selection read as a shape inside it.
constexpr int kMenuRadius = zephyrus::kRadiusLarge;
constexpr int kItemInset = 4;
// Concentric with the menu's corners (Rule 2): 16 - 4 = 12.
constexpr int kItemRadius =
    zephyrus::m3::ConcentricInner(kMenuRadius, kItemInset);
constexpr int kItemPadding = 10;
constexpr int kCheckSize = 16;

// An engine with no cached favicon gets its initial on a tonal disc rather
// than a magnifier: five identical magnifiers said nothing about which row
// was which.
gfx::ImageSkia Monogram(const std::u16string& name,
                        SkColor disc,
                        SkColor ink) {
  gfx::Canvas canvas(gfx::Size(kFaviconSize, kFaviconSize), 1.0f, false);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(disc);
  canvas.DrawCircle(gfx::PointF(kFaviconSize / 2.f, kFaviconSize / 2.f),
                    kFaviconSize / 2.f, flags);
  const std::u16string letter =
      name.empty() ? u"?" : base::i18n::ToUpper(name.substr(0, 1));
  canvas.DrawStringRectWithFlags(
      letter, zephyrus::m3::Font(zephyrus::m3::Type::kLabelSmall, true), ink,
      gfx::Rect(0, 0, kFaviconSize, kFaviconSize),
      gfx::Canvas::TEXT_ALIGN_CENTER);
  return gfx::ImageSkia::CreateFrom1xBitmap(canvas.GetBitmap());
}

// One engine in the menu: an M3 menu item.
//
// labelLarge; the selected engine on secondaryContainer with a trailing check,
// the rest on the menu's own surface with an onSurface state layer. Colours are
// applied in OnThemeChanged(), which is the first moment a role is readable --
// the constructor runs before the menu has a Widget. (The old rows read the
// global palette table there instead, and hovered to hardcoded WHITE text,
// which vanished on a light theme.)
class EngineMenuItem : public views::LabelButton {
  METADATA_HEADER(EngineMenuItem, views::LabelButton)

 public:
  EngineMenuItem(PressedCallback callback,
                 const std::u16string& text,
                 bool selected)
      : views::LabelButton(std::move(callback), text),
        selected_(selected),
        name_(text) {
    label()->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kBodyMedium));
    SetImageLabelSpacing(kItemPadding);
    SetMinSize(gfx::Size(0, kRowHeight));
    // The trailing padding holds the check, whether or not this row has one,
    // so every label is cut at the same width.
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(
        0, kItemPadding, 0, kItemPadding + kCheckSize + kItemPadding)));
    GetViewAccessibility().SetRole(ax::mojom::Role::kMenuItemRadio);
    GetViewAccessibility().SetCheckedState(
        selected ? ax::mojom::CheckedState::kTrue
                 : ax::mojom::CheckedState::kFalse);
    SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    SetInstallFocusRingOnFocus(true);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kItemRadius);
    views::InkDropHost* const ink = views::InkDrop::Get(this);
    ink->SetMode(views::InkDropHost::InkDropMode::ON);
    ink->SetHighlightOpacity(zephyrus::m3::kHover / 255.0f);
    ink->SetVisibleOpacity(zephyrus::m3::kPressed / 255.0f);
  }
  EngineMenuItem(const EngineMenuItem&) = delete;
  EngineMenuItem& operator=(const EngineMenuItem&) = delete;
  ~EngineMenuItem() override = default;

  // The engine's own favicon, once the favicon database answers.
  void SetFavicon(const gfx::ImageSkia& icon) {
    has_favicon_ = true;
    SetImageModel(views::Button::STATE_NORMAL,
                  ui::ImageModel::FromImageSkia(icon));
  }

  // views::LabelButton:
  void OnThemeChanged() override {
    views::LabelButton::OnThemeChanged();
    const SkColor ink = zephyrus::m3::Role(
        *this, selected_ ? kColorZephyrusOnSecondaryContainer
                         : kColorZephyrusOnSurface);
    SetEnabledTextColors(ink);
    views::InkDrop::Get(this)->SetBaseColor(ink);
    SetBackground(
        selected_
            ? views::CreateRoundedRectBackground(
                  zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer),
                  kItemRadius)
            : nullptr);
    // A generic glyph up front, so the row never renders iconless while the
    // real favicon is fetched, and so engines with no cached favicon still line
    // up with the others.
    if (!has_favicon_) {
      SetImageModel(
          views::Button::STATE_NORMAL,
          ui::ImageModel::FromImageSkia(Monogram(
              name_,
              zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHighest),
              zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant))));
    }
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    views::LabelButton::PaintButtonContents(canvas);
    if (!selected_ || !GetWidget()) {
      return;
    }
    // Trailing check, mirrored by hand for RTL: LabelButton lays its own
    // children out mirrored, but this is painted, not a child.
    const gfx::ImageSkia check = gfx::CreateVectorIcon(
        kCheckIcon, kCheckSize,
        zephyrus::m3::Role(*this, kColorZephyrusOnSecondaryContainer));
    canvas->DrawImageInt(
        check,
        GetMirroredXWithWidthInView(width() - kItemPadding - kCheckSize,
                                    kCheckSize),
        (height() - kCheckSize) / 2);
  }

 private:
  const bool selected_;
  const std::u16string name_;
  bool has_favicon_ = false;
};

BEGIN_METADATA(EngineMenuItem)
END_METADATA

// Only one at a time, so a double-click can't stack two.
views::Widget* g_picker_widget = nullptr;

TemplateURLService* GetService(Profile* profile) {
  return profile ? TemplateURLServiceFactory::GetForProfile(profile) : nullptr;
}

}  // namespace

namespace zephyrus {

gfx::ImageSkia GetBundledEngineIcon(const TemplateURL* engine) {
  if (!engine) {
    return gfx::ImageSkia();
  }
  // Prepopulate ids from third_party/search_engines_data's
  // prepopulated_engines.json. Stable: they key every user's saved default.
  int resource = 0;
  switch (engine->prepopulate_id()) {
    case 1:
      resource = IDR_ZEPHYRUS_ENGINE_GOOGLE;
      break;
    case 2:  // Yahoo and its regional variants (Yahoo! India) share the id.
      resource = IDR_ZEPHYRUS_ENGINE_YAHOO;
      break;
    case 3:
      resource = IDR_ZEPHYRUS_ENGINE_BING;
      break;
    case 92:
      resource = IDR_ZEPHYRUS_ENGINE_DUCKDUCKGO;
      break;
    case 109:
      resource = IDR_ZEPHYRUS_ENGINE_BRAVE;
      break;
    default:
      return gfx::ImageSkia();
  }
  const gfx::ImageSkia* icon =
      ui::ResourceBundle::GetSharedInstance().GetImageSkiaNamed(resource);
  return icon ? *icon : gfx::ImageSkia();
}

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
  // M3 menu: the items inset from every side by the same 4dp, so the first
  // and last items' corners nest in the menu's (Rule 2).
  set_margins(gfx::Insets(kItemInset));
  zephyrus::ConfigureBubble(this);
  // A menu's radius, not a floating sheet's.
  set_corner_radius(kMenuRadius);
  // Opaque, on an M3 menu's surfaceContainer. Read from the ANCHOR, which is in
  // the browser window and has a ColorProvider; this bubble has none until it
  // is shown. (A child-widget experiment that would have let a backdrop blur
  // sample the browser's compositor CRASHED the browser on 2026-08-10 -- see
  // zephyrus-menu-blur-impossible.)
  SetBackgroundColor(
      zephyrus::m3::Role(*anchor, kColorZephyrusSurfaceContainer));

  SetLayoutManager(std::make_unique<views::BoxLayout>(
                       views::BoxLayout::Orientation::kVertical, gfx::Insets(),
                       0))
      ->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);

  // An M3 menu's section label: labelMedium in onSurfaceVariant, on the items'
  // own keyline.
  auto* heading =
      AddChildView(std::make_unique<views::Label>(u"Search with"));
  heading->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
  heading->SetEnabledColor(
      zephyrus::m3::Role(*anchor, kColorZephyrusOnSurfaceVariant));
  heading->SetAutoColorReadabilityEnabled(false);
  heading->SetSubpixelRenderingEnabled(false);
  heading->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelSmall));
  heading->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(6, kItemPadding, 4, kItemPadding)));

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
    auto row = std::make_unique<EngineMenuItem>(
        base::BindRepeating(&ZephyrusSearchEnginePicker::Choose,
                            base::Unretained(this), turl->keyword()),
        turl->short_name(), selected);
    row->SetPreferredSize(gfx::Size(kPickerWidth, kRowHeight));
    views::LabelButton* row_ptr = AddChildView(std::move(row));

    // The bundled mark for a preset engine; nothing further to look up.
    if (const gfx::ImageSkia bundled = zephyrus::GetBundledEngineIcon(turl);
        !bundled.isNull()) {
      static_cast<EngineMenuItem*>(row_ptr)->SetFavicon(bundled);
      continue;
    }

    // Otherwise the real favicon, if one is cached. This reads the local
    // favicon database only -- it never hits the network, so an engine the
    // user has never visited keeps its letter.
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
  // Every row in this menu is an EngineMenuItem; the item has to know it has a
  // real favicon, or its next theme change would put the generic glyph back.
  static_cast<EngineMenuItem*>(row)->SetFavicon(icon);
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
