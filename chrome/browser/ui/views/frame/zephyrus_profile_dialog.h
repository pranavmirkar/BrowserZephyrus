// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ---------------------------------------------------------------------------
// ZEPHYRUS PROFILES BACKEND - RETAINED, NOT BUILT
//
// The profiles feature's user interface was withdrawn pending Google auth. This
// file is the BACKEND: profile creation, naming, and the in-window swap. It is
// deliberately kept in the tree and removed from chrome/browser/ui/BUILD.gn so
// it does not compile, rather than deleted -- it is the logic the Google auth
// work will build on.
//
// To bring the feature back: restore the four
// views/frame/zephyrus_profile_{switcher,dialog}.{h,cc} entries in
// chrome/browser/ui/BUILD.gn, then remove the `#if 0` blocks marked
// "ZEPHYRUS PROFILES FRONTEND - DISABLED" in views/toolbar/toolbar_view.{h,cc}
// and views/frame/browser_view.cc.
//
// Known caveat for whoever revives it: PW-6 is still open, and
// [[zephyrus-mixed-profile-window]] records that the session service tracks
// state per WINDOW, so an in-window profile swap can write private-workspace
// tabs to disk. Do not re-enable the swap path without resolving that first.
// ---------------------------------------------------------------------------

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_PROFILE_DIALOG_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_PROFILE_DIALOG_H_

#include <stddef.h>

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/textfield/textfield_controller.h"

class Browser;

namespace views {
class ImageButton;
class Textfield;
}

// Asks for a profile name before a profile exists or is created.
//
// Nothing is created unless the user confirms with a non-empty name: dismissing
// this dialog leaves the profile list untouched, which is the whole point —
// "Add profile" must not silently produce an unnamed profile.
//
// Two modes:
//  * kFirstRun — Zephyrus has never been set up. Chromium always ships a
//    "Default" profile, so there is nothing to create; confirming NAMES that
//    existing profile. Dismissing just means we ask again next launch.
//  * kAddProfile — the user picked "Add profile"; confirming creates a new one
//    and switches the window into it.
//
// Google sign-in is deliberately absent: this build has no OAuth credentials
// (`use_official_google_api_keys = false`), so Chrome's account-based profile
// creation cannot work here. The name is local-only for now.
class ZephyrusProfileDialog : public views::BubbleDialogDelegateView,
                              public views::TextfieldController {
  METADATA_HEADER(ZephyrusProfileDialog, views::BubbleDialogDelegateView)

 public:
  enum class Mode { kFirstRun, kAddProfile };

  // Run with the entered name and the chosen avatar index once the user
  // confirms. Never run on dismissal. The index is an index into Chromium's
  // default avatar set (profiles::GetDefaultAvatarIconResourceIDAtIndex), so it
  // can be handed straight to ProfileAttributesEntry::SetAvatarIconIndex.
  using ConfirmCallback =
      base::OnceCallback<void(const std::u16string& name, size_t avatar_index)>;

  ZephyrusProfileDialog(Mode mode, views::View* anchor, ConfirmCallback on_confirm);
  ZephyrusProfileDialog(const ZephyrusProfileDialog&) = delete;
  ZephyrusProfileDialog& operator=(const ZephyrusProfileDialog&) = delete;
  ~ZephyrusProfileDialog() override;

  // Shows the dialog centered over `browser`'s window. Ignored if one is
  // already open, so a double-click cannot stack two.
  static void Show(Mode mode, Browser* browser, ConfirmCallback on_confirm);

  // views::WidgetDelegate:
  void OnWidgetInitialized() override;

  // views::TextfieldController: Enter confirms, matching the OK button.
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;

 private:
  // Runs the callback if the name is usable, then closes.
  void Confirm();

  // Builds the row of pickable avatars and selects the default one.
  void BuildAvatarRow();
  // Moves the selection ring to `index` (an index into `avatar_indices_`).
  void SelectAvatar(size_t row_position);

  const Mode mode_;
  ConfirmCallback on_confirm_;
  raw_ptr<views::Textfield> name_field_ = nullptr;

  // The default-avatar indices offered, and the buttons showing them. Parallel
  // arrays: `avatar_buttons_[i]` displays `avatar_indices_[i]`.
  std::vector<size_t> avatar_indices_;
  std::vector<raw_ptr<views::ImageButton>> avatar_buttons_;
  size_t selected_row_position_ = 0;
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_PROFILE_DIALOG_H_
