// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_settings_popup.h"

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "build/build_config.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ref.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "cc/paint/paint_flags.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/base/page_transition_types.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/geometry/transform_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

// The single active popup (at most one across all windows).
ZephyrusSettingsPopup* g_active_popup = nullptr;

// 20, not 22: this radius reaches the WebView's layer via
// holder()->SetCornerRadii(), and layer-rounded corners must be a whole number
// of device pixels or the curve renders blurry at fractional display scaling
// (22 x 1.25 = 27.5). 20 also matches the web contents viewer.
// Holds things -> card radius. Was 20.
constexpr int kPopupCornerRadius = zephyrus::kRadiusCard;
constexpr int kRailWidth = 208;
constexpr int kRowHeight = 34;
constexpr int kRowRadius = zephyrus::kCornerRadius;
constexpr int kRowIconSize = 16;

// Derived from the permanent theme rather than the neutral greys this used to
// carry (#141418 / #1B1B1F), which belonged to no theme and read as a
// different product next to the title bar. Still deliberately quiet: a rail at
// the base theme color, monochrome icons, and one small accent detail on the
// selected row — no colored chips, no filled accent slabs.
//
// The rail IS the theme color, so the popup's edge continues the title bar.
// All of these were constexpr against the single permanent theme. They cannot
// be: the palette is resolved from the OS at paint time now, so they are
// functions that read it. Same names, same roles, one pair of parentheses.
SkColor RailColor() {
  return zephyrus::Ground();
}
SkColor RailHairline() {
  return zephyrus::Rule();
}
SkColor Foreground() {
  return zephyrus::Ink();
}
SkColor MutedForeground() {
  return zephyrus::Muted();
}
SkColor SelectedRowFill() {
  return SkColorSetA(zephyrus::Ink(), 0x1F);
}
SkColor HoverRowFill() {
  return SkColorSetA(zephyrus::Ink(), 0x0F);
}
// The content half, one surface step off the rail so the popup still reads as
// two halves in either theme.
SkColor WebViewBase() {
  return zephyrus::Surface();
}

// A rail entry: monochrome icon + label. Selection is a soft neutral
// rounded fill with a small accent notch on the left edge; hover is an even
// softer fill.
class ZephyrusNavPill : public views::LabelButton {
  METADATA_HEADER(ZephyrusNavPill, views::LabelButton)

 public:
  ZephyrusNavPill(PressedCallback callback,
                  const std::u16string& label,
                  const gfx::VectorIcon& icon)
      : views::LabelButton(std::move(callback), label), icon_(icon) {
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetImageLabelSpacing(10);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(7, 12)));
    SetMinSize(gfx::Size(kRailWidth - 24, kRowHeight));
    GetViewAccessibility().SetName(label);
    UpdateColors();
  }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    UpdateColors();
    SchedulePaint();
  }

  // views::LabelButton:
  void StateChanged(views::Button::ButtonState old_state) override {
    views::LabelButton::StateChanged(old_state);
    UpdateColors();
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const bool hovered = GetState() == views::Button::STATE_HOVERED ||
                         GetState() == views::Button::STATE_PRESSED;
    if (!selected_ && !hovered) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(selected_ ? SelectedRowFill() : HoverRowFill());
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kRowRadius, flags);
    if (selected_) {
      // Small accent notch, vertically centered on the left edge.
      constexpr float kNotchWidth = 3.0f;
      constexpr float kNotchHeight = 14.0f;
      flags.setColor(zephyrus::Accent());
      const float y = (height() - kNotchHeight) / 2.0f;
      canvas->DrawRoundRect(gfx::RectF(0, y, kNotchWidth, kNotchHeight),
                            kNotchWidth / 2.0f, flags);
    }
  }

 private:
  void UpdateColors() {
    const bool hovered = GetState() == views::Button::STATE_HOVERED ||
                         GetState() == views::Button::STATE_PRESSED;
    const SkColor fg =
        (selected_ || hovered) ? Foreground() : MutedForeground();
    SetTextColor(views::Button::STATE_NORMAL, fg);
    SetTextColor(views::Button::STATE_HOVERED, Foreground());
    SetTextColor(views::Button::STATE_PRESSED, Foreground());
    SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(*icon_, fg, kRowIconSize));
  }

  const raw_ref<const gfx::VectorIcon> icon_;
  bool selected_ = false;
};

BEGIN_METADATA(ZephyrusNavPill)
END_METADATA

GURL SectionURL(ZephyrusSettingsPopup::Section section) {
  using Section = ZephyrusSettingsPopup::Section;
  switch (section) {
    case Section::kSettings:
      return GURL("chrome://settings");
    case Section::kShield:
      return GURL("chrome://settings/adBlocker");
    case Section::kExtensions:
      return GURL("chrome://extensions");
    case Section::kDownloads:
      return GURL("chrome://downloads");
    case Section::kHistory:
      return GURL("chrome://history");
    case Section::kBookmarks:
      return GURL("chrome://bookmarks");
    case Section::kPasswords:
      return GURL("chrome://password-manager");
    case Section::kAbout:
      return GURL("chrome://settings/help");
  }
}

}  // namespace

// static
bool ZephyrusSettingsPopup::MaybeShow(Browser* browser, Section section) {
  if (!browser || !browser->is_type_normal() ||
      browser->profile()->IsOffTheRecord()) {
    return false;
  }
  Show(browser, section);
  return true;
}

// static
void ZephyrusSettingsPopup::Show(Browser* browser, Section section) {
  // Reuse the open popup only when its widget is demonstrably alive; a
  // closing/closed widget means the pointer is about to go stale.
  if (g_active_popup && g_active_popup->browser_ == browser &&
      g_active_popup->widget_ && !g_active_popup->widget_->IsClosed()) {
    g_active_popup->SwitchTo(section);
    g_active_popup->widget_->Activate();
    if (g_active_popup->web_view_) {
      g_active_popup->web_view_->RequestFocus();
    }
    return;
  }
  if (g_active_popup) {
    // Stale, closing, or belongs to another window: shut it down and start
    // fresh. Its WindowClosing() only clears g_active_popup when it still
    // points at itself, so overwriting it below is safe.
    g_active_popup->ClosePopup();
    g_active_popup = nullptr;
  }

  auto popup = base::WrapUnique(new ZephyrusSettingsPopup(browser, section));
  ZephyrusSettingsPopup* popup_ptr = popup.get();
  views::WebView* web_view = popup->web_view_;
  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      std::move(popup), browser->window()->GetNativeWindow());
  popup_ptr->widget_ = widget;
  // No DWM corner preference here any more: the compositor rounds the frame at
  // the designed 20px again (see set_use_round_corners in the constructor), and
  // asking DWM to also round the window at its ~8px system radius would clip the
  // corners twice and shave them.
  widget->Show();
  // Center over the browser window; the default browser-modal placement pins
  // the dialog to the top edge, which looks misaligned in the Zephyrus
  // frameless UI.
  const gfx::Rect browser_bounds = browser->window()->GetBounds();
  gfx::Rect bounds = widget->GetWindowBoundsInScreen();
  bounds.set_origin(gfx::Point(
      browser_bounds.x() + (browser_bounds.width() - bounds.width()) / 2,
      browser_bounds.y() + (browser_bounds.height() - bounds.height()) / 2));
  widget->SetBounds(bounds);
  // Springy entrance: fade in while the card scales up from 96%, overshoots
  // to ~101.5%, and settles — an under-damped spring about the center (modals
  // are not anchored to a trigger, so a centered origin is correct). Runs on
  // the compositor (opacity/transform only) and is skipped when the OS asks
  // for reduced motion.
  if (ui::Layer* layer = widget->GetLayer();
      layer && gfx::Animation::ShouldRenderRichAnimation()) {
    const gfx::Transform settled = layer->transform();
    const gfx::Point center = gfx::Rect(layer->bounds().size()).CenterPoint();
    layer->SetOpacity(0.0f);
    layer->SetTransform(gfx::GetScaleTransform(center, 0.96f));
    ui::LayerAnimator* animator = layer->GetAnimator();
    auto fade = ui::LayerAnimationElement::CreateOpacityElement(
        1.0f, base::Milliseconds(150));
    fade->set_tween_type(gfx::Tween::EASE_OUT);
    animator->StartAnimation(new ui::LayerAnimationSequence(std::move(fade)));
    auto grow = ui::LayerAnimationElement::CreateTransformElement(
        gfx::GetScaleTransform(center, 1.015f), base::Milliseconds(170));
    grow->set_tween_type(gfx::Tween::EASE_OUT_2);
    auto settle = ui::LayerAnimationElement::CreateTransformElement(
        settled, base::Milliseconds(140));
    settle->set_tween_type(gfx::Tween::EASE_IN_OUT);
    auto* spring = new ui::LayerAnimationSequence(std::move(grow));
    spring->AddElement(std::move(settle));
    animator->StartAnimation(spring);
  }
  if (web_view) {
    // Re-apply the rounded clip now that the web contents' native view is
    // attached to the widget: NativeViewHostAura::ApplyRoundedCorners() is a
    // silent no-op without an attached native view, and the layer can be
    // recreated during widget initialization, dropping radii applied earlier.
    web_view->holder()->SetCornerRadii(
        gfx::RoundedCornersF(0, kPopupCornerRadius, kPopupCornerRadius, 0));
    web_view->RequestFocus();
  }
}

ZephyrusSettingsPopup::ZephyrusSettingsPopup(Browser* browser,
                                             Section initial_section)
    : browser_(browser), current_(initial_section) {
  g_active_popup = this;

  SetModalType(ui::mojom::ModalType::kWindow);
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  SetTitle(u"Zephyrus Settings");
  SetShowTitle(false);
  SetShowCloseButton(false);
  // Translucent frameless window with compositor-rounded corners — the design's
  // intent, restored 2026-08-10.
  //
  // This was forced opaque (set_opaque_custom_frame(true) +
  // set_use_round_corners(false) + a DWM corner preference on the HWND) back
  // when DirectComposition was disabled: a translucent window could not deliver
  // per-pixel alpha, so its rounded-corner cutouts rendered as an opaque black
  // border, and the usual software-compositing escape hatch blanks a WebView.
  // DComp is on again — the disable was a workaround for a vsync bug that lived
  // somewhere else entirely — so none of that applies. The cost of the
  // workaround was the designed 20px radius (DWM rounds at ~8) and the soft
  // compositor shadow.
  set_use_round_corners(true);
  set_corner_radius(kPopupCornerRadius);
  set_margins(gfx::Insets());

  // Size to the browser window, capped so the popup keeps a floating,
  // palette-like feel.
  gfx::Size size(980, 680);
  if (browser_->window()) {
    const gfx::Rect bounds = browser_->window()->GetBounds();
    size = gfx::Size(std::min(1080, bounds.width() - 120),
                     std::min(720, bounds.height() - 100));
    size.SetToMax(gfx::Size(760, 480));
  }
  SetContentsView(BuildContentsView(size, initial_section));
}

ZephyrusSettingsPopup::~ZephyrusSettingsPopup() {
  if (g_active_popup == this) {
    g_active_popup = nullptr;
  }
}

std::unique_ptr<views::View> ZephyrusSettingsPopup::BuildContentsView(
    const gfx::Size& size,
    Section initial_section) {
  auto root = std::make_unique<views::View>();
  root->SetPreferredSize(size);
  root->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  auto* root_layout =
      static_cast<views::BoxLayout*>(root->GetLayoutManager());

  // ---- Left: pill navigation rail -----------------------------------------
  auto* rail = root->AddChildView(std::make_unique<views::View>());
  rail->SetPreferredSize(gfx::Size(kRailWidth, size.height()));
  rail->SetBackground(views::CreateSolidBackground(RailColor()));
  // Hairline between the rail and the hosted page gives the split definition.
  rail->SetBorder(views::CreateSolidSidedBorder(gfx::Insets::TLBR(0, 0, 0, 1),
                                                RailHairline()));
  auto* rail_layout = rail->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(12, 12, 12, 11),
      4));

  // A single close (✕) button at the top of the rail.
  auto* header = rail->AddChildView(std::make_unique<views::View>());
  header->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0, 0, 8, 0), 0));
  auto* close_button =
      header->AddChildView(std::make_unique<views::ImageButton>(
          base::BindRepeating(
              [](ZephyrusSettingsPopup* self, const ui::Event&) {
                self->ClosePopup();
              },
              base::Unretained(this))));
  close_button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(kZephyrusCloseIcon, MutedForeground(), 16));
  close_button->SetImageModel(
      views::Button::STATE_HOVERED,
      ui::ImageModel::FromVectorIcon(kZephyrusCloseIcon, Foreground(), 16));
  close_button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  close_button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  close_button->SetPreferredSize(gfx::Size(28, 28));
  close_button->GetViewAccessibility().SetName(u"Close settings");
  close_button->SetTooltipText(u"Close");
  views::InstallCircleHighlightPathGenerator(close_button);

  // Rail items: (section, icon, label), in three quiet groups —
  // browser configuration, the user's content, and about.
  const std::tuple<Section, const gfx::VectorIcon*, const char16_t*> items[] =
      {
          {Section::kSettings, &vector_icons::kSettingsIcon, u"Settings"},
          {Section::kShield, &vector_icons::kShieldIcon, u"Shield"},
          {Section::kHistory, &vector_icons::kHistoryIcon, u"History"},
          {Section::kDownloads, &vector_icons::kDownloadIcon, u"Downloads"},
          {Section::kBookmarks, &kBookmarkManagerIcon, u"Bookmarks"},
          {Section::kPasswords, &kPasswordIcon, u"Passwords"},
          {Section::kExtensions, &vector_icons::kChromeExtensionIcon,
           u"Extensions"},
          {Section::kAbout, &vector_icons::kInfoIcon, u"About Zephyrus"},
      };
  auto add_group_gap = [&rail]() {
    auto* gap = rail->AddChildView(std::make_unique<views::View>());
    gap->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 10), 0));
    auto* line = gap->AddChildView(std::make_unique<views::View>());
    line->SetBackground(views::CreateSolidBackground(RailHairline()));
    line->SetPreferredSize(gfx::Size(1, 1));
  };
  for (const auto& [item_section, item_icon, item_label] : items) {
    // Hairline gaps between the groups.
    if (item_section == Section::kHistory ||
        item_section == Section::kAbout) {
      add_group_gap();
    }
    auto* pill = rail->AddChildView(std::make_unique<ZephyrusNavPill>(
        base::BindRepeating(
            [](ZephyrusSettingsPopup* self, Section section,
               const ui::Event&) { self->SwitchTo(section); },
            base::Unretained(this), item_section),
        item_label, *item_icon));
    pills_.emplace_back(item_section, pill);
  }
  // Ignore the return value; the rail is fixed-width so no flex is needed,
  // but keep the pills top-aligned by letting trailing space collapse.
  rail_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kStart);

  // Zephyrus version, pinned to the foot of the rail (a flexible spacer above
  // pushes it down). Visible across all settings sections, including About.
  auto* spacer = rail->AddChildView(std::make_unique<views::View>());
  rail_layout->SetFlexForView(spacer, 1);
  auto* version = rail->AddChildView(std::make_unique<views::Label>(
      u"Zephyrus " + std::u16string(zephyrus::kVersion)));
  version->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  version->SetEnabledColor(MutedForeground());
  version->SetAutoColorReadabilityEnabled(false);
  version->SetSubpixelRenderingEnabled(false);
  version->SetFontList(version->font_list().DeriveWithSizeDelta(-1));
  version->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 20, 14, 12)));

  // ---- Right: embedded WebUI ----------------------------------------------
  auto* web_view =
      root->AddChildView(std::make_unique<views::WebView>(browser_->profile()));
  web_view_ = web_view;
  root_layout->SetFlexForView(web_view, 1);
  // Clip the native web contents to the popup's rounded right corners.
  web_view->holder()->SetCornerRadii(
      gfx::RoundedCornersF(0, kPopupCornerRadius, kPopupCornerRadius, 0));
  // Dark base under/behind the page so opening the popup and switching
  // sections never flashes white before the (dark) WebUI paints.
  web_view->SetBackground(views::CreateSolidBackground(WebViewBase()));
  // Register the embedding context BEFORE the first navigation: hosted WebUIs
  // (history clusters, settings subpages, ...) resolve their browser through
  // webui::GetBrowserWindowInterface() since they are not in a tab here.
  content::WebContents* web_contents = web_view->GetWebContents();
  webui::SetBrowserWindowInterface(web_contents, browser_);
  web_contents->SetPageBaseBackgroundColor(WebViewBase());
  web_contents->SetDelegate(this);
  // Observe navigations so the rail highlight follows pages the popup did not
  // navigate to itself (e.g. history's "Delete browsing data" subpage).
  Observe(web_contents);
  web_view->LoadInitialURL(SectionURL(initial_section));

  UpdatePillStates();
  return root;
}

void ZephyrusSettingsPopup::SwitchTo(Section section) {
  if (!web_view_ || !web_view_->GetWebContents()) {
    return;
  }
  content::WebContents* web_contents = web_view_->GetWebContents();
  // Re-selecting the section the page is already showing would reload the
  // page and throw away its state (scroll position, open subsections, search
  // text). Only navigate when it actually changes something; clicking the
  // active pill while deep in a subpage still returns to the section root.
  const GURL section_url = SectionURL(section);
  if (section == current_ &&
      web_contents->GetLastCommittedURL() == section_url) {
    web_view_->RequestFocus();
    return;
  }
  current_ = section;
  content::NavigationController::LoadURLParams params(section_url);
  params.transition_type = ui::PAGE_TRANSITION_AUTO_TOPLEVEL;
  web_contents->GetController().LoadURLWithParams(params);
  web_view_->RequestFocus();
  UpdatePillStates();
}

void ZephyrusSettingsPopup::UpdatePillStates() {
  for (const auto& [section, pill] : pills_) {
    static_cast<ZephyrusNavPill*>(pill)->SetSelected(section == current_);
  }
}

// static
std::optional<ZephyrusSettingsPopup::Section>
ZephyrusSettingsPopup::SectionForURL(const GURL& url) {
  if (!url.SchemeIs("chrome")) {
    return std::nullopt;
  }
  const std::string_view host = url.host();
  const std::string_view path = url.path();
  if (host == "settings") {
    // More specific settings subpages first.
    if (path == "/adBlocker") {
      return Section::kShield;
    }
    if (path == "/help") {
      return Section::kAbout;
    }
    // Everything else under settings (incl. clearBrowserData) is Settings.
    return Section::kSettings;
  }
  if (host == "extensions") {
    return Section::kExtensions;
  }
  if (host == "downloads") {
    return Section::kDownloads;
  }
  if (host == "history") {
    return Section::kHistory;
  }
  if (host == "bookmarks") {
    return Section::kBookmarks;
  }
  if (host == "password-manager") {
    return Section::kPasswords;
  }
  return std::nullopt;
}

void ZephyrusSettingsPopup::PrimaryPageChanged(content::Page& page) {
  // Keep the rail selection in sync with whatever page the webview is showing,
  // including navigations the popup didn't initiate via SwitchTo().
  const GURL& url = page.GetMainDocument().GetLastCommittedURL();
  if (std::optional<Section> section = SectionForURL(url);
      section && *section != current_) {
    current_ = *section;
    UpdatePillStates();
  }
}

void ZephyrusSettingsPopup::ClosePopup() {
  if (widget_ && !widget_->IsClosed()) {
    widget_->Close();
  }
}

void ZephyrusSettingsPopup::WindowClosing() {
  views::DialogDelegate::WindowClosing();
  // The widget is going away: sever every pointer that outside code (the
  // singleton in Show()) or the hosted WebContents could still reach us
  // through, while the view tree is still alive.
  if (g_active_popup == this) {
    g_active_popup = nullptr;
  }
  if (web_view_ && web_view_->GetWebContents()) {
    web_view_->GetWebContents()->SetDelegate(nullptr);
  }
  Observe(nullptr);
  web_view_ = nullptr;
  widget_ = nullptr;
  pills_.clear();
  // The widget does not own this delegate (WidgetDelegate defaults to
  // not-owned and SetOwnedByWidget is pass-key gated), so it must delete
  // itself. Deferred so the widget teardown currently on the stack never
  // touches a freed object.
  if (!delete_scheduled_) {
    delete_scheduled_ = true;
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                                  this);
  }
}

content::WebContents* ZephyrusSettingsPopup::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  if (params.disposition == WindowOpenDisposition::CURRENT_TAB) {
    // Navigation within the popup (links inside the hosted WebUI page).
    content::NavigationController::LoadURLParams load_params(params);
    base::WeakPtr<content::NavigationHandle> handle =
        source->GetController().LoadURLWithParams(load_params);
    if (navigation_handle_callback && handle) {
      std::move(navigation_handle_callback).Run(*handle);
    }
    return source;
  }
  // Links that target a new tab open in the real browser; the modal popup
  // closes so the user actually sees the page.
  NavigateParams nav(browser_, params.url, params.transition);
  nav.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&nav);
  ClosePopup();
  return nullptr;
}

content::WebContents* ZephyrusSettingsPopup::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  // window.open()/target=_blank from the hosted page: hand the new contents
  // to the browser, preserving the requested disposition (sign-in flows from
  // settings open NEW_POPUP windows and expect a real popup), then close the
  // modal so the user can interact with what opened.
  if (disposition == WindowOpenDisposition::CURRENT_TAB ||
      disposition == WindowOpenDisposition::NEW_BACKGROUND_TAB) {
    disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  }
  content::WebContents* added =
      chrome::AddWebContents(browser_, nullptr, std::move(new_contents),
                             target_url, disposition, window_features);
  ClosePopup();
  return added;
}

content::KeyboardEventProcessingResult
ZephyrusSettingsPopup::PreHandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (event.GetType() != blink::WebInputEvent::Type::kRawKeyDown) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  // Esc closes the popup even while the web contents has focus.
  if (event.windows_key_code == ui::VKEY_ESCAPE) {
    ClosePopup();
    return content::KeyboardEventProcessingResult::HANDLED;
  }
  // The popup is its own widget, so browser accelerators don't reach it while
  // the hosted page has focus. Handle the section shortcuts locally so
  // Ctrl+H / Ctrl+J / Ctrl+Shift+O keep working inside the popup.
  const int modifiers =
      event.GetModifiers() & blink::WebInputEvent::kKeyModifiers;
  if (modifiers == blink::WebInputEvent::kControlKey) {
    if (event.windows_key_code == ui::VKEY_H) {
      SwitchTo(Section::kHistory);
      return content::KeyboardEventProcessingResult::HANDLED;
    }
    if (event.windows_key_code == ui::VKEY_J) {
      SwitchTo(Section::kDownloads);
      return content::KeyboardEventProcessingResult::HANDLED;
    }
  } else if (modifiers == (blink::WebInputEvent::kControlKey |
                           blink::WebInputEvent::kShiftKey) &&
             event.windows_key_code == ui::VKEY_O) {
    SwitchTo(Section::kBookmarks);
    return content::KeyboardEventProcessingResult::HANDLED;
  }
  return content::KeyboardEventProcessingResult::NOT_HANDLED;
}
