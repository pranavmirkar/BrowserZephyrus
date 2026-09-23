// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_OMNIBOX_ROUNDED_OMNIBOX_RESULTS_FRAME_H_
#define CHROME_BROWSER_UI_VIEWS_OMNIBOX_ROUNDED_OMNIBOX_RESULTS_FRAME_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

class LocationBar;
class OmniboxPopupWebUIBaseContent;

// A class that wraps a Widget's content view to provide a custom results frame.
class RoundedOmniboxResultsFrame : public views::View {
  METADATA_HEADER(RoundedOmniboxResultsFrame, views::View)

 public:
  RoundedOmniboxResultsFrame(views::View* contents,
                             LocationBar* location_bar,
                             bool forward_mouse_events);
  RoundedOmniboxResultsFrame(const RoundedOmniboxResultsFrame&) = delete;
  RoundedOmniboxResultsFrame& operator=(const RoundedOmniboxResultsFrame&) =
      delete;
  ~RoundedOmniboxResultsFrame() override;

  // Hook to customize Widget initialization.
  static void OnBeforeWidgetInit(views::Widget::InitParams* params,
                                 views::Widget* widget);

  // The height of the location bar view part of the omnibox popup.
  static int GetNonResultSectionHeight(bool include_cutout = true);

  // How the Widget is aligned relative to the location bar.
  static gfx::Insets GetLocationBarAlignmentInsets();

  // Returns the blur region taken up by the Omnibox popup shadows.
  static gfx::Insets GetShadowInsets();

  // Zephyrus (Figma searchbar_resultdropdown): horizontal inset, within the
  // frame, at which the detached card is drawn. The widget is outset from the
  // search bar by the location-bar alignment insets plus the shadow margin;
  // undoing both lands the card flush with the search bar (Figma: bar and card
  // are both 477 wide at the same x). Rows pin their content to this inset, and
  // the selected hero row overhangs it by kZephyrusHeroOverhang.
  static int GetZephyrusCardInset();

  // The margin the popup keeps around the card on each side (Figma: card 477
  // inside a 531 frame, 27px per side). It is what lets the card land flush
  // with the search bar: the popup is widened by it and the card inset by it.
  //
  // It used to be the room the selected "hero" row grew into, past the card.
  // Under M3 nothing overhangs -- OmniboxResultView keeps the selection inside
  // the card -- but the popup geometry is built on this value, and setting it
  // to 0 moved the card off the bar and let the rows' own bounds clip the
  // selection on one side. So the margin stays; only the overhang went.
  static constexpr int kZephyrusHeroOverhang = 27;

  // The gap between a row's pill and the card edge, on EVERY side. The pill's
  // radius is derived from the card's through this number
  // (zephyrus::m3::ConcentricInner), so the two only nest if the gap really is
  // the same all the way round -- which is what it was not: the sides were 4
  // and the top was 8, and a 24dp curve inside a 28dp one at two different
  // offsets reads as a mistake even when you cannot say why.
  static constexpr int kZephyrusPillInsideCard = 4;
  // Half the gap between two adjacent pills; also the pill's own inset within
  // its row. The card's vertical padding is the remainder of
  // kZephyrusPillInsideCard once this is accounted for -- see
  // kZephyrusCardPadding.
  static constexpr int kZephyrusPillRowInset = 2;
  // The card's inner vertical padding, top and bottom. DERIVED: the first and
  // last pills must sit kZephyrusPillInsideCard from the card's edge, and each
  // pill already insets itself by kZephyrusPillRowInset inside its row, so the
  // card supplies only the difference.
  static constexpr int kZephyrusCardPadding =
      kZephyrusPillInsideCard - kZephyrusPillRowInset;

  // Rule 2, solved from the INSIDE. A single-line row is 40dp, so its pill is
  // 36dp tall and can be no rounder than 18 -- asking it for more (it used to
  // ask for 24, the popup radius minus the 4dp gap) just clamps it to a
  // capsule of 18, sitting 4dp inside a 28 corner: two curves with different
  // centres, which is what the eye catches at the card's top corners. The
  // pill's radius is the fixed quantity, so the CARD takes pill + gap.
  // (Checked against OmniboxMatchCellView::kRowHeight in the .cc.)
  static constexpr int kZephyrusPillRadius = 40 / 2 - kZephyrusPillRowInset;
  static constexpr int kZephyrusCardRadius =
      kZephyrusPillRadius + kZephyrusPillInsideCard;

  // Zephyrus: the detached card's bounds in this frame's coordinates. Rows
  // anchor their selection pill to this rather than to their own bounds, so the
  // pill's overhang is guaranteed equal on the left and right regardless of any
  // offset between a row's bounds and the card.
  gfx::Rect GetZephyrusCardBounds() const;

  // Removes the `contents_` view and returns ownership to the caller.
  std::unique_ptr<views::View> ExtractContents();

  // Returns the `contents_` view.
  views::View* GetContents();

  // Returns the nested `OmniboxPopupWebUIBaseContent` if the contents of the
  // frame contains one.
  OmniboxPopupWebUIBaseContent* GetOmniboxPopupWebUIBaseContent();

  void SetCutoutVisibility(bool visible);

  static constexpr int kDefaultElevation = 16;

  // Updates whether mouse events should be forwarded to the underlying
  // location bar.
  void set_forward_mouse_events(bool forward) {
    forward_mouse_events_ = forward;
  }

  bool forward_mouse_events() const { return forward_mouse_events_; }

  // views::View:
  void Layout(PassKey) override;
  void AddedToWidget() override;
  // Zephyrus: paints a soft drop shadow hugging just the detached card (drawn
  // in the frame, behind the card layer, so it isn't clipped and the card reads
  // as floating below the search bar).
  void OnPaintBackground(gfx::Canvas* canvas) override;
#if !defined(USE_AURA)
  void OnMouseMoved(const ui::MouseEvent& event) override;
  void OnMouseEvent(ui::MouseEvent* event) override;
#endif  // !USE_AURA

 private:
  void SetElevation(int elevation);

  gfx::Insets GetContentInsets();

  raw_ptr<views::View> top_background_ = nullptr;
  raw_ptr<views::View> contents_host_ = nullptr;
  raw_ptr<views::View> contents_;

  // Zephyrus: the detached card's fill color (page-adapted), so the frame can
  // paint a matching drop shadow behind it.
  SkColor zephyrus_card_color_ = SK_ColorWHITE;

  // Only used on platforms that support Aura (non-Mac).
  [[maybe_unused]] bool forward_mouse_events_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_OMNIBOX_ROUNDED_OMNIBOX_RESULTS_FRAME_H_
