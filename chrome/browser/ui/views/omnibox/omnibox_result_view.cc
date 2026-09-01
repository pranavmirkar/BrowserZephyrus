// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/omnibox/omnibox_result_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"

#include <limits.h>

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_model.h"
#include "chrome/browser/ui/omnibox/omnibox_theme.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/location_bar/selected_keyword_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_local_answer_header_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_match_cell_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_view_views.h"
#include "chrome/browser/ui/views/omnibox/omnibox_suggestion_button_row_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_text_view.h"
#include "chrome/browser/ui/views/omnibox/remove_suggestion_bubble.h"
#include "chrome/browser/ui/views/omnibox/rounded_omnibox_results_frame.h"
#include "chrome/grit/generated_resources.h"
#include "components/omnibox/browser/actions/omnibox_pedal.h"
#include "components/omnibox/browser/autocomplete_match_type.h"
#include "components/omnibox/browser/omnibox.mojom-shared.h"
#include "components/omnibox/browser/omnibox_client.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "components/omnibox/browser/omnibox_popup_selection.h"
#include "components/omnibox/browser/vector_icons.h"
#include "components/omnibox/common/omnibox_features.h"
#include "components/strings/grit/components_strings.h"
#include "components/vector_icons/vector_icons.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "third_party/omnibox_proto/answer_data.pb.h"
#include "third_party/omnibox_proto/rich_answer_template.pb.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_features.h"
#include "ui/color/color_id.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/insets_f.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "cc/paint/paint_flags.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/border.h"
#include "ui/views/background.h"
#include "ui/views/painter.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/metadata/type_conversion.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"

namespace {

// Zephyrus: paints the selected-row "hero" pill (Figma searchbar_resultdropdown
// 71:207) — a rounded white fill with a 0.5px #b3b3b3 border and a soft drop
// shadow so the current result floats out of the list. `strength` (0..1) fades
// the border and shadow in during the width-jump so the pill materializes as a
// real surface rather than snapping on.
void PaintZephyrusHeroPill(gfx::Canvas* canvas,
                           const gfx::RectF& rect,
                           SkColor color,
                           float radius,
                           float strength) {
  std::vector<gfx::ShadowValue> shadows;
  shadows.emplace_back(gfx::Vector2d(0, 3), 12,
                       SkColorSetA(SK_ColorBLACK,
                                   static_cast<U8CPU>(0x33 * strength)));
  shadows.emplace_back(gfx::Vector2d(0, 1), 3,
                       SkColorSetA(SK_ColorBLACK,
                                   static_cast<U8CPU>(0x24 * strength)));
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(color);
  fill.setLooper(gfx::CreateShadowDrawLooper(shadows));
  canvas->DrawRoundRect(rect, radius, fill);
  cc::PaintFlags stroke;
  stroke.setAntiAlias(true);
  stroke.setStyle(cc::PaintFlags::kStroke_Style);
  stroke.setStrokeWidth(0.5f);
  // Border adapts to the surface: a hairline grey on light cards, a faint white
  // edge on dark ones, so the hero reads cleanly under the dynamic theme.
  const bool dark = color_utils::IsDark(color);
  const SkColor stroke_rgb = dark ? SK_ColorWHITE : SkColorSetRGB(0xB3, 0xB3, 0xB3);
  const SkAlpha stroke_a = dark ? 0x59 : 0xFF;
  stroke.setColor(
      SkColorSetA(stroke_rgb, static_cast<U8CPU>(stroke_a * strength)));
  canvas->DrawRoundRect(rect, radius, stroke);
}

bool PrefersHighContrast(const views::View* view) {
  const ui::NativeTheme* const native_theme = view->GetNativeTheme();
  return native_theme && native_theme->preferred_contrast() ==
                             ui::NativeTheme::PreferredContrast::kMore;
}

class OmniboxResultViewButton : public views::ImageButton {
  METADATA_HEADER(OmniboxResultViewButton, views::ImageButton)

 public:
  OmniboxResultViewButton(int a11y_message_id, PressedCallback callback)
      : ImageButton(std::move(callback)) {
    views::ConfigureVectorImageButton(this);

    SetAnimationDuration(base::TimeDelta());
    views::InkDrop::Get(this)->GetInkDrop()->SetHoverHighlightFadeDuration(
        base::TimeDelta());

    SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);

    // Although this appears visually as a button, expose as a list box option
    // so that it matches the other options within its list box container.
    GetViewAccessibility().SetRole(ax::mojom::Role::kListBoxOption);
    GetViewAccessibility().SetName(l10n_util::GetStringUTF16(a11y_message_id));
  }
};

BEGIN_METADATA(OmniboxResultViewButton)
END_METADATA

constexpr float kIPHBackgroundBorderRadius = 8;

// Zephyrus (Figma searchbar_resultdropdown) row geometry. Rows span the full
// results frame; the card is drawn inset by GetZephyrusCardInset() so it lands
// flush with the search bar. Row content is pinned to that same inset, so
// icons/text always sit inside the card. The collapsed (hovered) pill spans the
// card exactly; the selected "hero" pill grows kZephyrusHeroOverhang past the
// card on each side (Figma: hero 531 vs card 477), while the content stays
// pinned — so the favicon never moves or leaves the card.
int ZephyrusCardInset() {
  return RoundedOmniboxResultsFrame::GetZephyrusCardInset();
}
int ZephyrusHeroPillInset() {
  return std::max(0, ZephyrusCardInset() -
                         RoundedOmniboxResultsFrame::kZephyrusHeroOverhang);
}
// Both track the shared popup radius. They sit inside a 28px card, and a row
// pill with visibly tighter corners than the surface holding it reads as a
// different design rather than a smaller one. Clamped to a pill at row height,
// which is the intent.
constexpr float kZephyrusHoverPillRadius =
    static_cast<float>(zephyrus::kRadiusPopup);
constexpr float kZephyrusHeroPillRadius =
    static_cast<float>(zephyrus::kRadiusPopup);

// Zephyrus motion. The pill is a state indicator on a surface the user scans
// constantly, so it stays well under the 300ms UI ceiling. Enter leads with a
// strongly decelerating ease-out (Chromium's plain EASE_OUT is too weak to read
// as intentional); exit is snappier still, because dismissing should never feel
// like waiting.
constexpr base::TimeDelta kZephyrusHeroEnterDuration = base::Milliseconds(150);
constexpr base::TimeDelta kZephyrusHeroExitDuration = base::Milliseconds(110);

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// OmniboxResultSelectionIndicator

class OmniboxResultSelectionIndicator : public views::View {
  METADATA_HEADER(OmniboxResultSelectionIndicator, views::View)

 public:
  const int kStrokeThickness = 4;

  OmniboxResultSelectionIndicator() {
    // Height will automatically match the parent's height.
    SetPreferredSize(gfx::Size(kStrokeThickness, 0));
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    SkPath path = GetPath();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(
        GetColorProvider()->GetColor(kColorOmniboxResultsFocusIndicator));
    flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawPath(path, flags);
  }

 private:
  // The focus bar is a straight vertical line with half-rounded endcaps. Since
  // this geometry is nontrivial to represent using primitives, it's instead
  // represented using a fill path. This matches the style and implementation
  // used in Tab Groups.
  SkPath GetPath() const {
    return SkPathBuilder()
        .moveTo(0, 0)
        .arcTo(SkVector(kStrokeThickness, kStrokeThickness), 0,
               SkPathBuilder::kSmall_ArcSize, SkPathDirection::kCW,
               SkPoint(kStrokeThickness, kStrokeThickness))
        .lineTo(kStrokeThickness, height() - kStrokeThickness)
        .arcTo(SkVector(kStrokeThickness, kStrokeThickness), 0,
               SkPathBuilder::kSmall_ArcSize, SkPathDirection::kCW,
               SkPoint(0, height()))
        .close()
        .detach();
  }
};

BEGIN_METADATA(OmniboxResultSelectionIndicator)
END_METADATA

////////////////////////////////////////////////////////////////////////////////
// OmniboxResultView, public:

OmniboxResultView::OmniboxResultView(OmniboxPopupViewViews* popup_view,
                                     size_t model_index)
    : popup_view_(popup_view),
      model_index_(model_index),
      // Using base::Unretained is correct here. 'this' outlives the callback.
      mouse_enter_exit_handler_(
          base::BindRepeating(&OmniboxResultView::UpdateHoverState,
                              base::Unretained(this))) {
  CHECK_GE(model_index, 0u);

  // Zephyrus: fast, ease-out width-jump for the selected hero pill. Kept short
  // (selection moves with arrow keys, a many-times-a-day action) so it reads as
  // a crisp pop rather than a sluggish transition.
  zephyrus_hero_animation_.SetSlideDuration(kZephyrusHeroEnterDuration);
  // EASE_OUT_3 decelerates harder than plain EASE_OUT: the pill leaves
  // immediately and settles, which reads as intentional rather than mushy.
  zephyrus_hero_animation_.SetTweenType(gfx::Tween::EASE_OUT_3);

  // The view hierarchy is:
  // OmniboxResultView (FillLayout)
  //   selection_indicator_
  //   local_answer_header_and_suggestion_and_buttons_ (BoxLayout vertical)
  //     local_answer_header_ (added lazily)
  //     divider_line_
  //     suggestion_and_buttons (FlexLayout horizontal)
  //       suggestion_and_button_row (FlexLayout horizontal)
  //         suggestion_view_
  //         button_row_
  //       thumbs_up_button_
  //       thumbs_down_button_
  //       remove_suggestion_button_

  // TODO(crbug.com/370088101): The division between `OmniboxResultView` and
  //   `suggestion_view_` is not clear. Should we inline `suggestion_view_`'s
  //   members in `OmniboxResultView`? Or should we move e.g. the thumb and
  //   remove buttons into `suggestion_view_`? `suggestion_view_` currently uses
  //   custom layout, so it's easier to add new views to `OmniboxResultView`
  //   when possible.

  SetLayoutManager(std::make_unique<views::FillLayout>());

  selection_indicator_ =
      AddChildView(std::make_unique<OmniboxResultSelectionIndicator>());

  local_answer_header_and_suggestion_and_buttons_ =
      AddChildView(std::make_unique<views::View>());
  local_answer_header_and_suggestion_and_buttons_
      ->SetLayoutManager(std::make_unique<views::BoxLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical);
  // Zephyrus: align the row's content (icon, text, buttons) to the selection
  // pill, so the favicon/text keep a constant padding inside whichever surface
  // is active and never sit outside it. Tracks the width-jump animation.
  UpdateZephyrusContentInset();

  divider_line_ = local_answer_header_and_suggestion_and_buttons_->AddChildView(
      std::make_unique<views::Separator>());
  divider_line_->SetOrientation(views::Separator::Orientation::kHorizontal);
  divider_line_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(8, 0, 2, 0));

  auto* suggestion_and_buttons =
      local_answer_header_and_suggestion_and_buttons_->AddChildView(
          std::make_unique<views::View>());
  suggestion_and_buttons
      ->SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);

  views::View* suggestion_and_button_row =
      suggestion_and_buttons->AddChildView(std::make_unique<views::View>());
  suggestion_and_button_row->SetLayoutManager(
      std::make_unique<views::FlexLayout>());
  suggestion_and_button_row->SetProperty(
      views::kFlexBehaviorKey,
      // FlexSpecification has multiple constructors, and if no direction is
      // specified, the settings will be used in both horizontal and vertical
      // directions. Therefore, we must specify the horizontal direction.
      // Otherwise, the vertical height will be stretched.
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));

  suggestion_view_ = suggestion_and_button_row->AddChildView(
      std::make_unique<OmniboxMatchCellView>(this));
  suggestion_view_->iph_link_view()->SetCallback(base::BindRepeating(
      &OmniboxResultView::OpenIphLink, weak_factory_.GetWeakPtr()));

  auto* const iph_link_focus_ring =
      views::FocusRing::Get(suggestion_view_->iph_link_view());
  iph_link_focus_ring->SetHasFocusPredicate(base::BindRepeating(
      [](const OmniboxResultView* result_view, const View* view) {
        return view->GetVisible() && result_view->GetMatchSelected() &&
               result_view->popup_view_->GetSelection().state ==
                   OmniboxPopupSelection::FOCUSED_IPH_LINK;
      },
      base::Unretained(this)));
  iph_link_focus_ring->SetColorId(kColorOmniboxResultsFocusIndicator);

  // TODO(b/345536738): Move the common code for setting up instances of
  //  OmniboxResultViewButton to the constructor.
  thumbs_up_button_ = suggestion_and_buttons->AddChildView(
      std::make_unique<OmniboxResultViewButton>(
          IDS_ACC_THUMBS_UP_SUGGESTION_BUTTON,
          base::BindRepeating(
              &OmniboxResultView::ButtonPressed, base::Unretained(this),
              OmniboxPopupSelection::FOCUSED_BUTTON_THUMBS_UP)));
  thumbs_up_button_->SetProperty(views::kMarginsKey,
                                 gfx::Insets::TLBR(0, 0, 0, 8));
  views::InstallCircleHighlightPathGenerator(thumbs_up_button_);
  thumbs_up_button_->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_OMNIBOX_THUMBS_UP_SUGGESTION));
  auto* const thumbs_up_focus_ring = views::FocusRing::Get(thumbs_up_button_);
  thumbs_up_focus_ring->SetHasFocusPredicate(base::BindRepeating(
      [](const OmniboxResultView* results, const View* view) {
        return view->GetVisible() && results->GetMatchSelected() &&
               (results->popup_view_->GetSelection().state ==
                OmniboxPopupSelection::FOCUSED_BUTTON_THUMBS_UP);
      },
      base::Unretained(this)));
  thumbs_up_focus_ring->SetColorId(kColorOmniboxResultsFocusIndicator);

  thumbs_down_button_ = suggestion_and_buttons->AddChildView(
      std::make_unique<OmniboxResultViewButton>(
          IDS_ACC_THUMBS_DOWN_SUGGESTION_BUTTON,
          base::BindRepeating(
              &OmniboxResultView::ButtonPressed, base::Unretained(this),
              OmniboxPopupSelection::FOCUSED_BUTTON_THUMBS_DOWN)));
  thumbs_down_button_->SetProperty(views::kMarginsKey,
                                   gfx::Insets::TLBR(0, 0, 0, 8));
  views::InstallCircleHighlightPathGenerator(thumbs_down_button_);
  thumbs_down_button_->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_OMNIBOX_THUMBS_DOWN_SUGGESTION));
  auto* const thumbs_down_focus_ring =
      views::FocusRing::Get(thumbs_down_button_);
  thumbs_down_focus_ring->SetHasFocusPredicate(base::BindRepeating(
      [](const OmniboxResultView* results, const View* view) {
        return view->GetVisible() && results->GetMatchSelected() &&
               (results->popup_view_->GetSelection().state ==
                OmniboxPopupSelection::FOCUSED_BUTTON_THUMBS_DOWN);
      },
      base::Unretained(this)));
  thumbs_down_focus_ring->SetColorId(kColorOmniboxResultsFocusIndicator);

  remove_suggestion_button_ = suggestion_and_buttons->AddChildView(
      std::make_unique<OmniboxResultViewButton>(
          IDS_ACC_REMOVE_SUGGESTION_BUTTON,
          base::BindRepeating(
              &OmniboxResultView::ButtonPressed, base::Unretained(this),
              OmniboxPopupSelection::FOCUSED_BUTTON_REMOVE_SUGGESTION)));
  remove_suggestion_button_->SetProperty(views::kMarginsKey,
                                         gfx::Insets::TLBR(0, 0, 0, 16));
  views::InstallCircleHighlightPathGenerator(remove_suggestion_button_);
  auto* const remove_focus_ring =
      views::FocusRing::Get(remove_suggestion_button_);
  remove_focus_ring->SetHasFocusPredicate(base::BindRepeating(
      [](const OmniboxResultView* results, const View* view) {
        return view->GetVisible() && results->GetMatchSelected() &&
               (results->popup_view_->GetSelection().state ==
                OmniboxPopupSelection::FOCUSED_BUTTON_REMOVE_SUGGESTION);
      },
      base::Unretained(this)));
  remove_focus_ring->SetColorId(kColorOmniboxResultsFocusIndicator);

  button_row_ = suggestion_and_button_row->AddChildView(
      std::make_unique<OmniboxSuggestionButtonRowView>(popup_view_,
                                                       model_index));
  // If there's insufficient space for rendering both the suggestion text
  // and the action chip row at their preferred sizes, the give priority to the
  // button row by setting its order to 1 here and the suggestion view's order
  // to 2 in `SetMatch()` (lower numbers get higher priority).
  button_row_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kPreferred)
          .WithOrder(1));

  mouse_enter_exit_handler_.ObserveMouseEnterExitOn(this);

  GetViewAccessibility().SetRole(ax::mojom::Role::kListBoxOption);
  UpdateAccessibleName();
  GetViewAccessibility().SetPosInSet(model_index_ + 1);
}

OmniboxResultView::~OmniboxResultView() = default;

SkColor OmniboxResultView::GetThemedColor(ui::ColorId id) const {
  if (popup_view_) {
    if (LocationBarView* location_bar = popup_view_->location_bar_view()) {
      if (std::optional<SkColor> color =
              location_bar->GetZephyrusOmniboxColor(id)) {
        return *color;
      }
    }
  }
  return GetColorProvider()->GetColor(id);
}

// static
std::unique_ptr<views::Background> OmniboxResultView::GetPopupCellBackground(
    const views::View* view,
    OmniboxPartState part_state) {
  DCHECK(view);

  // TODO(tapted): Consider using background()->SetNativeControlColor() and
  // always have a background.
  if (part_state == OmniboxPartState::NORMAL && !PrefersHighContrast(view)) {
    return nullptr;
  }

  // Zephyrus: resolve the background to a page-color-adapted value (so the
  // hovered/selected rows match the page) when available, else the theme color.
  const ui::ColorId background_id = GetOmniboxBackgroundColorId(part_state);
  SkColor background_color;
  if (const auto* result = views::AsViewClass<OmniboxResultView>(view)) {
    background_color = result->GetThemedColor(background_id);
  } else {
    background_color = view->GetColorProvider()->GetColor(background_id);
  }

  if (part_state == OmniboxPartState::IPH) {
    return views::CreateRoundedRectBackground(
        background_color,
        /*radius=*/kIPHBackgroundBorderRadius,
        /*for_border_thickness=*/0);
  }

  // Zephyrus: both the SELECTED and HOVERED rows use the SAME animated "hero"
  // pill, painted in OnPaintBackground (so it renders behind the icon/text and
  // can animate its width). Return no static Background for either — only the
  // high-contrast fallback below keeps a plain grey pill.
  if (part_state == OmniboxPartState::SELECTED ||
      part_state == OmniboxPartState::HOVERED) {
    return nullptr;
  }
  return views::CreateBackgroundFromPainter(
      views::Painter::CreateSolidRoundRectPainter(
          background_color, kZephyrusHoverPillRadius,
          gfx::Insets::VH(2, ZephyrusCardInset())));
}

void OmniboxResultView::SetMatch(const AutocompleteMatch& match) {
  match_ = match.GetMatchWithContentsAndDescriptionPossiblySwapped();

  suggestion_view_->SetProperty(views::kMarginsKey,
                                gfx::Insets::TLBR(0, 0, 0, 0));
  // Allocate space for the suggestion text only after accounting for the space
  // needed to render the inline action chip row, by setting the order to 2.
  //
  // In the toolbelt case, we want to snap the suggestion text to zero, since
  // that looks better when there's not room for both. But in the normal case,
  // we want to scale the suggestion text to zero since an elided suggestion
  // still provides useful information.
  suggestion_view_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(
          match_.IsToolbelt()
              ? views::MinimumFlexSizeRule::kPreferredSnapToMinimum
              : views::MinimumFlexSizeRule::kScaleToMinimum,
          views::MaximumFlexSizeRule::kPreferred)
          .WithOrder(2));

  suggestion_view_->OnMatchUpdate(this, match_);
  UpdateDividerLineVisibility();
  UpdateSecondaryTextVisibility();
  UpdateFeedbackButtonsVisibility();
  UpdateRemoveSuggestionVisibility();
  if (match_.IsIphSuggestion()) {
    remove_suggestion_button_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_OMNIBOX_CLOSE_IPH_SUGGESTION));
    remove_suggestion_button_->GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_ACC_DISMISS_CHROME_TIP_BUTTON));
  } else {
    remove_suggestion_button_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_OMNIBOX_REMOVE_SUGGESTION));
    remove_suggestion_button_->GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_ACC_REMOVE_SUGGESTION_BUTTON));
  }

  button_row_->UpdateFromModel();

  if (match_.type == AutocompleteMatchType::Type::HISTORY_EMBEDDINGS_ANSWER) {
    if (!local_answer_header_) {
      local_answer_header_ =
          local_answer_header_and_suggestion_and_buttons_->AddChildViewAt(
              std::make_unique<OmniboxLocalAnswerHeaderView>(), 0);
    }
    local_answer_header_->SetText(match.history_embeddings_answer_header_text);
    local_answer_header_->SetVisible(true);
    local_answer_header_->SetThrobberVisibility(
        match.history_embeddings_answer_header_loading);
  } else if (local_answer_header_) {
    local_answer_header_->SetVisible(false);
  }

  ApplyThemeAndRefreshIcons();
  InvalidateLayout();
  UpdateAccessibleName();
}

void OmniboxResultView::ApplyThemeAndRefreshIcons(bool force_reapply_styles) {
  const ui::ColorId icon_color_id = GetMatchSelected()
                                        ? kColorOmniboxResultsIconSelected
                                        : kColorOmniboxResultsIcon;

  // TODO(b/345536738): Iterate over all the buttons and updates their icons.
  views::SetImageFromVectorIconWithColor(
      thumbs_up_button_,
      match_.feedback_type == FeedbackType::kThumbsUp
          ? features::IsRoundedIconsEnabled()
                ? vector_icons::kThumbUpFilledIcon
                : vector_icons::kThumbUpFilledOldIcon
      : features::IsRoundedIconsEnabled() ? vector_icons::kThumbUpIcon
                                          : vector_icons::kThumbUpOldIcon,
      GetLayoutConstant(LayoutConstant::kLocationBarIconSize),
      {icon_color_id,
       /* omnibox buttons are never disabled */
       gfx::kPlaceholderColor, icon_color_id});
  if (thumbs_up_button_->GetVisible()) {
    views::FocusRing::Get(thumbs_up_button_)->Refresh();
  }

  views::SetImageFromVectorIconWithColor(
      thumbs_down_button_,
      match_.feedback_type == FeedbackType::kThumbsDown
          ? features::IsRoundedIconsEnabled()
                ? vector_icons::kThumbDownFilledIcon
                : vector_icons::kThumbDownFilledOldIcon
      : features::IsRoundedIconsEnabled() ? vector_icons::kThumbDownIcon
                                          : vector_icons::kThumbDownOldIcon,
      GetLayoutConstant(LayoutConstant::kLocationBarIconSize),
      {icon_color_id,
       /* omnibox buttons are never disabled */
       gfx::kPlaceholderColor, icon_color_id});
  if (thumbs_down_button_->GetVisible()) {
    views::FocusRing::Get(thumbs_down_button_)->Refresh();
  }

  views::SetImageFromVectorIconWithColor(
      remove_suggestion_button_,
      features::IsRoundedIconsEnabled() ? vector_icons::kCloseIcon
                                        : vector_icons::kCloseRoundedOldIcon,
      GetLayoutConstant(LayoutConstant::kLocationBarIconSize),
      {icon_color_id,
       /* omnibox buttons are never disabled */
       gfx::kPlaceholderColor, icon_color_id});
  if (remove_suggestion_button_->GetVisible()) {
    views::FocusRing::Get(remove_suggestion_button_)->Refresh();
  }

  const OmniboxPartState state = GetThemeState();
  SetBackground(GetPopupCellBackground(this, state));

  // Zephyrus: drive the selected-row hero pill's width-jump. Show() animates
  // the pill out to its full hero width when this row becomes selected; Hide()
  // collapses it back when it isn't. OnPaintBackground reads the progress.
  // Zephyrus: hover and selection share the SAME hero pill + width-jump, so a
  // hovered row lights up exactly like the keyboard-selected one.
  const bool zephyrus_active = (state == OmniboxPartState::SELECTED ||
                                state == OmniboxPartState::HOVERED);
  if (zephyrus_active != zephyrus_hero_shown_) {
    zephyrus_hero_shown_ = zephyrus_active;
    // Respect the user's reduced-motion setting: snap straight to the final
    // state rather than sliding the pill's width. Otherwise use an asymmetric
    // pair — quick to arrive, quicker to leave.
    zephyrus_hero_animation_.SetSlideDuration(
        !gfx::Animation::ShouldRenderRichAnimation()
            ? base::TimeDelta()
            : (zephyrus_active ? kZephyrusHeroEnterDuration
                               : kZephyrusHeroExitDuration));
    if (zephyrus_active) {
      zephyrus_hero_animation_.Show();
    } else {
      zephyrus_hero_animation_.Hide();
    }
    // Keep the content aligned to the pill even if animations are disabled.
    UpdateZephyrusContentInset();
  }

  // Reapply the dim color to account for the highlight state. Zephyrus: hover
  // shares the selected row's highlight, so it also uses the selected dim color.
  const ui::ColorId dimmed_id = zephyrus_active
                                    ? kColorOmniboxResultsTextDimmedSelected
                                    : kColorOmniboxResultsTextDimmed;
  suggestion_view_->separator()->ApplyTextColor(dimmed_id);

  // Recreate the icons in case the color needs to change.
  // Note: if this is an extension icon or favicon then this can be done in
  //       SetMatch() once (rather than repeatedly, as happens here). There may
  //       be an optimization opportunity here.
  auto icon = GetIcon();
  if (icon.IsEmpty()) {
    suggestion_view_->ClearIcon();
  } else {
    suggestion_view_->SetIcon(*icon.ToImageSkia(), match_);
  }

  // We must reapply colors for all the text fields here. If we don't, we can
  // break theme changes for ZeroSuggest. See https://crbug.com/40135721.
  //
  // TODO(crbug.com/430318151): We should finish migrating this logic to live
  // entirely within OmniboxTextView, which should keep track of its own
  // OmniboxPart.
  if (match_.type == AutocompleteMatchType::NULL_RESULT_MESSAGE) {
    suggestion_view_->content()->ApplyTextColor(
        match_.IsIphSuggestion() || match_.IsToolbelt()
            ? kColorOmniboxResultsTextDimmed
            : kColorOmniboxText);
  } else if (force_reapply_styles || PrefersHighContrast(this)) {
    // Normally, OmniboxTextView caches its appearance, but in high contrast,
    // selected-ness changes the text colors, so the styling of the text part of
    // the results needs to be recomputed.
    suggestion_view_->content()->ReapplyStyling();
    suggestion_view_->description()->ReapplyStyling();
  }

  button_row_->SetThemeState(GetThemeState());

  // Zephyrus: the selected result is indicated by the white "hero" pill, so
  // Chromium's blue focus bar is redundant. It also lives at the row's left
  // edge — out in the transparent gutter, detached from the pinned content —
  // where it reads as a stray chip. Keep it permanently hidden.
  selection_indicator_->SetVisible(false);

  if (suggestion_view_->iph_link_view()->GetVisible()) {
    views::FocusRing::Get(suggestion_view_->iph_link_view())->Refresh();
  }
}

void OmniboxResultView::OnSelectionStateChanged() {
  UpdateDividerLineVisibility();
  UpdateSecondaryTextVisibility();
  UpdateFeedbackButtonsVisibility();
  UpdateRemoveSuggestionVisibility();
  UpdateAccessibleName();
  UpdateAccessibilitySelectedState();
  ApplyThemeAndRefreshIcons();
  button_row_->SelectionStateChanged();
}

bool OmniboxResultView::GetMatchSelected() const {
  const auto selection = popup_view_->GetSelection();
  return selection.line == model_index_;
}

views::Button* OmniboxResultView::GetActiveAuxiliaryButtonForAccessibility() {
  return const_cast<views::Button*>(
      std::as_const(*this).GetActiveAuxiliaryButtonForAccessibility());
}

const views::Button*
OmniboxResultView::GetActiveAuxiliaryButtonForAccessibility() const {
  if (popup_view_->GetSelection().state ==
      OmniboxPopupSelection::FOCUSED_BUTTON_THUMBS_UP) {
    return thumbs_up_button_;
  } else if (popup_view_->GetSelection().state ==
             OmniboxPopupSelection::FOCUSED_BUTTON_THUMBS_DOWN) {
    return thumbs_down_button_;
  } else if (popup_view_->GetSelection().state ==
             OmniboxPopupSelection::FOCUSED_BUTTON_REMOVE_SUGGESTION) {
    return remove_suggestion_button_;
  }

  return button_row_->GetActiveButton();
}

OmniboxPartState OmniboxResultView::GetThemeState() const {
  // NULL_RESULT_MESSAGE matches are no-op suggestions that only deliver a
  // message. The selected and hovered states imply an action can be taken from
  // that suggestion, so do not allow those states for this result.
  if (match_.type == AutocompleteMatchType::NULL_RESULT_MESSAGE) {
    if (match_.IsToolbelt()) {
      return OmniboxPartState::TOOLBELT;
    }
    return match_.IsIphSuggestion() ? OmniboxPartState::IPH
                                    : OmniboxPartState::NORMAL;
  }

  if (GetMatchSelected()) {
    return OmniboxPartState::SELECTED;
  }

  // If we don't highlight the whole row when the user has the mouse over the
  // remove suggestion button, it's unclear which suggestion is being removed.
  return IsMouseHovered() ? OmniboxPartState::HOVERED
                          : OmniboxPartState::NORMAL;
}

void OmniboxResultView::OnMatchIconUpdated() {
  // The new icon will be fetched during ApplyThemeAndRefreshIcons().
  ApplyThemeAndRefreshIcons();
}

void OmniboxResultView::SetRichSuggestionImage(const gfx::ImageSkia& image) {
  suggestion_view_->SetImage(image, match_);
}

void OmniboxResultView::ButtonPressed(OmniboxPopupSelection::LineState state,
                                      const ui::Event& event) {
  popup_view_->controller()->edit_model()->OpenSelection(
      OmniboxPopupSelection(model_index_, state), event.time_stamp(),
      WindowOpenDisposition::CURRENT_TAB,
      /*via_keyboard=*/event.IsKeyEvent());
  if (state == OmniboxPopupSelection::FOCUSED_BUTTON_REMOVE_SUGGESTION) {
    // The button could be pressed and the deletion successful, but the match
    // may continue to appear with the X button remaining so it looked like it
    // didn't delete. There may be a deeper async matches issue involved, but
    // this seems to help in at least some cases (pedals + entities, e.g. dino).
    UpdateRemoveSuggestionVisibility();
  }
}

////////////////////////////////////////////////////////////////////////////////
// OmniboxResultView, views::View overrides:

bool OmniboxResultView::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyLeftMouseButton()) {
    popup_view_->SetSelectedIndex(model_index_);
    // Inform the model that a new result is now selected via mouse press.
    popup_view_->controller()->edit_model()->OnNavigationLikely(
        model_index_, omnibox::mojom::NavigationPredictor::kMouseDown);
  }
  return true;
}

bool OmniboxResultView::OnMouseDragged(const ui::MouseEvent& event) {
  if (HitTestPoint(event.location())) {
    // When the drag enters or remains within the bounds of this view, either
    // set the state to be selected or hovered, depending on the mouse button.
    if (event.IsOnlyLeftMouseButton()) {
      if (!GetMatchSelected()) {
        popup_view_->SetSelectedIndex(model_index_);
      }
    } else {
      UpdateHoverState();
    }
    return true;
  }

  // When the drag leaves the bounds of this view, cancel the hover state and
  // pass control to the popup view.
  UpdateHoverState();
  SetMouseAndGestureHandler(popup_view_);
  return false;
}

void OmniboxResultView::OnMouseReleased(const ui::MouseEvent& event) {
  if (event.IsOnlyMiddleMouseButton() || event.IsOnlyLeftMouseButton()) {
    const auto disposition = event.IsOnlyLeftMouseButton()
                                 ? WindowOpenDisposition::CURRENT_TAB
                                 : WindowOpenDisposition::NEW_BACKGROUND_TAB;
    popup_view_->controller()->edit_model()->OpenSelection(
        OmniboxPopupSelection(model_index_), event.time_stamp(), disposition);
  }
}

void OmniboxResultView::OnMouseEntered(const ui::MouseEvent& event) {
  UpdateHoverState();
}

void OmniboxResultView::OnMouseExited(const ui::MouseEvent& event) {
  UpdateHoverState();
}

void OmniboxResultView::OnThemeChanged() {
  views::View::OnThemeChanged();
  ApplyThemeAndRefreshIcons(/*force_reapply_styles=*/true);
}

void OmniboxResultView::UpdateAccessibilityProperties() {
  GetViewAccessibility().SetSetSize(
      popup_view_->controller()->autocomplete_controller()->result().size());
}

////////////////////////////////////////////////////////////////////////////////
// OmniboxResultView, private:

void OmniboxResultView::OpenIphLink() {
  popup_view_->controller()->client()->OpenIphLink(match_.iph_link_url);
}

gfx::Image OmniboxResultView::GetIcon() const {
  // Zephyrus (Figma searchbar_resultdropdown): search suggestions all use the
  // one magnifier glyph (no history clocks), for a clean, consistent list.
  // URL / navigation rows keep their favicon so sites stay recognizable.
  if (AutocompleteMatch::IsSearchType(match_.type) &&
      !match_.answer_template.has_value()) {
    const SkColor color = GetThemedColor(
        GetMatchSelected() ? kColorOmniboxResultsIconSelected
                           : kColorOmniboxResultsIcon);
    return gfx::Image(gfx::CreateVectorIcon(kZephyrusSearchIcon, 14, color));
  }

  // Usually, use kColorOmniboxResultsIcon[Selected] for icon color. Except for
  // history cluster suggestions which want to stand out. They reuse the
  // kColorOmniboxResultsUrl[Selected] color which is intended for the URL text
  // in suggestion texts.
  ui::ColorId vector_icon_color_id;
  if (match_.type == AutocompleteMatchType::STARTER_PACK ||
      match_.type == AutocompleteMatchType::FEATURED_ENTERPRISE_SEARCH) {
    vector_icon_color_id = kColorOmniboxResultsStarterPackIcon;
  } else if (match_.type == AutocompleteMatchType::HISTORY_CLUSTER ||
             match_.type == AutocompleteMatchType::PEDAL) {
    vector_icon_color_id = kColorOmniboxAnswerIconGM3Foreground;
  } else {
    vector_icon_color_id = GetMatchSelected() ? kColorOmniboxResultsIconSelected
                                              : kColorOmniboxResultsIcon;
  }

  return popup_view_->GetMatchIcon(match_,
                                   GetThemedColor(vector_icon_color_id));
}

void OmniboxResultView::UpdateHoverState() {
  UpdateDividerLineVisibility();
  UpdateSecondaryTextVisibility();
  UpdateFeedbackButtonsVisibility();
  UpdateRemoveSuggestionVisibility();
  ApplyThemeAndRefreshIcons();
  GetViewAccessibility().SetIsHovered(IsMouseHovered());
}

void OmniboxResultView::UpdateDividerLineVisibility() {
  const bool old_visibility = divider_line_->GetVisible();
  const bool new_visibility = match_.IsToolbelt();

  divider_line_->SetVisible(new_visibility);

  if (old_visibility != new_visibility) {
    InvalidateLayout();
  }
}

void OmniboxResultView::UpdateSecondaryTextVisibility() {
  const bool is_contextual = match_.IsContextualSearchSuggestion();

  const bool show_description =
      !is_contextual || GetMatchSelected() || IsMouseHovered();
  if (suggestion_view_->description()->GetVisible() != show_description) {
    suggestion_view_->description()->SetVisible(show_description);
    suggestion_view_->separator()->SetVisible(show_description);
    // Explicitly notify the parent that the preferred size has changed, as
    // the row's FlexLayout depends on this to allocate space.
    suggestion_view_->OnSecondaryTextVisibilityChanged();
  }
}

void OmniboxResultView::UpdateFeedbackButtonsVisibility() {
  const bool old_visibility = thumbs_up_button_->GetVisible();
  const bool new_visibility =
      popup_view_->controller()->edit_model()->IsPopupControlPresentOnMatch(
          OmniboxPopupSelection(
              model_index_, OmniboxPopupSelection::FOCUSED_BUTTON_THUMBS_UP)) &&
      (GetMatchSelected() || IsMouseHovered());

  // Same rules apply to both buttons.
  thumbs_up_button_->SetVisible(new_visibility);
  thumbs_down_button_->SetVisible(new_visibility);

  if (old_visibility != new_visibility) {
    InvalidateLayout();
  }
}

// TODO(b/345536738): Introduce a single UpdateButtonsVisibility() that iterates
//  over all the buttons and updates their visibilities.
void OmniboxResultView::UpdateRemoveSuggestionVisibility() {
  const bool old_visibility = remove_suggestion_button_->GetVisible();
  // Zephyrus (Figma searchbar_resultdropdown): clean rows never show the "✕"
  // remove-suggestion affordance. (Deleting a suggestion is still available via
  // Shift+Delete, which the backend handles.)
  const bool new_visibility = false;

  remove_suggestion_button_->SetVisible(new_visibility);

  if (old_visibility != new_visibility) {
    InvalidateLayout();
  }
}

void OmniboxResultView::UpdateAccessibilitySelectedState() {
  GetViewAccessibility().SetIsSelected(GetMatchSelected());
}

void OmniboxResultView::UpdateAccessibleName() {
  // Get the label without the ", n of m" positional text appended.
  // The positional info is provided via
  // ax::mojom::IntAttribute::kPosInSet/SET_SIZE and providing it via text as
  // well would result in duplicate announcements.

  const auto* autocomplete_controller =
      popup_view_->controller()->autocomplete_controller();

  // TODO(tommycli): We re-fetch the original match from the popup model,
  // because |match_| already has its contents and description swapped by this
  // class, and we don't want that for the bubble. We should improve this.
  const bool is_selected = GetMatchSelected();
  if (model_index_ < autocomplete_controller->result().size()) {
    const auto raw_match =
        autocomplete_controller->result().match_at(model_index_);
    // The selected match can have a special name, e.g. when is one or more
    // buttons that can be tabbed to.
    std::u16string label;
    if (is_selected) {
      // The selected match can have a special name, e.g. when is one or more
      // buttons that can be tabbed to.
      label = popup_view_->controller()
                  ->edit_model()
                  ->GetPopupAccessibilityLabelForCurrentSelection(
                      raw_match.contents, false);

      // If the line immediately after the current selection is the
      // informational IPH row, append its accessibility label at the end of
      // this selection's accessibility label.
      label += popup_view_->controller()
                   ->edit_model()
                   ->MaybeGetPopupAccessibilityLabelForIPHSuggestion();
    } else {
      label = AutocompleteMatchType::ToAccessibilityLabel(
          raw_match,
          popup_view_->controller()->edit_model()->GetSuggestionGroupHeaderText(
              raw_match.suggestion_group_id),
          raw_match.contents);
    }
    GetViewAccessibility().SetName(label);
  }
}

////////////////////////////////////////////////////////////////////////////////
// OmniboxResultView, views::View overrides, private:

void OmniboxResultView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  // Zephyrus: the card's edges move within this row's coordinates when the row
  // is repositioned/resized, so re-anchor the content to them.
  UpdateZephyrusContentInset();
  InvalidateLayout();
}

void OmniboxResultView::OnPaintBackground(gfx::Canvas* canvas) {
  // Paint the static Background first (hovered grey pill / IPH / high-contrast)
  // so it sits behind the row content and behind the animated hero pill.
  views::View::OnPaintBackground(canvas);

  const double progress = zephyrus_hero_animation_.GetCurrentValue();
  if (progress <= 0.0) {
    return;
  }

  // Zephyrus width-jump: the pill grows from the card's own edges out to its
  // full hero width, overhanging the card by kZephyrusHeroOverhang per side.
  // Anchoring to the card (rather than to this row's bounds) keeps the overhang
  // exactly equal on both sides. The icon/text track it via
  // UpdateZephyrusContentInset, so the expanded hero stays properly filled.
  gfx::RectF rect;
  float card_left = 0.f;
  float card_right = 0.f;
  if (GetZephyrusCardEdges(&card_left, &card_right)) {
    const float overhang = ZephyrusOverhang(card_left, card_right);
    rect = gfx::RectF(card_left - overhang, 2.f,
                      (card_right - card_left) + 2.f * overhang,
                      static_cast<float>(height()) - 4.f);
  } else {
    const float inset = ZephyrusPillInset();
    rect = gfx::RectF(GetLocalBounds());
    rect.Inset(gfx::InsetsF::TLBR(2.f, inset, 2.f, inset));
  }

  const SkColor color =
      GetThemedColor(GetOmniboxBackgroundColorId(OmniboxPartState::SELECTED));
  PaintZephyrusHeroPill(canvas, rect, color, kZephyrusHeroPillRadius,
                        static_cast<float>(progress));
}

bool OmniboxResultView::GetZephyrusCardEdges(float* left, float* right) const {
  const views::Widget* widget = GetWidget();
  if (!widget) {
    return false;
  }
  const auto* frame = views::AsViewClass<RoundedOmniboxResultsFrame>(
      widget->GetContentsView());
  if (!frame) {
    return false;
  }
  const gfx::Rect card = frame->GetZephyrusCardBounds();
  if (card.IsEmpty()) {
    return false;
  }
  // Express the card in this row's coordinates so the pill can be anchored to
  // it symmetrically, whatever the row's own offset happens to be.
  const gfx::RectF card_in_row =
      views::View::ConvertRectToTarget(frame, this, gfx::RectF(card));
  *left = card_in_row.x();
  *right = card_in_row.right();
  return true;
}

float OmniboxResultView::ZephyrusOverhang(float card_left,
                                          float card_right) const {
  // Only overhang as far as BOTH sides can afford inside this row. A row's
  // bounds can be offset from the card, and OnPaintBackground is clipped to
  // those bounds — so taking the tighter side keeps the pill symmetric and
  // keeps its rounded corners from being sliced off at the row's edge.
  const float room = std::min(card_left, static_cast<float>(width()) - card_right);
  const float max_overhang = std::clamp(
      room, 0.f,
      static_cast<float>(RoundedOmniboxResultsFrame::kZephyrusHeroOverhang));
  return max_overhang *
         static_cast<float>(zephyrus_hero_animation_.GetCurrentValue());
}

float OmniboxResultView::ZephyrusPillInset() const {
  return gfx::Tween::FloatValueBetween(
      zephyrus_hero_animation_.GetCurrentValue(),
      static_cast<float>(ZephyrusCardInset()),
      static_cast<float>(ZephyrusHeroPillInset()));
}

void OmniboxResultView::UpdateZephyrusContentInset() {
  if (!local_answer_header_and_suggestion_and_buttons_) {
    return;
  }
  int inset_left = 0;
  int inset_right = 0;
  float card_left = 0.f;
  float card_right = 0.f;
  if (GetZephyrusCardEdges(&card_left, &card_right)) {
    // Content sits at the pill's own edge, so the expanded hero stays filled.
    const float overhang = ZephyrusOverhang(card_left, card_right);
    inset_left = base::ClampRound(card_left - overhang);
    inset_right = base::ClampRound(width() - (card_right + overhang));
  } else {
    inset_left = inset_right = base::ClampRound(ZephyrusPillInset());
  }
  const gfx::Insets insets =
      gfx::Insets::TLBR(0, std::max(0, inset_left), 0, std::max(0, inset_right));
  // Re-bordering forces a layout pass, so skip frames where the rounded inset
  // hasn't actually moved — this runs on every tick of the width-jump.
  const views::Border* existing =
      local_answer_header_and_suggestion_and_buttons_->GetBorder();
  if (existing && existing->GetInsets() == insets) {
    return;
  }
  local_answer_header_and_suggestion_and_buttons_->SetBorder(
      views::CreateEmptyBorder(insets));
}

void OmniboxResultView::AnimationProgressed(const gfx::Animation* animation) {
  if (animation == &zephyrus_hero_animation_) {
    UpdateZephyrusContentInset();
    SchedulePaint();
  }
}

void OmniboxResultView::AnimationEnded(const gfx::Animation* animation) {
  if (animation == &zephyrus_hero_animation_) {
    UpdateZephyrusContentInset();
    SchedulePaint();
  }
}

////////////////////////////////////////////////////////////////////////////////
// OmniboxResultView, overrides, private:

DEFINE_ENUM_CONVERTERS(OmniboxPartState,
                       {OmniboxPartState::NORMAL, u"NORMAL"},
                       {OmniboxPartState::HOVERED, u"HOVERED"},
                       {OmniboxPartState::SELECTED, u"SELECTED"})

BEGIN_METADATA(OmniboxResultView)
ADD_READONLY_PROPERTY_METADATA(bool, MatchSelected)
ADD_READONLY_PROPERTY_METADATA(OmniboxPartState, ThemeState)
ADD_READONLY_PROPERTY_METADATA(gfx::Image, Icon)
END_METADATA
