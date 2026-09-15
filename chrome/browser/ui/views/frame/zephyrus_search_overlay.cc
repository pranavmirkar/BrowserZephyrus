// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_search_overlay.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/autocomplete/autocomplete_classifier_factory.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/top_sites_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_provider_client.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/common/url_constants.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_partition.h"
#include "chrome/browser/ui/views/frame/zephyrus_search_engine_picker.h"
#include "components/favicon/core/favicon_service.h"
#include "components/history/core/browser/top_sites.h"
#include "components/vector_icons/vector_icons.h"
#include "components/favicon_base/favicon_types.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/transform_util.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "cc/paint/paint_flags.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/widget/widget.h"

namespace {

// Geometry from the "spotlight-panel" component in the Zephyrus Figma
// (node 142:295). Colours are the design's own here — unlike the context-menu
// pass, this component was drawn in the product's dark palette, so its values
// are used directly.
//
//   panel   28px padding, 20px gap, radius 24
//   field   52px tall, radius 26, 16px horizontal padding, 12px gap
//   engine  radius 14, 10x6 padding, 6px gap, 18px circular favicon
//   chips   light: white 90%, radius 14, 14x8 padding, 8px gap, 8px brand dot
constexpr int kContentWidth = 628;
constexpr int kPanelPadding = 28;
constexpr int kPanelGap = 20;
// No kPanelRadius: the card has no background to round. The design's 24px
// panel radius belongs to a surface that does not exist here.

constexpr int kFieldHeight = 52;
// Pressable -> pill. Was 26, a mid-range value the binary rule removes.
constexpr int kFieldRadius = zephyrus::kPillRadius;
constexpr int kFieldHPadding = 16;
constexpr int kFieldGap = 12;
// Figma specifies a background-blur of 25, paired with the 60% fill. Dialled
// back to 15 on 2026-08-10 after seeing it in the product: 25 over live web
// content smeared the page into an indistinct wash, where 15 still reads as
// glass but keeps some sense of what is behind the card.
constexpr float kFieldBlurSigma = 15.0f;

constexpr int kEngineRadius = zephyrus::kPillRadius;
constexpr int kEngineFaviconSize = 18;
constexpr int kSearchGlyphSize = 17;
constexpr int kDividerHeight = 24;

constexpr int kChipRadius = zephyrus::kPillRadius;
constexpr int kChipFaviconSize = 16;
constexpr int kMaxChips = 5;
constexpr int kChipsInset = 76;
constexpr int kChipsGap = 8;

// ---- suggestion list, Material 3 -----------------------------------------
//
// MD3's list item, not a compact omnibox dropdown: a 56dp row, a 24dp leading
// icon, and a state layer for the highlight instead of a selection colour.
// Those three are what make a list read as Material rather than as a menu.
// 56 for a single line, 72 once a row carries a supporting line under its
// headline -- MD3's two-line list item.
constexpr int kRowHeight = 56;
constexpr int kRowHeightTwoLine = 72;
// The sheet sits close under the bar. MD3's docked search reads as one control
// in two parts, and kPanelGap (20) is the gap between unrelated blocks.
constexpr int kListGap = 8;
constexpr int kRowIconSize = 24;
constexpr int kRowHPadding = 16;
constexpr int kRowGap = 16;
// MD3 surfaces are rounded at the CONTAINER, and its rows square -- the corners
// belong to the sheet, not to each item.
//
// Rule 2 does NOT apply: the panel behind this has no background to round (see
// the note on kPanelPadding), so the list is a standalone floating sheet rather
// than a shape nested in another. It therefore CHOOSES its radius, and takes
// the sheet step from the scale instead of repeating the number.
constexpr int kListRadius = zephyrus::kRadiusPopup;
constexpr int kMaxRows = 6;
// MD3 state-layer opacities live in SuggestionRow::ApplyStateLayer, which is
// the only thing that can reconcile hover, press and keyboard selection.

// Motion. Ctrl+T is keyboard-initiated and used dozens of times a day, which
// by the usual rule argues for NO animation at all — a command surface that
// makes you wait is worse than one that snaps. So this is deliberately at the
// short end: long enough to see where the card came from, short enough that it
// never gates typing.
//
// Exit is faster than entry: the user has already decided, and the system
// should get out of the way immediately.
constexpr base::TimeDelta kEnterDuration = base::Milliseconds(140);
constexpr base::TimeDelta kExitDuration = base::Milliseconds(90);

// Never from scale(0) — nothing appears from nothing. 0.96 is enough to read as
// arriving without looking like a zoom. The exit barely shrinks (0.98): a card
// collapsing on its way out draws more attention than it deserves.
constexpr float kEnterScale = 0.96f;
constexpr float kExitScale = 0.98f;

// Centred, not origin-anchored: this is a spotlight surface with no trigger to
// grow from, which is the same reason modals scale from their centre.

// FALLBACK ONLY. The row is normally the profile's most-visited sites; this
// stands in on a fresh profile, where there is no history to rank yet and an
// empty row would look broken.
struct Shortcut {
  const char* label;
  const char* url;
};
constexpr Shortcut kFallbackShortcuts[] = {
    {"YouTube", "https://www.youtube.com"},
    {"GitHub", "https://github.com"},
    {"Gmail", "https://mail.google.com"},
    {"ChatGPT", "https://chatgpt.com"},
    {"IRCTC", "https://www.irctc.co.in"},
};

// Scale about the card's own centre. A spotlight surface has no trigger to
// grow out of, so centre is correct here — unlike a popover, which should scale
// from whatever opened it.
gfx::Transform ScaleAboutCenter(views::View* view, float scale) {
  return gfx::GetScaleTransform(gfx::Rect(view->size()).CenterPoint(), scale);
}

// Hover/press feedback is painted as the chip's OWN fill rather than with an
// ink drop. InkDropHost inserts its layer with LayerRegion::kBelow — under the
// host's painted background — which on these glass chips put a 10% white wash
// beneath a 60%-opaque navy fill: smothered where it overlapped the chip and
// visible only where it spilled past it, at neither the chip's bounds nor its
// radius. A background swap cannot be mispositioned, and it lands above the
// backdrop blur where the design puts it.
//
// The swap is instant by design. Hover is the one moment where any delay reads
// as the control not having noticed the pointer, so this is the same call as
// highlighting a button on press rather than on release.
struct ChipFills {
  SkColor normal;
  SkColor hovered;
  SkColor pressed;
};

// The shortcut chips.
//
// These were hardcoded NAVY (#1B2038 / #14182B) and faint WHITE alphas, left
// over from the original dark theme -- they survived the move to the warm
// palette untouched because nothing referenced the theme constant, so nothing
// broke loudly. On a light ground they were a dark blue chip; on the monochrome
// palette they would still be. Both now derive from the live palette, which is
// also why they can no longer be constexpr.
//
// The chip comes FORWARD on hover rather than being washed lighter, so the row
// keeps one material instead of gaining a second.
ChipFills GlassFills() {
  const SkColor base = zephyrus::Ground();
  return {
      SkColorSetA(base, 0x99),
      SkColorSetA(zephyrus::Raise(base, 0x28), 0xB8),
      SkColorSetA(zephyrus::Raise(base, 0x3C), 0xB8),
  };
}

// The engine chip sits inside the field on that same material, so its resting
// state is a faint lift of the ink and hover simply deepens it.
ChipFills EngineFills() {
  const SkColor ink = zephyrus::Ink();
  return {
      SkColorSetA(ink, 0x0A),
      SkColorSetA(ink, 0x1C),
      SkColorSetA(ink, 0x28),
  };
}

SkColor FillForState(const ChipFills& fills, views::Button::ButtonState state) {
  switch (state) {
    case views::Button::STATE_PRESSED:
      return fills.pressed;
    case views::Button::STATE_HOVERED:
      return fills.hovered;
    default:
      return fills.normal;
  }
}

// The engine selector. views::Button is subclassed rather than using
// LabelButton because LabelButton hard-codes image-then-text, and the design
// puts the chevron after the engine name.
// One suggestion row.
//
// A BUTTON, not a plain View, and that is the whole reason mouse input works:
// the overlay is a full-window scrim that dismisses on any click reaching it,
// so a row that does not consume its own click hands it to the scrim and the
// card closes instead of opening the result. Keyboard worked because it never
// went near the scrim.
//
// It also carries MD3's state layers -- hover 8%, press 12%, selected 12% --
// which a plain View has no state to express.
class SuggestionRow : public views::Button {
  METADATA_HEADER(SuggestionRow, views::Button)

 public:
  using ActivateCallback = base::RepeatingCallback<void(const GURL&)>;

  explicit SuggestionRow(ActivateCallback on_activate)
      : views::Button(base::BindRepeating(&SuggestionRow::Activate,
                                          base::Unretained(this))),
        on_activate_(std::move(on_activate)) {
    // The row is the full-bleed width of the sheet; the sheet owns the corners.
    SetAnimateOnStateChange(false);

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, kRowHPadding), kRowGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    icon_ = AddChildView(std::make_unique<views::ImageView>());
    icon_->SetImageSize(gfx::Size(kRowIconSize, kRowIconSize));
    icon_->SetPreferredSize(gfx::Size(kRowIconSize, kRowIconSize));
    icon_->SetCanProcessEventsWithinSubtree(false);

    auto* column = AddChildView(std::make_unique<views::View>());
    column->SetCanProcessEventsWithinSubtree(false);
    column->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    layout->SetFlexForView(column, 1);

    headline_ = column->AddChildView(std::make_unique<views::Label>());
    headline_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    headline_->SetAutoColorReadabilityEnabled(false);
    // The card paints to a translucent blurred layer, and subpixel text AA
    // needs an opaque backing. Views DCHECKs on this in debug builds.
    headline_->SetSubpixelRenderingEnabled(false);
    headline_->SetElideBehavior(gfx::ELIDE_TAIL);

    supporting_ = column->AddChildView(std::make_unique<views::Label>());
    supporting_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    supporting_->SetAutoColorReadabilityEnabled(false);
    supporting_->SetSubpixelRenderingEnabled(false);
    supporting_->SetElideBehavior(gfx::ELIDE_TAIL);
    supporting_->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kBodySmall));
    supporting_->SetVisible(false);

    ApplyStateLayer();
  }

  // Returns true when the destination changed, i.e. the caller owes this row a
  // fresh favicon. Rows are UPDATED rather than rebuilt so the one under the
  // cursor survives: results arrive repeatedly as providers report in, and
  // destroying the hovered row each time is what made the highlight blink.
  bool SetMatch(const AutocompleteMatch& match, bool is_search) {
    const bool two_line = !is_search && !match.description.empty();
    headline_->SetText(two_line ? match.description
                                : (match.contents.empty() ? match.description
                                                          : match.contents));
    headline_->SetEnabledColor(zephyrus::Ink());
    supporting_->SetVisible(two_line);
    if (two_line) {
      supporting_->SetText(match.contents);
      supporting_->SetEnabledColor(zephyrus::Muted());
    }
    SetPreferredSize(
        gfx::Size(kContentWidth, two_line ? kRowHeightTwoLine : kRowHeight));

    const bool changed = destination_ != match.destination_url;
    destination_ = match.destination_url;
    if (changed) {
      // Generic glyph immediately; the favicon replaces it if one arrives.
      icon_->SetImage(ui::ImageModel::FromVectorIcon(
          is_search ? vector_icons::kSearchIcon : vector_icons::kGlobeIcon,
          zephyrus::Muted(), kRowIconSize));
    }
    return changed;
  }

  views::ImageView* icon() { return icon_; }
  const GURL& destination() const { return destination_; }
  SuggestionRow(const SuggestionRow&) = delete;
  SuggestionRow& operator=(const SuggestionRow&) = delete;
  ~SuggestionRow() override = default;

  // Keyboard selection. Held separately from the mouse state because both can
  // be true at once and the row must not flicker when they disagree.
  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    ApplyStateLayer();
  }

  // views::Button:
  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    ApplyStateLayer();
  }

 private:
  void ApplyStateLayer() {
    // M3 state-layer opacities, from zephyrus::m3. These were hand-written
    // (12% press against the spec's 10%) and drifted from the same values in
    // the settings popup and the dialog buttons -- three surfaces, three
    // answers to one question.
    const ButtonState state = GetState();
    SkAlpha alpha = 0;
    if (state == STATE_PRESSED) {
      alpha = zephyrus::m3::kPressed;
    } else if (selected_) {
      // Keyboard selection is not a pointer state, so it takes the focus
      // opacity rather than borrowing press.
      alpha = zephyrus::m3::kFocus;
    } else if (state == STATE_HOVERED) {
      alpha = zephyrus::m3::kHover;
    }
    SetBackground(alpha ? views::CreateSolidBackground(
                              SkColorSetA(zephyrus::Ink(), alpha))
                        : nullptr);
    SchedulePaint();
  }

  void Activate() {
    if (destination_.is_valid()) {
      on_activate_.Run(destination_);
    }
  }

  ActivateCallback on_activate_;
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> headline_ = nullptr;
  raw_ptr<views::Label> supporting_ = nullptr;
  GURL destination_;
  bool selected_ = false;
};

BEGIN_METADATA(SuggestionRow)
END_METADATA

class EngineChip : public views::Button {
  METADATA_HEADER(EngineChip, views::Button)

 public:
  explicit EngineChip(PressedCallback callback)
      : views::Button(std::move(callback)) {
    ApplyFill();
  }
  EngineChip(const EngineChip&) = delete;
  EngineChip& operator=(const EngineChip&) = delete;
  ~EngineChip() override = default;

  // views::Button:
  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    ApplyFill();
  }

 private:
  void ApplyFill() {
    SetBackground(views::CreateRoundedRectBackground(
        FillForState(EngineFills(), GetState()), kEngineRadius));
  }
};

BEGIN_METADATA(EngineChip)
END_METADATA

// A shortcut chip: the same glass as the field above it, and the same
// fill-swap hover as the engine chip.
class GlassChip : public views::LabelButton {
  METADATA_HEADER(GlassChip, views::LabelButton)

 public:
  GlassChip(PressedCallback callback, const std::u16string& text)
      : views::LabelButton(std::move(callback), text) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(kChipRadius));
    layer()->SetBackgroundBlur(kFieldBlurSigma);
    // LabelButton owns an internal Label with subpixel antialiasing on by
    // default, and the layer above is deliberately non-opaque so the blur
    // shows through. views::Label DCHECKs on that pair, and without DCHECKs it
    // renders colour-fringed text over transparency. Every chip in the row is
    // one of these, so this is the same defect as engine_label_ multiplied.
    label()->SetSubpixelRenderingEnabled(false);
    ApplyFill();
  }
  GlassChip(const GlassChip&) = delete;
  GlassChip& operator=(const GlassChip&) = delete;
  ~GlassChip() override = default;

  // views::LabelButton:
  void StateChanged(ButtonState old_state) override {
    views::LabelButton::StateChanged(old_state);
    ApplyFill();
  }

 private:
  void ApplyFill() {
    SetBackground(views::CreateRoundedRectBackground(
        FillForState(GlassFills(), GetState()), kChipRadius));
  }
};

BEGIN_METADATA(GlassChip)
END_METADATA

}  // namespace

ZephyrusSearchOverlay::ZephyrusSearchOverlay(BrowserView* browser_view)
    : browser_view_(browser_view) {
  // `this` is a full-window scrim: invisible, but it catches the click that
  // dismisses the card, and it keeps the card above the web contents. It needs
  // its own layer to composite over the contents layer at all.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetVisible(false);

  // The card. Nothing is drawn behind it — the field pill and the chips float,
  // exactly as drawn; the dark rectangle in the Figma frame is artboard, not a
  // surface.
  panel_ = AddChildView(std::make_unique<views::View>());
  // Its own layer so entry/exit animate on the compositor (transform and
  // opacity only — both skip layout and paint).
  panel_->SetPaintToLayer();
  panel_->layer()->SetFillsBoundsOpaquely(false);
  panel_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(kPanelPadding),
      kPanelGap));
  views::View* const container = panel_;

  // ---- search-input-field: one pill holding engine selector, divider, glyph
  // and the text field. The engine selector lives INSIDE the field because it
  // qualifies the query being typed, not the panel as a whole.
  auto* field = container->AddChildView(std::make_unique<views::View>());
  // 60% navy, exactly as drawn. Pranav chose the design value over a more
  // opaque one on 2026-08-10, having seen it float: the panel has no card
  // behind it, so the field tints the page rather than covering it. Note this
  // is the one place a light page can wash the pill out, which the backdrop
  // blur below is what saves.
  //
  // Fill at 60% PLUS a backdrop blur of 25 — the two together, as in the
  // design. Either alone is wrong: the fill without blur is a washed-out tint,
  // and blur behind an opaque fill is invisible, which is exactly why the
  // sidebar's blur reads as flat today.
  //
  // This works only because this view lives in the browser window. A backdrop
  // filter samples its own compositor frame, so here it sees the web contents;
  // as a separate bubble widget there was nothing behind it at all — the same
  // reason DWM window backdrops failed for menus.
  field->SetPaintToLayer();
  field->layer()->SetFillsBoundsOpaquely(false);
  field->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(kFieldRadius));
  field->layer()->SetBackgroundBlur(kFieldBlurSigma);
  field->SetBackground(views::CreateRoundedRectBackground(
      SkColorSetA(zephyrus::Ground(), 0x99), kFieldRadius));
  field->SetBorder(views::CreateRoundedRectBorder(
      1, kFieldRadius, SkColorSetARGB(0x08, 0x00, 0x00, 0x00)));
  field->SetPreferredSize(gfx::Size(kContentWidth, kFieldHeight));
  auto* field_layout =
      field->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kFieldHPadding), kFieldGap));
  field_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // search-engine-selector: favicon, name, chevron — in that order.
  auto engine = std::make_unique<EngineChip>(base::BindRepeating(
      &ZephyrusSearchOverlay::ShowEnginePicker, base::Unretained(this)));
  engine->SetBorder(views::CreateRoundedRectBorder(
      1, kEngineRadius, SkColorSetARGB(0x08, 0x00, 0x00, 0x00)));
  auto* engine_layout =
      engine->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(6, 10), 6));
  engine_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  engine_favicon_ =
      engine->AddChildView(std::make_unique<views::ImageView>());
  engine_favicon_->SetImageSize(
      gfx::Size(kEngineFaviconSize, kEngineFaviconSize));

  engine_label_ = engine->AddChildView(std::make_unique<views::Label>());
  engine_label_->SetEnabledColor(SkColorSetA(zephyrus::Ink(), 0xE6));
  engine_label_->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelLarge));
  // Subpixel antialiasing needs an opaque backing to blend against, and this
  // label sits inside `field`, whose layer is deliberately NOT opaque so the
  // glass effect works. views::Label DCHECKs on exactly that combination, and
  // in a release build it silently produces colour-fringed text over
  // transparency instead. Greyscale AA is the correct rendering here.
  engine_label_->SetSubpixelRenderingEnabled(false);

  engine_chevron_ = engine->AddChildView(std::make_unique<views::ImageView>(
      ui::ImageModel::FromVectorIcon(kZephyrusDropdownIcon, zephyrus::Ink(),
                                     10)));
  engine_chevron_->SetImageSize(gfx::Size(10, 10));
  // Its own layer so the open/close flip can be a transform rather than a
  // second icon asset.
  engine_chevron_->SetPaintToLayer();
  engine_chevron_->layer()->SetFillsBoundsOpaquely(false);

  engine_chip_ = field->AddChildView(std::move(engine));

  // engine-divider
  auto* divider = field->AddChildView(std::make_unique<views::View>());
  divider->SetBackground(
      views::CreateSolidBackground(SkColorSetARGB(0x14, 0x00, 0x00, 0x00)));
  divider->SetPreferredSize(gfx::Size(1, kDividerHeight));

  // The pixel magnifier, then the field itself.
  auto* glyph = field->AddChildView(std::make_unique<views::ImageView>(
      ui::ImageModel::FromVectorIcon(kZephyrusSearchIcon,
                                     SkColorSetA(zephyrus::Ink(), 0xB3),
                                     kSearchGlyphSize)));
  glyph->SetImageSize(gfx::Size(kSearchGlyphSize, kSearchGlyphSize));

  input_ = field->AddChildView(std::make_unique<views::Textfield>());
  input_->set_controller(this);
  input_->SetBackgroundEnabled(false);
  input_->SetBorder(nullptr);
  input_->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kBodyLarge));
  // No explicit text colour: Textfield::SetColor only tints the text that is
  // already there, so it would not hold for typed input. The dark theme
  // already renders this field's text light.
  field_layout->SetFlexForView(input_, 1);

  // ---- suggestions-container: chips inset from both edges, wrapping.
  auto* suggestions =
      container->AddChildView(std::make_unique<views::View>());
  suggestions->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::VH(0, kChipsInset)));
  auto* chips_layout =
      suggestions->SetLayoutManager(std::make_unique<views::FlexLayout>());
  chips_layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCollapseMargins(true)
      .SetDefault(views::kMarginsKey, gfx::Insets::VH(0, kChipsGap / 2))
      .SetMainAxisAlignment(views::LayoutAlignment::kCenter);

  chips_row_ = suggestions;

  // ---- suggestion list. Its own surface below the field, in the MD3 "docked
  // search" shape: the field is the bar, this is the sheet that drops from it.
  //
  // Created empty and hidden. It only has height once the user types, so the
  // resting card keeps the size the design draws.
  auto* list = container->AddChildView(std::make_unique<views::View>());
  list->SetProperty(views::kMarginsKey,
                    gfx::Insets::TLBR(kListGap - kPanelGap, 0, 0, 0));
  list->SetPaintToLayer();
  list->layer()->SetFillsBoundsOpaquely(false);
  list->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(kListRadius));
  list->layer()->SetIsFastRoundedCorner(true);
  // Same blur and fill as the field, so the two read as one material rather
  // than a translucent bar with an opaque panel hanging off it.
  list->layer()->SetBackgroundBlur(kFieldBlurSigma);
  list->SetBackground(views::CreateRoundedRectBackground(
      SkColorSetA(zephyrus::Ground(), 0x99), kListRadius));
  list->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(8, 0), 0));
  list->SetVisible(false);
  suggestions_list_ = list;


  // Last: it labels both the chip and the field, so both must exist.
  RefreshEngineLabel();
  SetChevronOpen(false);
}

ZephyrusSearchOverlay::~ZephyrusSearchOverlay() = default;

// static
bool ZephyrusSearchOverlay::ShowForNewTab(Browser* browser) {
  if (!browser) {
    return false;
  }
  TabStripModel* model = browser->tab_strip_model();
  content::WebContents* contents =
      model ? model->GetActiveWebContents() : nullptr;
  if (!contents) {
    return false;  // Empty window: the user needs a tab, not a search box.
  }
  // The VISIBLE url, not the committed one: a tab mid-navigation away from the
  // NTP is already somewhere else as far as the user is concerned.
  const GURL url = contents->GetVisibleURL();
  if (!url.is_valid() || url.IsAboutBlank()) {
    return false;
  }
  // The NTP has its own search field; a floating one on top of it would be two
  // search boxes for one intent.
  if (url.SchemeIs(content::kChromeUIScheme) &&
      (url.host() == chrome::kChromeUINewTabPageHost ||
       url.host() == chrome::kChromeUINewTabHost ||
       url.host() == chrome::kChromeUINewTabPageThirdPartyHost)) {
    return false;
  }
  Show(browser);
  return true;
}

void ZephyrusSearchOverlay::Show(Browser* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view) {
    return;
  }
  ZephyrusSearchOverlay* overlay = browser_view->zephyrus_search_overlay();
  if (!overlay) {
    return;
  }
  // Toggle: a second Ctrl+T while it is up puts it away again.
  if (overlay->GetVisible()) {
    overlay->Dismiss();
  } else {
    overlay->Reveal();
  }
}

void ZephyrusSearchOverlay::Reveal() {
  // Cover the whole window: the scrim has to catch a click anywhere outside the
  // card, and the card is positioned against the full frame.
  if (parent()) {
    SetBoundsRect(parent()->GetLocalBounds());
  }
  const bool was_hidden = !GetVisible();
  hiding_ = false;
  SetVisible(true);
  RefreshEngineLabel();
  RequestShortcuts();
  DeprecatedLayoutImmediately();

  if (panel_ && panel_->layer()) {
    ui::Layer* layer = panel_->layer();
    // Reduced motion means gentler, not none: the opacity fade stays, because
    // it is what stops the card from appearing out of nowhere, and only the
    // scale — the part that actually moves — is dropped.
    const bool reduced = gfx::Animation::PrefersReducedMotion();
    // Only seed the "from" state on a real open. Re-opening mid-exit must
    // animate from wherever the card currently is, not snap back and replay —
    // that is the difference between an interruptible animation and a restart.
    if (was_hidden) {
      layer->SetOpacity(0.0f);
      layer->SetTransform(reduced ? gfx::Transform()
                                  : ScaleAboutCenter(panel_, kEnterScale));
    }
    ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
    settings.SetTransitionDuration(kEnterDuration);
    settings.SetTweenType(gfx::Tween::EASE_OUT_4);
    settings.SetPreemptionStrategy(
        ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    layer->SetOpacity(1.0f);
    layer->SetTransform(gfx::Transform());
  }

  if (input_) {
    input_->SetText(std::u16string());
    input_->RequestFocus();
  }
}

void ZephyrusSearchOverlay::Dismiss() {
  if (!GetVisible() || hiding_) {
    return;
  }
  if (input_) {
    input_->SetText(std::u16string());
  }

  if (!panel_ || !panel_->layer()) {
    SetVisible(false);
    return;
  }
  hiding_ = true;
  ui::Layer* layer = panel_->layer();
  ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
  settings.SetTransitionDuration(kExitDuration);
  settings.SetTweenType(gfx::Tween::EASE_OUT_4);
  settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  settings.AddObserver(this);
  layer->SetOpacity(0.0f);
  // As on the way in: under reduced motion the card fades without moving. The
  // opacity animation still runs, so the observer still fires and the card
  // still hides — dropping the transform must not drop the completion.
  if (!gfx::Animation::PrefersReducedMotion()) {
    layer->SetTransform(ScaleAboutCenter(panel_, kExitScale));
  }
}

void ZephyrusSearchOverlay::OnImplicitAnimationsCompleted() {
  // Only the exit hides. A re-open during the exit clears `hiding_`, so this
  // fires harmlessly after the card is already back on screen.
  if (hiding_) {
    hiding_ = false;
    SetVisible(false);
  }
}

void ZephyrusSearchOverlay::Layout(PassKey) {
  if (!panel_) {
    return;
  }
  const gfx::Size preferred = panel_->GetPreferredSize();
  const int panel_width = std::min(preferred.width(), width());
  // Above centre, the way a spotlight surface sits — dead centre reads as a
  // modal dialog rather than a command bar.
  const int y = std::max(0, height() / 5);
  panel_->SetBounds((width() - panel_width) / 2, y, panel_width,
                    preferred.height());
}

bool ZephyrusSearchOverlay::OnMousePressed(const ui::MouseEvent& event) {
  // Anything landing on the scrim rather than on the card is a click-away.
  Dismiss();
  return true;
}

bool ZephyrusSearchOverlay::HandleKeyEvent(views::Textfield* sender,
                                           const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_DOWN) {
    MoveSelection(1);
    return true;
  }
  if (key_event.key_code() == ui::VKEY_UP) {
    MoveSelection(-1);
    return true;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    // A highlighted row wins. With none highlighted this falls through to the
    // old behaviour, which is the right default: the user typed something and
    // pressed Enter without ever reaching for the list.
    if (!OpenSelectedSuggestion()) {
      OpenQuery(std::u16string(input_->GetText()));
    }
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    Dismiss();
    return true;
  }
  return false;
}

void ZephyrusSearchOverlay::ContentsChanged(views::Textfield* sender,
                                            const std::u16string& new_contents) {
  UpdateSuggestions(new_contents);
}

void ZephyrusSearchOverlay::UpdateSuggestions(const std::u16string& text) {
  Profile* const profile =
      browser_view_ ? zephyrus::ActiveProfile(browser_view_->browser())
                    : nullptr;
  if (!profile) {
    return;
  }
  if (text.empty()) {
    // Blank field: stop the controller and collapse the list, so the resting
    // card is the size the design draws rather than a panel with a stale set of
    // results hanging off it.
    if (autocomplete_) {
      autocomplete_->Stop(AutocompleteStopReason::kClobbered);
    }
    selected_row_ = -1;
    suggestion_rows_.clear();
    if (suggestions_list_) {
      suggestions_list_->RemoveAllChildViews();
      suggestions_list_->SetVisible(false);
    }
    if (chips_row_) {
      chips_row_->SetVisible(true);
    }
    panel_->InvalidateLayout();
    return;
  }

  if (!autocomplete_) {
    // The SAME controller the omnibox drives, deliberately. Workspace scoping
    // lives inside it (IsUrlOutsideCurrentWorkspace demotes other workspaces'
    // history), so reusing it means one definition of that rule rather than a
    // second one here that would drift.
    autocomplete_ = std::make_unique<AutocompleteController>(
        std::make_unique<ChromeAutocompleteProviderClient>(profile),
        AutocompleteControllerConfig{
            .provider_types = AutocompleteClassifier::DefaultOmniboxProviders()});
    autocomplete_observation_.Observe(autocomplete_.get());
  }

  AutocompleteInput input(text, metrics::OmniboxEventProto::OTHER,
                          ChromeAutocompleteSchemeClassifier(profile));
  input.set_focus_type(metrics::OmniboxFocusType::INTERACTION_DEFAULT);
  autocomplete_->Start(input);
}

void ZephyrusSearchOverlay::OnResultChanged(AutocompleteController* controller,
                                            bool default_match_changed) {
  RebuildSuggestionRows();
}

void ZephyrusSearchOverlay::MoveSelection(int delta) {
  if (suggestion_rows_.empty()) {
    return;
  }
  const int count = static_cast<int>(suggestion_rows_.size());
  // -1 is a real position, not "none": Up from the first row returns to the
  // typed text rather than wrapping to the bottom, which is what the omnibox
  // does and what the hand expects.
  int next = selected_row_ + delta;
  if (next < -1) {
    next = count - 1;
  } else if (next >= count) {
    next = -1;
  }
  selected_row_ = next;
  ApplySelectionHighlight();
}

bool ZephyrusSearchOverlay::OpenSelectedSuggestion() {
  if (selected_row_ < 0 || !autocomplete_ ||
      selected_row_ >= static_cast<int>(autocomplete_->result().size())) {
    return false;
  }
  const AutocompleteMatch& match =
      autocomplete_->result().match_at(static_cast<size_t>(selected_row_));
  if (!match.destination_url.is_valid()) {
    return false;
  }
  OpenUrl(match.destination_url);
  return true;
}

void ZephyrusSearchOverlay::RebuildSuggestionRows() {
  if (!suggestions_list_ || !autocomplete_) {
    return;
  }

  const AutocompleteResult& result = autocomplete_->result();
  const size_t count = std::min(result.size(), static_cast<size_t>(kMaxRows));

  // GROW OR SHRINK, then update in place. Never rebuild wholesale.
  //
  // OnResultChanged fires repeatedly as each provider reports in, so a full
  // rebuild destroyed and recreated the row under the cursor several times per
  // keystroke -- the mouse left a dying view and entered a new one, which is
  // what made the hover highlight blink and drop.
  while (suggestion_rows_.size() > count) {
    views::View* doomed = suggestion_rows_.back();
    suggestion_rows_.pop_back();
    suggestions_list_->RemoveChildViewT(doomed);
  }
  while (suggestion_rows_.size() < count) {
    suggestion_rows_.push_back(
        suggestions_list_->AddChildView(std::make_unique<SuggestionRow>(
            base::BindRepeating(&ZephyrusSearchOverlay::OpenUrl,
                                base::Unretained(this)))));
  }

  Profile* const profile =
      browser_view_ ? zephyrus::ActiveProfile(browser_view_->browser())
                    : nullptr;
  favicon::FaviconService* const favicons =
      profile ? FaviconServiceFactory::GetForProfile(
                    profile, ServiceAccessType::EXPLICIT_ACCESS)
              : nullptr;

  for (size_t i = 0; i < count; ++i) {
    const AutocompleteMatch& match = result.match_at(i);
    auto* row = views::AsViewClass<SuggestionRow>(suggestion_rows_[i]);
    if (!row) {
      continue;
    }
    const bool is_search = AutocompleteMatch::IsSearchType(match.type);
    // Only a row whose destination actually changed needs a new icon. Without
    // this every provider update re-requested every favicon, which is both the
    // churn that made icons flicker and a pile of cancelled work.
    if (row->SetMatch(match, is_search) && favicons && !is_search &&
        match.destination_url.is_valid()) {
      favicons->GetFaviconImageForPageURL(
          match.destination_url,
          base::BindOnce(&ZephyrusSearchOverlay::OnRowFaviconReady,
                         weak_factory_.GetWeakPtr(), row->icon()),
          &favicon_tracker_);
    }
  }

  // A result set that no longer contains the highlighted position must not keep
  // it: the next Enter would open whatever slid into that index.
  if (selected_row_ >= static_cast<int>(suggestion_rows_.size())) {
    selected_row_ = -1;
  }
  const bool any = !suggestion_rows_.empty();
  suggestions_list_->SetVisible(any);
  if (chips_row_) {
    // The chips are the zero-input state; they and the list never share the
    // card.
    chips_row_->SetVisible(!any);
  }
  ApplySelectionHighlight();
  panel_->InvalidateLayout();
}

void ZephyrusSearchOverlay::OnRowFaviconReady(
    views::ImageView* icon,
    const favicon_base::FaviconImageResult& result) {
  // `icon` is a raw view pointer that outlived an async hop. It is safe only
  // because RebuildSuggestionRows cancels this tracker before destroying rows;
  // without that cancel this would be a use-after-free on every keystroke.
  if (!icon || result.image.IsEmpty()) {
    return;  // Keeps the generic glyph.
  }
  gfx::ImageSkia image = result.image.AsImageSkia();
  if (image.width() != kRowIconSize || image.height() != kRowIconSize) {
    image = gfx::ImageSkiaOperations::CreateResizedImage(
        image, skia::ImageOperations::RESIZE_BEST,
        gfx::Size(kRowIconSize, kRowIconSize));
  }
  icon->SetImage(ui::ImageModel::FromImageSkia(image));
}

void ZephyrusSearchOverlay::ApplySelectionHighlight() {
  // The row owns its own state layer now, because it has to reconcile keyboard
  // selection with hover and press -- all three can be true at once, and the
  // old version overwrote the mouse state with a background of its own.
  for (size_t i = 0; i < suggestion_rows_.size(); ++i) {
    if (auto* row = views::AsViewClass<SuggestionRow>(suggestion_rows_[i])) {
      row->SetSelected(static_cast<int>(i) == selected_row_);
    }
  }
}

void ZephyrusSearchOverlay::ShowEnginePicker() {
  // The picker is a separate widget, so showing it deactivates this card. With
  // close-on-deactivate left on, this overlay would close and destroy itself —
  // and `engine_chip_`, which the picker is anchored to — the instant the
  // picker appeared, leaving the picker pointing at freed views. That was a
  // reliable crash. Hold dismissal open until the picker is gone.
  // No close-on-deactivate dance any more: a view does not deactivate, so
  // opening the picker cannot dismiss this card out from under the picker's own
  // anchor. That was the crash when this was a bubble.
  SetChevronOpen(true);
  ZephyrusSearchEnginePicker::Show(
      zephyrus::ActiveProfile(browser_view_->browser()), engine_chip_,
      base::BindOnce(&ZephyrusSearchOverlay::OnEnginePickerFinished,
                     weak_factory_.GetWeakPtr()));
}

void ZephyrusSearchOverlay::OnEnginePickerFinished() {
  SetChevronOpen(false);
  RefreshEngineLabel();
}

void ZephyrusSearchOverlay::SetChevronOpen(bool open) {
  if (!engine_chevron_ || !engine_chevron_->layer()) {
    return;
  }
  gfx::Transform flipped;
  if (open) {
    const gfx::PointF center(engine_chevron_->size().width() / 2.0f,
                             engine_chevron_->size().height() / 2.0f);
    flipped.Translate(center.x(), center.y());
    flipped.Rotate(180);
    flipped.Translate(-center.x(), -center.y());
  }
  // Short and ease-out, like every other transition on this card. Reduced
  // motion skips the rotation but still lands on the right orientation.
  if (gfx::Animation::PrefersReducedMotion()) {
    engine_chevron_->layer()->SetTransform(flipped);
    return;
  }
  ui::ScopedLayerAnimationSettings settings(
      engine_chevron_->layer()->GetAnimator());
  settings.SetTransitionDuration(base::Milliseconds(140));
  settings.SetTweenType(gfx::Tween::EASE_OUT_4);
  settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  engine_chevron_->layer()->SetTransform(flipped);
}

void ZephyrusSearchOverlay::RequestShortcuts() {
  Profile* profile = browser_view_ ? zephyrus::ActiveProfile(browser_view_->browser())
                                   : nullptr;
  scoped_refptr<history::TopSites> top_sites =
      profile ? TopSitesFactory::GetForProfile(profile) : nullptr;
  if (!top_sites) {
    OnShortcutsReady(history::MostVisitedURLList());  // No history: fall back.
    return;
  }
  top_sites->GetMostVisitedURLs(
      base::BindOnce(&ZephyrusSearchOverlay::OnShortcutsReady,
                     weak_factory_.GetWeakPtr()));
}

void ZephyrusSearchOverlay::OnShortcutsReady(
    const history::MostVisitedURLList& sites) {
  if (!chips_row_) {
    return;
  }
  chips_row_->RemoveAllChildViews();

  Profile* profile = browser_view_ ? zephyrus::ActiveProfile(browser_view_->browser())
                                   : nullptr;
  favicon::FaviconService* favicons =
      profile ? FaviconServiceFactory::GetForProfile(
                    profile, ServiceAccessType::EXPLICIT_ACCESS)
              : nullptr;

  // Build the working list: history if we have it, the curated set if not.
  std::vector<std::pair<std::u16string, GURL>> shortcuts;
  for (const history::MostVisitedURL& site : sites) {
    if (!site.url.is_valid()) {
      continue;
    }
    std::u16string label = site.title;
    if (label.empty()) {
      label = base::UTF8ToUTF16(site.url.host());
    }
    shortcuts.emplace_back(label, site.url);
    if (shortcuts.size() >= kMaxChips) {
      break;
    }
  }
  if (shortcuts.empty()) {
    for (const Shortcut& fallback : kFallbackShortcuts) {
      shortcuts.emplace_back(base::UTF8ToUTF16(fallback.label),
                             GURL(fallback.url));
    }
  }

  for (const auto& [label, url] : shortcuts) {
    // Dark glass, matching the field above rather than the light pills the
    // first pass used: one material for the whole card.
    auto chip = std::make_unique<GlassChip>(
        base::BindRepeating(&ZephyrusSearchOverlay::OpenUrl,
                            base::Unretained(this), url),
        label);
    chip->SetTextColor(views::Button::STATE_NORMAL,
                       SkColorSetA(zephyrus::Ink(), 0xE6));
    chip->SetTextColor(views::Button::STATE_HOVERED, zephyrus::Ink());
    chip->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(8, 14)));
    chip->SetImageLabelSpacing(8);
    chip->SetMaxSize(gfx::Size(180, 0));  // Long titles elide, not stretch.

    views::LabelButton* chip_ptr =
        chips_row_->AddChildView(std::move(chip));

    // The site's own favicon, from the local cache only — these are places the
    // user has already been, so it is almost always there.
    if (favicons && url.is_valid()) {
      // Generic glyph first so the row never jumps as icons land.
      chip_ptr->SetImageModel(
          views::Button::STATE_NORMAL,
          ui::ImageModel::FromVectorIcon(vector_icons::kSearchIcon,
                                         zephyrus::Muted(),
                                         kChipFaviconSize));
      favicons->GetFaviconImageForPageURL(
          url,
          base::BindOnce(&ZephyrusSearchOverlay::OnShortcutFaviconReady,
                         weak_factory_.GetWeakPtr(), chip_ptr),
          &favicon_tracker_);
    }
  }
  DeprecatedLayoutImmediately();
}

void ZephyrusSearchOverlay::OnShortcutFaviconReady(
    views::LabelButton* chip,
    const favicon_base::FaviconImageResult& result) {
  if (!chip || result.image.IsEmpty()) {
    return;  // Keeps the generic glyph.
  }
  gfx::ImageSkia icon = result.image.AsImageSkia();
  if (icon.width() != kChipFaviconSize || icon.height() != kChipFaviconSize) {
    icon = gfx::ImageSkiaOperations::CreateResizedImage(
        icon, skia::ImageOperations::RESIZE_BEST,
        gfx::Size(kChipFaviconSize, kChipFaviconSize));
  }
  chip->SetImageModel(views::Button::STATE_NORMAL,
                      ui::ImageModel::FromImageSkia(icon));
}

void ZephyrusSearchOverlay::OnEngineFaviconReady(
    const favicon_base::FaviconImageResult& result) {
  if (!engine_favicon_ || result.image.IsEmpty()) {
    return;
  }
  engine_favicon_->SetImage(
      ui::ImageModel::FromImageSkia(result.image.AsImageSkia()));
}

void ZephyrusSearchOverlay::RefreshEngineLabel() {
  if (!engine_chip_ || !engine_label_) {
    return;
  }
  const std::u16string name =
      zephyrus::GetDefaultSearchEngineName(zephyrus::ActiveProfile(browser_view_->browser()));

  // The engine's own mark, if one is cached locally. Cancel any earlier lookup
  // so a previous engine's icon cannot land after a newer choice.
  favicon_tracker_.TryCancelAll();
  Profile* profile = zephyrus::ActiveProfile(browser_view_->browser());
  TemplateURLService* service =
      profile ? TemplateURLServiceFactory::GetForProfile(profile) : nullptr;
  const TemplateURL* def =
      service ? service->GetDefaultSearchProvider() : nullptr;
  if (favicon::FaviconService* favicons =
          profile ? FaviconServiceFactory::GetForProfile(
                        profile, ServiceAccessType::EXPLICIT_ACCESS)
                  : nullptr;
      favicons && def && def->favicon_url().is_valid()) {
    favicons->GetFaviconImage(
        def->favicon_url(),
        base::BindOnce(&ZephyrusSearchOverlay::OnEngineFaviconReady,
                       weak_factory_.GetWeakPtr()),
        &favicon_tracker_);
  }
  // No engine configured is possible (policy, or a half-set-up profile); fall
  // back to a neutral label rather than an empty chip.
  const std::u16string engine_text = name.empty() ? u"Search engine" : name;
  if (engine_label_) {
    engine_label_->SetText(engine_text);
  }
  // The chip is a focusable views::Button whose text lives in a CHILD label, so
  // the button itself has no accessible name of its own. That is not just an
  // accessibility gap: views::RunAccessibilityPaintChecks DCHECKs on a
  // focusable, unnamed view the first time it paints, which crashed every new
  // tab in a DCHECK build. Naming it here keeps the name correct as the default
  // engine changes.
  engine_chip_->GetViewAccessibility().SetName(
      u"Search engine: " + engine_text);
  const std::u16string placeholder =
      name.empty() ? u"Search or type a URL"
                   : u"Search " + name + u" or type a URL";
  if (input_) {
    input_->SetPlaceholderText(placeholder);
    input_->SetAccessibleName(placeholder);
  }
}

void ZephyrusSearchOverlay::OpenQuery(const std::u16string& text) {
  if (text.empty()) {
    return;
  }
  // Resolve exactly as the omnibox would, so a bare domain navigates and
  // anything else searches with the user's default engine.
  AutocompleteMatch match;
  AutocompleteClassifier* classifier =
      AutocompleteClassifierFactory::GetForProfile(zephyrus::ActiveProfile(browser_view_->browser()));
  if (!classifier) {
    return;
  }
  classifier->Classify(text, /*prefer_keyword=*/false,
                       /*allow_exact_keyword_match=*/false,
                       metrics::OmniboxEventProto::BLANK, &match,
                       /*alternate_nav_url=*/nullptr);
  OpenUrl(match.destination_url);
}

void ZephyrusSearchOverlay::OpenUrl(const GURL& url) {
  if (!url.is_valid()) {
    return;
  }
  NavigateParams params(browser_view_->browser(), url,
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);

  Dismiss();
}

BEGIN_METADATA(ZephyrusSearchOverlay)
END_METADATA
