// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_settings_popup.h"

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
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
#include "ui/views/animation/ink_drop.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/scrollbar/overlay_scroll_bar.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/vector_icons.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

// The single active popup (at most one across all windows).
ZephyrusSettingsPopup* g_active_popup = nullptr;

// POPUP radius, not card. This sheet floats over the window, so it takes M3's
// dialog/sheet step (28) and now matches every upstream dialog, which
// LayoutProvider maps to the same value.
//
// It asked for kRadiusCard before, which was simply the wrong constant for what
// this is -- an 8px sheet reads as a clipped rectangle rather than something
// cast on top of the window.
//
// The DPI constraint that shaped the old value still holds and 28 satisfies it:
// this radius reaches the WebView's layer via holder()->SetCornerRadii(), and a
// layer-rounded corner must be a whole number of device pixels or the curve
// renders blurry at fractional scaling. 28 is a multiple of 4, so it is whole
// at 1.25x (35), 1.5x (42) and 2x (56).
constexpr int kPopupCornerRadius = zephyrus::kRadiusPopup;
constexpr int kRailWidth = 208;
constexpr int kRailInset = 12;

// The rail is a column of M3 NAVIGATION DRAWER ITEMS, with the numbers taken
// from Material's NavigationDrawerTokens rather than chosen: a 56dp item, a
// full-pill active indicator on secondaryContainer, a 24dp icon and a
// labelLarge label; the active item's content is onSecondaryContainer and
// every other item's is onSurfaceVariant.
constexpr int kItemHeight = 56;
constexpr int kItemIconSize = 24;
constexpr int kItemIconLabelGap = 12;
constexpr int kItemLeadingPadding = 16;
constexpr int kItemTrailingPadding = 24;

// An M3 standard icon button: a 40dp target around a 24dp icon.
constexpr int kIconButtonSize = 40;
constexpr int kIconSize = 24;

// An M3 divider between the rail's groups: 1dp of outlineVariant, inset.
constexpr int kDividerInsetH = 16;
constexpr int kDividerInsetV = 8;

// Effectively unbounded: ScrollView::ClipHeightTo needs a real maximum.
constexpr int kUnboundedScrollHeight = 100000;

// Colours are ROLES, and a role can only be read from a view that is in a
// Widget.
//
// The rail and the page base are the SAME role, and that is measured, not a
// shortcut. The base was `surface` for a tonal split against the rail, and it
// produced a 1px dark line instead: at 1.5x the web contents lands a device
// pixel inside the WebView, so the WebView's own background shows at the edge
// — 19,19,20 between a rail at 31,32,32 and a page at 32,33,36. The page
// paints its OWN background, which is Chromium's WebUI colour and not ours,
// so a split here cannot show until WebUI is aligned (overhaul Phase 6).
// Until then the honest result is one dialog surface, with the base matching
// it so neither the sliver nor the pre-paint flash stands out.
SkColor RailColor(const views::View& view) {
  return zephyrus::m3::Role(view, kColorZephyrusSurfaceContainer);
}
SkColor ContentBase(const views::View& view) {
  return RailColor(view);
}

// A rail entry: an M3 navigation drawer item. Selection is the full-pill
// active indicator on secondaryContainer — the whole signal. The accent notch
// that used to sit on its leading edge was the retired design language saying
// "selected" a second time. Hover and press are M3 state layers.
class ZephyrusNavPill : public views::LabelButton {
  METADATA_HEADER(ZephyrusNavPill, views::LabelButton)

 public:
  ZephyrusNavPill(PressedCallback callback,
                  const std::u16string& label,
                  const gfx::VectorIcon& icon)
      : views::LabelButton(std::move(callback), label), icon_(icon) {
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetImageLabelSpacing(kItemIconLabelGap);
    SetBorder(views::CreateEmptyBorder(
        gfx::Insets::TLBR(0, kItemLeadingPadding, 0, kItemTrailingPadding)));
    SetMinSize(gfx::Size(0, kItemHeight));
    this->label()->SetFontList(
        zephyrus::m3::Font(zephyrus::m3::Type::kLabelLarge));
    GetViewAccessibility().SetName(label);
    // No colour here. A role read from a view that is not yet in a Widget comes
    // back as the sentinel; OnThemeChanged applies the real ones, and runs
    // before the first paint.
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
    // Only the state layer changes with hover and press; M3 does not brighten
    // the label, so there is nothing to recolour.
    SchedulePaint();
  }

  void OnThemeChanged() override {
    views::LabelButton::OnThemeChanged();
    UpdateColors();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (!GetWidget()) {
      return;
    }
    const bool hovered = GetState() == views::Button::STATE_HOVERED;
    const bool pressed = GetState() == views::Button::STATE_PRESSED;
    if (!selected_ && !hovered && !pressed) {
      return;
    }
    const SkColor content = Ink();
    SkColor fill =
        selected_ ? zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer)
                  : SK_ColorTRANSPARENT;
    if (hovered || pressed) {
      const SkAlpha layer =
          pressed ? zephyrus::m3::kPressed : zephyrus::m3::kHover;
      fill = selected_ ? zephyrus::m3::WithStateLayer(fill, content, layer)
                       : zephyrus::m3::StateLayer(content, layer);
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(fill);
    const gfx::RectF bounds(GetLocalBounds());
    // Full pill: the drawer's active indicator is ShapeKeyTokens.CornerFull.
    canvas->DrawRoundRect(bounds, bounds.height() / 2.0f, flags);
  }

 private:
  SkColor Ink() const {
    return zephyrus::m3::Role(*this, selected_
                                         ? kColorZephyrusOnSecondaryContainer
                                         : kColorZephyrusOnSurfaceVariant);
  }

  void UpdateColors() {
    if (!GetWidget()) {
      return;
    }
    const SkColor ink = Ink();
    for (views::Button::ButtonState state :
         {views::Button::STATE_NORMAL, views::Button::STATE_HOVERED,
          views::Button::STATE_PRESSED}) {
      SetTextColor(state, ink);
    }
    SetImageModel(views::Button::STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(*icon_, ink, kItemIconSize));
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

// The popup's contents. It exists to apply the theme to the parts that do
// not colour themselves: this whole tree is built inside the dialog's
// constructor, before any Widget or ColorProvider exists, so nothing built
// there may ask for a role. OnThemeChanged runs once the popup is in its
// Widget and before the first paint, and again on every theme change.
class SettingsPopupContents : public views::View {
  METADATA_HEADER(SettingsPopupContents, views::View)

 public:
  SettingsPopupContents() = default;
  SettingsPopupContents(const SettingsPopupContents&) = delete;
  SettingsPopupContents& operator=(const SettingsPopupContents&) = delete;
  ~SettingsPopupContents() override = default;

  void set_rail(views::View* rail) { rail_ = rail; }
  void add_divider(views::View* divider) { dividers_.push_back(divider); }
  void set_close_button(views::ImageButton* button) { close_button_ = button; }
  void set_version(views::Label* version) { version_ = version; }
  void set_web_view(views::WebView* web_view) { web_view_ = web_view; }

  // views::View:
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    if (rail_) {
      rail_->SetBackground(views::CreateSolidBackground(RailColor(*this)));
    }
    const SkColor divider =
        zephyrus::m3::Role(*this, kColorZephyrusOutlineVariant);
    for (views::View* line : dividers_) {
      line->SetBackground(views::CreateSolidBackground(divider));
    }
    const SkColor variant =
        zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant);
    if (close_button_) {
      close_button_->SetImageModel(
          views::Button::STATE_NORMAL,
          ui::ImageModel::FromVectorIcon(kZephyrusCloseIcon, variant,
                                         kIconSize));
      // An M3 icon button shows hover and press as a state layer in the
      // content colour, not by recolouring the icon.
      views::InkDrop::Get(close_button_)->SetBaseColor(variant);
    }
    if (version_) {
      version_->SetEnabledColor(variant);
    }
    if (web_view_) {
      const SkColor base = ContentBase(*this);
      web_view_->SetBackground(views::CreateSolidBackground(base));
      if (content::WebContents* contents = web_view_->GetWebContents()) {
        contents->SetPageBaseBackgroundColor(base);
      }
    }
  }

 private:
  raw_ptr<views::View> rail_ = nullptr;
  std::vector<raw_ptr<views::View>> dividers_;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  raw_ptr<views::Label> version_ = nullptr;
  raw_ptr<views::WebView> web_view_ = nullptr;
};

BEGIN_METADATA(SettingsPopupContents)
END_METADATA

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
    fade->set_tween_type(
        zephyrus::m3::TweenFor(zephyrus::m3::Spring::kFastEffects));
    animator->StartAnimation(new ui::LayerAnimationSequence(std::move(fade)));
    auto grow = ui::LayerAnimationElement::CreateTransformElement(
        gfx::GetScaleTransform(center, 1.015f), base::Milliseconds(170));
    grow->set_tween_type(
        zephyrus::m3::TweenFor(zephyrus::m3::Spring::kFastSpatial));
    auto settle = ui::LayerAnimationElement::CreateTransformElement(
        settled, base::Milliseconds(140));
    settle->set_tween_type(
        zephyrus::m3::TweenFor(zephyrus::m3::Spring::kFastSpatial));
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
  auto root = std::make_unique<SettingsPopupContents>();
  SettingsPopupContents* const contents = root.get();
  root->SetPreferredSize(size);
  root->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  auto* root_layout =
      static_cast<views::BoxLayout*>(root->GetLayoutManager());

  // ---- Left: M3 navigation drawer items -----------------------------------
  // No hairline against the page: the rail's tone does that now. Colours are
  // applied by SettingsPopupContents::OnThemeChanged.
  auto* rail = root->AddChildView(std::make_unique<views::View>());
  contents->set_rail(rail);
  rail->SetPreferredSize(gfx::Size(kRailWidth, size.height()));
  auto* rail_layout = rail->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(kRailInset), 0));
  rail_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Close, as an M3 standard icon button at the top of the rail.
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
  contents->set_close_button(close_button);
  close_button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  close_button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  close_button->SetPreferredSize(gfx::Size(kIconButtonSize, kIconButtonSize));
  close_button->GetViewAccessibility().SetName(u"Close settings");
  close_button->SetTooltipText(u"Close");
  views::InstallCircleHighlightPathGenerator(close_button);
  views::InkDrop::Get(close_button)
      ->SetMode(views::InkDropHost::InkDropMode::ON);
  views::InkDrop::Get(close_button)
      ->SetHighlightOpacity(zephyrus::m3::kHover / 255.0f);
  views::InkDrop::Get(close_button)
      ->SetVisibleOpacity(zephyrus::m3::kPressed / 255.0f);

  // The items SCROLL. At M3's 56dp the eight of them, the dividers and the
  // header come to roughly 584dp, and the popup is allowed down to 480 on a
  // small window — without this the last items and the version would simply
  // be cut off. Set up exactly as the sidebar's tab list, which learned both
  // halves the hard way: ClipHeightTo is required, and the scrollbar must be
  // an overlay.
  auto* scroll = rail->AddChildView(std::make_unique<views::ScrollView>());
  scroll->ClipHeightTo(0, kUnboundedScrollHeight);
  scroll->SetBackgroundColor(std::nullopt);
  scroll->SetDrawOverflowIndicator(false);
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll->SetVerticalScrollBar(std::make_unique<views::OverlayScrollBar>(
      views::ScrollBar::Orientation::kVertical));
  auto item_list = std::make_unique<views::BoxLayoutView>();
  item_list->SetOrientation(views::BoxLayout::Orientation::kVertical);
  item_list->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  views::View* const list = scroll->SetContents(std::move(item_list));
  rail_layout->SetFlexForView(scroll, 1);

  // Rail items: (section, icon, label), in three groups — browser
  // configuration, the user's content, and about — divided by M3 dividers.
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
  auto add_divider = [list, contents]() {
    auto* holder = list->AddChildView(std::make_unique<views::View>());
    holder->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::VH(kDividerInsetV, kDividerInsetH), 0));
    auto* line = holder->AddChildView(std::make_unique<views::View>());
    line->SetPreferredSize(gfx::Size(1, 1));
    contents->add_divider(line);
  };
  for (const auto& [item_section, item_icon, item_label] : items) {
    if (item_section == Section::kHistory ||
        item_section == Section::kAbout) {
      add_divider();
    }
    auto* pill = list->AddChildView(std::make_unique<ZephyrusNavPill>(
        base::BindRepeating(
            [](ZephyrusSettingsPopup* self, Section section,
               const ui::Event&) { self->SwitchTo(section); },
            base::Unretained(this), item_section),
        item_label, *item_icon));
    pills_.emplace_back(item_section, pill);
  }

  // Zephyrus version at the foot of the rail, below the scrolling items.
  auto* version = rail->AddChildView(std::make_unique<views::Label>(
      u"Zephyrus " + std::u16string(zephyrus::kVersion)));
  contents->set_version(version);
  version->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  version->SetAutoColorReadabilityEnabled(false);
  version->SetSubpixelRenderingEnabled(false);
  version->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelSmall));
  version->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(8, kItemLeadingPadding, 2, 0)));

  // ---- Right: embedded WebUI ----------------------------------------------
  auto* web_view =
      root->AddChildView(std::make_unique<views::WebView>(browser_->profile()));
  web_view_ = web_view;
  root_layout->SetFlexForView(web_view, 1);
  // Clip the native web contents to the popup's rounded right corners.
  web_view->holder()->SetCornerRadii(
      gfx::RoundedCornersF(0, kPopupCornerRadius, kPopupCornerRadius, 0));
  contents->set_web_view(web_view);
  // A base under the page, so opening the popup and switching sections never
  // flashes white before the WebUI paints. This tree has no ColorProvider yet,
  // so the first value is read from the BROWSER WINDOW, which does; the
  // contents view re-applies it in OnThemeChanged.
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  // Register the embedding context BEFORE the first navigation: hosted WebUIs
  // (history clusters, settings subpages, ...) resolve their browser through
  // webui::GetBrowserWindowInterface() since they are not in a tab here.
  content::WebContents* web_contents = web_view->GetWebContents();
  webui::SetBrowserWindowInterface(web_contents, browser_);
  if (browser_view) {
    web_view->SetBackground(
        views::CreateSolidBackground(ContentBase(*browser_view)));
    web_contents->SetPageBaseBackgroundColor(ContentBase(*browser_view));
  }
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
