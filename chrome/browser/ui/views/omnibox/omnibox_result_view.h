// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_OMNIBOX_OMNIBOX_RESULT_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_OMNIBOX_OMNIBOX_RESULT_VIEW_H_

#include <stddef.h>

#include <memory>
#include <utility>

#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/omnibox/omnibox_mouse_enter_exit_handler.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/omnibox_popup_selection.h"
#include "components/omnibox/browser/suggestion_answer.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/view.h"

class OmniboxLocalAnswerHeaderView;
class OmniboxMatchCellView;
class OmniboxPopupViewViews;
class OmniboxResultSelectionIndicator;
class OmniboxSuggestionButtonRowView;
enum class OmniboxPart;
enum class OmniboxPartState;

namespace gfx {
class Image;
}

namespace views {
class Button;
class ImageButton;
class Separator;
}  // namespace views

class OmniboxResultView : public views::View, public gfx::AnimationDelegate {
  METADATA_HEADER(OmniboxResultView, views::View)

 public:
  OmniboxResultView(OmniboxPopupViewViews* popup_view, size_t model_index);
  OmniboxResultView(const OmniboxResultView&) = delete;
  OmniboxResultView& operator=(const OmniboxResultView&) = delete;
  ~OmniboxResultView() override;

  // Zephyrus: resolves an omnibox color id, substituting a page-color-adapted
  // value when the active page color is set (so the results popup matches the
  // title bar), otherwise falling back to the theme color.
  SkColor GetThemedColor(ui::ColorId id) const;

  // Static method to share logic about how to set backgrounds of popup cells.
  static std::unique_ptr<views::Background> GetPopupCellBackground(
      const views::View* view,
      OmniboxPartState part_state);

  // Updates the match used to paint the contents of this result view. We copy
  // the match so that we can continue to paint the last result even after the
  // model has changed.
  void SetMatch(const AutocompleteMatch& match);

  // Applies the current theme to the current text and widget colors.
  // Also refreshes the icons which may need to be re-colored as well.
  void ApplyThemeAndRefreshIcons(bool force_reapply_styles = false);

  // Invoked when this result view has been selected or unselected.
  void OnSelectionStateChanged();

  // Whether this result view should be considered 'selected'. This returns
  // false if this line's header is selected (instead of the match itself).
  bool GetMatchSelected() const;

  // Returns the focused button or nullptr if none exists for this suggestion.
  views::Button* GetActiveAuxiliaryButtonForAccessibility();
  const views::Button* GetActiveAuxiliaryButtonForAccessibility() const;

  OmniboxPartState GetThemeState() const;

  // Notification that the match icon has changed and schedules a repaint.
  void OnMatchIconUpdated();

  // Stores the image in a local data member and schedules a repaint.
  void SetRichSuggestionImage(const gfx::ImageSkia& image);

  void ButtonPressed(OmniboxPopupSelection::LineState state,
                     const ui::Event& event);

  void UpdateAccessibilityProperties();

  void UpdateAccessibleName();

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnThemeChanged() override;

 private:
  FRIEND_TEST_ALL_PREFIXES(OmniboxPopupViewViewsTest, DeleteSuggestion);
  FRIEND_TEST_ALL_PREFIXES(OmniboxResultViewTest, ContextualSecondaryText);

  void OpenIphLink();

  gfx::Image GetIcon() const;

  // Updates the highlight state of the row, as well as conditionally shows
  // controls that are only visible on row hover.
  void UpdateHoverState();

  void UpdateDividerLineVisibility();

  // Sets the visibility of the secondary text (description) based on the
  // current state. Only applies to contextual suggestions.
  void UpdateSecondaryTextVisibility();

  // Sets the visibility of the |thumbs_up_button_| and |thumbs_down_button_|
  // based on the current state.
  void UpdateFeedbackButtonsVisibility();

  // Sets the visibility of the |remove_suggestion_button_| based on the current
  // state.
  void UpdateRemoveSuggestionVisibility();

  // Updates the 'selected' state of the view as applicable based on whether or
  // not the view is selected.
  void UpdateAccessibilitySelectedState();

  // views::View:
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;
  // Zephyrus: paints the animated "hero" pill behind the selected row's
  // content (kept in OnPaintBackground so it renders behind the icon/text and
  // can animate its width without the z-order problems of a layer underlay).
  void OnPaintBackground(gfx::Canvas* canvas) override;

  // gfx::AnimationDelegate: drives the selected-row width-jump.
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  // Zephyrus: the detached card's left/right edges expressed in this row's
  // coordinates. The selection pill is anchored to these rather than to the
  // row's own bounds, so its overhang is equal on both sides even if a row's
  // bounds are offset from the card. Returns false if the frame isn't
  // reachable yet (e.g. during construction).
  bool GetZephyrusCardEdges(float* left, float* right) const;

  // Zephyrus: how far the selection pill currently extends past the card on
  // EACH side. Clamped to the room available inside this row on the tighter
  // side, so the overhang stays equal left/right and the pill's rounded corners
  // are never sliced off by the row's paint clip.
  float ZephyrusOverhang(float card_left, float card_right) const;

  // Zephyrus: the selection pill's current horizontal inset, interpolated by
  // the width-jump animation between the card's edge and the hero's overhang.
  // Fallback for when the card edges aren't available.
  float ZephyrusPillInset() const;
  // Keeps the row's icon/text tracking that pill, so the favicon and text sit
  // at a constant padding inside whichever surface is active and the expanded
  // hero is properly filled instead of leaving dead space on the left.
  void UpdateZephyrusContentInset();

  // The parent view.
  const raw_ptr<OmniboxPopupViewViews> popup_view_;

  // This result's model index.
  const size_t model_index_;

  // The data this class is built to display (the "Omnibox Result").
  AutocompleteMatch match_;

  // Weak pointers for easy reference.

  // The blue bar used to indicate selection.
  raw_ptr<OmniboxResultSelectionIndicator> selection_indicator_ = nullptr;

  // A container view for layout.
  raw_ptr<views::View> local_answer_header_and_suggestion_and_buttons_;

  // This separator runs along the top edge to visually divide the toolbelt
  // match from other matches.
  raw_ptr<views::Separator> divider_line_;

  // The answer header; e.g. 'Summary' or 'Generating...'. Lazily initialized.
  raw_ptr<OmniboxLocalAnswerHeaderView> local_answer_header_ = nullptr;

  // The icon, contents, description, etc depicting the match.
  raw_ptr<OmniboxMatchCellView> suggestion_view_;

  // The row of buttons that appears when actions such as tab switch or Pedals
  // are on the suggestion. It is owned by the base view, not this raw pointer.
  raw_ptr<OmniboxSuggestionButtonRowView> button_row_ = nullptr;

  // The thumbs up button used to submit feedback for suggestions.
  raw_ptr<views::ImageButton> thumbs_up_button_;

  // The thumbs down button used to submit feedback for suggestions.
  raw_ptr<views::ImageButton> thumbs_down_button_;

  // The "X" button at the end of the match cell, used to remove suggestions.
  raw_ptr<views::ImageButton> remove_suggestion_button_;

  // Keeps track of mouse-enter and mouse-exit events of child Views.
  OmniboxMouseEnterExitHandler mouse_enter_exit_handler_;

  // Zephyrus: 0->1 progress of the selected-row hero pill "width jump". Shown
  // when this row becomes selected, hidden when it isn't; OnPaintBackground
  // interpolates the pill's width and its border/shadow strength from it.
  gfx::SlideAnimation zephyrus_hero_animation_{this};
  bool zephyrus_hero_shown_ = false;

  base::WeakPtrFactory<OmniboxResultView> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_OMNIBOX_OMNIBOX_RESULT_VIEW_H_
