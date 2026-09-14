// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SEARCH_OVERLAY_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SEARCH_OVERLAY_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include <memory>
#include <vector>

#include "base/scoped_observation.h"
#include "components/history/core/browser/history_types.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class Browser;
class BrowserView;

namespace favicon_base {
struct FaviconImageResult;
}

namespace views {
class Button;
class ImageView;
class Label;
class LabelButton;
class Textfield;
}

// Zephyrus has no new-tab page. Pressing Ctrl+T or the toolbar "+" floats this
// search card over whatever the user is already looking at: no tab is created
// and nothing navigates until they commit a query. Enter resolves the text the
// same way the omnibox would (URL vs. default-engine search) and opens it in a
// new foreground tab; Escape or clicking away dismisses it.
//
// This is a VIEW INSIDE THE BROWSER WINDOW, not a bubble widget, and that is
// load-bearing: the field carries a compositor backdrop blur, and a backdrop
// filter can only sample content in its own compositor frame. As a separate
// widget there was nothing behind it to blur — the same reason DWM window
// backdrops failed for menus. Hosted here, it samples the web contents, which
// is exactly what the design's "background blur 25 + 60% fill" describes.
//
// Being a view rather than a widget means the behaviour a bubble gave for free
// is implemented here instead: this view is a full-window scrim, so a click
// anywhere outside the card dismisses it, and Escape is handled on the field.
class ZephyrusSearchOverlay : public views::View,
                              public views::TextfieldController,
                              public AutocompleteController::Observer,
                              public ui::ImplicitAnimationObserver {
  METADATA_HEADER(ZephyrusSearchOverlay, views::View)

 public:
  explicit ZephyrusSearchOverlay(BrowserView* browser_view);
  ZephyrusSearchOverlay(const ZephyrusSearchOverlay&) = delete;
  ZephyrusSearchOverlay& operator=(const ZephyrusSearchOverlay&) = delete;
  ~ZephyrusSearchOverlay() override;

  // Shows the card over `browser`'s window, or dismisses it if already up.
  static void Show(Browser* browser);

  // Ctrl+T handling. Returns true when the overlay took over and NO tab should
  // be created.
  //
  // Only when the user is looking at a real page: from a website, Ctrl+T means
  // "search for something", and the overlay answers that without spending a tab
  // on a page you are going to navigate away from anyway. On the New Tab Page
  // -- which has a search box of its own -- and on an empty window, a real new
  // tab is still the right answer, so this returns false and the caller falls
  // through to it.
  static bool ShowForNewTab(Browser* browser);

  // Reveals the card and focuses the field.
  void Reveal();
  // Hides the card and clears whatever was typed.
  void Dismiss();

  // views::View:
  void Layout(PassKey) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

  // views::TextfieldController: Enter commits, Escape dismisses.
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // ui::ImplicitAnimationObserver: hides the card once the exit finishes.
  void OnImplicitAnimationsCompleted() override;

 private:
  // Resolves `text` to a destination and opens it in a new foreground tab.
  void OpenQuery(const std::u16string& text);

 public:
  // Public because a suggestion row's click callback calls it. The rows are
  // built here and owned by this view, so this is not widening the surface for
  // anyone else.
  void OpenUrl(const GURL& url);

 private:

  void ShowEnginePicker();
  void OnEnginePickerFinished();
  void RefreshEngineLabel();
  void OnEngineFaviconReady(const favicon_base::FaviconImageResult& result);

  // Points the chevron down when the picker is closed and up while it is open,
  // so the control says which state it is in rather than leaving the user to
  // infer it from the popup.
  void SetChevronOpen(bool open);

  // Shortcut chips come from the profile's most-visited sites, so the row is
  // about where this user actually goes. Asked for on every reveal because the
  // ranking shifts as they browse.
  // AutocompleteController::Observer:
  void OnResultChanged(AutocompleteController* controller,
                       bool default_match_changed) override;

  // Restarts autocomplete for `text`, or clears the list when it is empty.
  void UpdateSuggestions(const std::u16string& text);
  // Rebuilds the rows from the controller's current result.
  void RebuildSuggestionRows();
  // Moves the highlight. `delta` is +1 for Down, -1 for Up.
  void MoveSelection(int delta);
  // Opens the highlighted row, or returns false when nothing is highlighted and
  // the caller should fall back to resolving the raw text.
  bool OpenSelectedSuggestion();
  void ApplySelectionHighlight();

  void OnRowFaviconReady(views::ImageView* icon,
                         const favicon_base::FaviconImageResult& result);

  void RequestShortcuts();
  void OnShortcutsReady(const history::MostVisitedURLList& sites);
  void OnShortcutFaviconReady(views::LabelButton* chip,
                              const favicon_base::FaviconImageResult& result);

  const raw_ptr<BrowserView> browser_view_;
  // The centred card. `this` is the full-window scrim around it.
  raw_ptr<views::View> panel_ = nullptr;
  raw_ptr<views::Textfield> input_ = nullptr;
  // A container button, not a LabelButton: LabelButton always paints its
  // image before its text, which put the chevron in front of the engine
  // name. The design has favicon, name, then chevron.
  raw_ptr<views::Button> engine_chip_ = nullptr;
  raw_ptr<views::Label> engine_label_ = nullptr;
  raw_ptr<views::ImageView> engine_chevron_ = nullptr;
  raw_ptr<views::View> chips_row_ = nullptr;
  // Holds one row per suggestion. Hidden (and empty) while the field is blank,
  // which is what keeps the resting card the size the design draws.
  raw_ptr<views::View> suggestions_list_ = nullptr;

  // Workspace scoping is NOT implemented here. AutocompleteController already
  // demotes URLs visited in other workspaces, via
  // ChromeAutocompleteProviderClient::IsUrlOutsideCurrentWorkspace, so driving
  // the same controller the omnibox uses gets it for free -- and keeps one
  // definition of what "this workspace's history" means.
  std::unique_ptr<AutocompleteController> autocomplete_;
  base::ScopedObservation<AutocompleteController,
                          AutocompleteController::Observer>
      autocomplete_observation_{this};
  // -1 when nothing is highlighted, which is a real state: Enter then resolves
  // the typed text rather than opening a row the user never moved to.
  int selected_row_ = -1;
  std::vector<raw_ptr<views::View>> suggestion_rows_;
  raw_ptr<views::ImageView> engine_favicon_ = nullptr;

  // True between the start of the exit animation and the card actually
  // hiding, so a re-open mid-exit reverses instead of hiding afterwards.
  bool hiding_ = false;

  base::CancelableTaskTracker favicon_tracker_;
  base::WeakPtrFactory<ZephyrusSearchOverlay> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SEARCH_OVERLAY_H_
