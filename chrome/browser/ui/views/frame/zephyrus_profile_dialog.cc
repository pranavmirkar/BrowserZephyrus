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

#include "chrome/browser/ui/views/frame/zephyrus_profile_dialog.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/image/image.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr int kDialogWidth = 380;
constexpr int kFieldHeight = 34;

// Avatar swatches: 36px icons with a 2px selection ring, 8 across. Eight fits
// kDialogWidth with room to spare (8*40 + 7*6 = 362) and is enough choice to
// feel personal without turning setup into a decision.
constexpr int kAvatarSize = 36;
constexpr int kAvatarRingThickness = 2;
constexpr int kAvatarSpacing = 6;
constexpr size_t kAvatarChoices = 8;

// Only one at a time, so a double-click can't stack two dialogs.
views::Widget* g_dialog_widget = nullptr;

}  // namespace

ZephyrusProfileDialog::ZephyrusProfileDialog(Mode mode,
                                             views::View* anchor,
                                             ConfirmCallback on_confirm)
    : views::BubbleDialogDelegateView(anchor, views::BubbleBorder::FLOAT),
      mode_(mode),
      on_confirm_(std::move(on_confirm)) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
             static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetButtonLabel(ui::mojom::DialogButton::kOk,
                 mode_ == Mode::kFirstRun ? u"Continue" : u"Create profile");
  SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Not now");
  // A name is required — nothing is created without one.
  SetButtonEnabled(ui::mojom::DialogButton::kOk, false);
  SetAcceptCallback(base::BindOnce(&ZephyrusProfileDialog::Confirm,
                                   base::Unretained(this)));

  set_margins(gfx::Insets(18));
  // Setup is a decision, not a passing popup: don't let a stray click discard it.
  set_close_on_deactivate(false);
  zephyrus::ConfigureBubble(this);
  // Was a hardcoded #16161A panel -- a dark-theme leftover that never moved
  // when the palette did, so on a light browser it was a black box.
  SetBackgroundColor(zephyrus::Surface());

  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 10));

  auto* heading = AddChildView(std::make_unique<views::Label>(
      mode_ == Mode::kFirstRun ? u"Welcome to Zephyrus"
                               : u"Create a new profile"));
  heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  heading->SetEnabledColor(SK_ColorWHITE);
  heading->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kTitleMedium, /*emphasized=*/true));

  auto* body = AddChildView(std::make_unique<views::Label>(
      mode_ == Mode::kFirstRun
          ? u"Name this profile to get started. Profiles keep your tabs, "
            u"history, and logins separate."
          : u"Give the profile a name. It gets its own tabs, history, and "
            u"logins."));
  body->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  body->SetMultiLine(true);
  body->SetEnabledColor(zephyrus::Muted());
  body->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kBodySmall));
  body->SizeToFit(kDialogWidth);

  name_field_ = AddChildView(std::make_unique<views::Textfield>());
  name_field_->set_controller(this);
  name_field_->SetPlaceholderText(mode_ == Mode::kFirstRun ? u"e.g. Personal"
                                                           : u"Profile name");
  name_field_->SetAccessibleName(u"Profile name");
  name_field_->SetPreferredSize(gfx::Size(kDialogWidth, kFieldHeight));

  BuildAvatarRow();
}

void ZephyrusProfileDialog::BuildAvatarRow() {
  auto* caption = AddChildView(std::make_unique<views::Label>(u"Pick an icon"));
  caption->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  caption->SetEnabledColor(zephyrus::Muted());
  caption->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kBodySmall));

  auto* row = AddChildView(std::make_unique<views::View>());
  row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      kAvatarSpacing));

  // Chromium's avatar set starts with the legacy cartoon icons; the modern ones
  // are the flat illustrations that match this UI, so start there.
  const size_t first = profiles::GetModernAvatarIconStartIndex();
  const size_t total = profiles::GetDefaultAvatarIconCount();
  auto& bundle = ui::ResourceBundle::GetSharedInstance();

  for (size_t i = 0; i < kAvatarChoices && first + i < total; ++i) {
    const size_t avatar_index = first + i;
    const gfx::Image sized = profiles::GetSizedAvatarIcon(
        bundle.GetImageNamed(
            profiles::GetDefaultAvatarIconResourceIDAtIndex(avatar_index)),
        kAvatarSize, kAvatarSize);

    const size_t row_position = avatar_indices_.size();
    auto button = std::make_unique<views::ImageButton>(base::BindRepeating(
        &ZephyrusProfileDialog::SelectAvatar, base::Unretained(this),
        row_position));
    button->SetImageModel(views::Button::STATE_NORMAL,
                          ui::ImageModel::FromImage(sized));
    button->SetAccessibleName(u"Profile icon " +
                              base::NumberToString16(row_position + 1));
    // Reserve the ring's space on every button, selected or not, so selection
    // never shifts the row.
    button->SetPreferredSize(
        gfx::Size(kAvatarSize + 2 * kAvatarRingThickness,
                  kAvatarSize + 2 * kAvatarRingThickness));
    button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);

    avatar_indices_.push_back(avatar_index);
    avatar_buttons_.push_back(row->AddChildView(std::move(button)));
  }

  if (!avatar_buttons_.empty()) {
    SelectAvatar(0);
  }
}

void ZephyrusProfileDialog::SelectAvatar(size_t row_position) {
  if (row_position >= avatar_buttons_.size()) {
    return;
  }
  selected_row_position_ = row_position;
  for (size_t i = 0; i < avatar_buttons_.size(); ++i) {
    const bool selected = (i == row_position);
    avatar_buttons_[i]->SetBorder(
        selected ? views::CreateRoundedRectBorder(
                       kAvatarRingThickness,
                       (kAvatarSize + 2 * kAvatarRingThickness) / 2.0f,
                       zephyrus::Accent())
                 : views::CreateEmptyBorder(gfx::Insets(kAvatarRingThickness)));
  }
}

ZephyrusProfileDialog::~ZephyrusProfileDialog() {
  g_dialog_widget = nullptr;
}

// static
void ZephyrusProfileDialog::Show(Mode mode,
                                 Browser* browser,
                                 ConfirmCallback on_confirm) {
  if (g_dialog_widget) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view) {
    return;
  }

  auto dialog = std::make_unique<ZephyrusProfileDialog>(
      mode, browser_view->contents_container(), std::move(on_confirm));
  ZephyrusProfileDialog* dialog_ptr = dialog.get();
  views::Widget* widget =
      views::BubbleDialogDelegateView::CreateBubble(std::move(dialog));
  g_dialog_widget = widget;
  widget->Show();
  dialog_ptr->name_field_->RequestFocus();
}

void ZephyrusProfileDialog::OnWidgetInitialized() {
  views::BubbleDialogDelegateView::OnWidgetInitialized();
  zephyrus::ApplyBubbleFrame(this);
}

bool ZephyrusProfileDialog::HandleKeyEvent(views::Textfield* sender,
                                           const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed ||
      key_event.key_code() != ui::VKEY_RETURN) {
    return false;
  }
  if (base::TrimWhitespace(name_field_->GetText(), base::TRIM_ALL).empty()) {
    return true;  // Swallow: Enter must not confirm an empty name.
  }
  Confirm();
  if (views::Widget* widget = GetWidget()) {
    widget->Close();
  }
  return true;
}

void ZephyrusProfileDialog::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  SetButtonEnabled(
      ui::mojom::DialogButton::kOk,
      !base::TrimWhitespace(new_contents, base::TRIM_ALL).empty());
  DialogModelChanged();
}

void ZephyrusProfileDialog::Confirm() {
  const std::u16string name =
      std::u16string(base::TrimWhitespace(name_field_->GetText(),
                                          base::TRIM_ALL));
  if (name.empty() || !on_confirm_) {
    return;
  }
  const size_t avatar_index =
      selected_row_position_ < avatar_indices_.size()
          ? avatar_indices_[selected_row_position_]
          : profiles::GetModernAvatarIconStartIndex();
  std::move(on_confirm_).Run(name, avatar_index);
}

BEGIN_METADATA(ZephyrusProfileDialog)
END_METADATA
