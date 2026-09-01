// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/extensions/extensions_menu_coordinator.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"

#include <memory>

#include "base/check_deref.h"
#include "base/feature_list.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/extensions/extensions_menu_view_model.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "chrome/browser/ui/views/extensions/extensions_menu_delegate_desktop.h"
#include "extensions/browser/permissions_manager.h"
#include "extensions/common/extension_features.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/metadata/view_factory.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

ExtensionsMenuCoordinator::ExtensionsMenuCoordinator(
    BrowserWindowInterface* browser,
    ExtensionsContainer* extensions_container)
    : browser_(browser),
      extensions_container_(CHECK_DEREF(extensions_container)) {}

ExtensionsMenuCoordinator::~ExtensionsMenuCoordinator() {
  if (views::Widget* const menu = GetExtensionsMenuWidget()) {
    // Close the menu widget synchronously as it may hold references back to the
    // coordinator and its host Browser.
    menu->CloseNow();
  }
}

void ExtensionsMenuCoordinator::Show(
    views::BubbleAnchor anchor,
    ExtensionsContainerViews* extensions_container_views) {
  DCHECK(base::FeatureList::IsEnabled(
      extensions_features::kExtensionsMenuAccessControl));
  std::unique_ptr<views::BubbleDialogDelegate> bubble_delegate =
      CreateExtensionsMenuBubbleDialogDelegate(anchor,
                                               extensions_container_views);

  // Zephyrus: the nub has to be applied after the widget exists, since it needs
  // the bubble's frame view.
  views::BubbleDialogDelegate* zephyrus_delegate = bubble_delegate.get();
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble_delegate),
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  zephyrus::ApplyAnchoredNub(zephyrus_delegate);
  widget->Show();
}

void ExtensionsMenuCoordinator::Hide() {
  DCHECK(base::FeatureList::IsEnabled(
      extensions_features::kExtensionsMenuAccessControl));
  if (views::Widget* const menu = GetExtensionsMenuWidget()) {
    menu->CloseNow();
  }
}

bool ExtensionsMenuCoordinator::IsShowing() const {
  return bubble_tracker_.view() != nullptr;
}

views::Widget* ExtensionsMenuCoordinator::GetExtensionsMenuWidget() {
  return IsShowing() ? bubble_tracker_.view()->GetWidget() : nullptr;
}

std::unique_ptr<views::BubbleDialogDelegate>
ExtensionsMenuCoordinator::CreateExtensionsMenuBubbleDialogDelegateForTesting(
    views::BubbleAnchor anchor,
    ExtensionsContainerViews* extensions_container_views) {
  return CreateExtensionsMenuBubbleDialogDelegate(anchor,
                                                  extensions_container_views);
}

std::unique_ptr<views::BubbleDialogDelegate>
ExtensionsMenuCoordinator::CreateExtensionsMenuBubbleDialogDelegate(
    views::BubbleAnchor anchor,
    ExtensionsContainerViews* extensions_container_views) {
  DCHECK(base::FeatureList::IsEnabled(
      extensions_features::kExtensionsMenuAccessControl));
  // Zephyrus: TOP_CENTER so the popup centres under the extensions button and
  // the nub lands in the middle of its top edge. Anchoring by a corner puts the
  // nub wherever that corner happens to be.
  auto bubble_delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor, views::BubbleBorder::TOP_CENTER,
      // Zephyrus: STANDARD_SHADOW, not DIALOG_SHADOW. DIALOG_SHADOW is
      // drawn by the platform and the widget is sized tight to the bubble,
      // which clips the nub and the top corners' curve off the top edge.
      // STANDARD_SHADOW is Chromium-drawn and leaves a margin the nub can
      // live in; it is what the shield popup uses.
      views::BubbleBorder::STANDARD_SHADOW, /*autosize=*/true);
  // 28px corners, matching every other Zephyrus popup.
  zephyrus::ConfigureBubble(bubble_delegate.get());
  bubble_delegate->SetOwnedByWidget(
      views::WidgetDelegate::OwnedByWidgetPassKey());
  bubble_delegate->set_margins(gfx::Insets(0));
  bubble_delegate->set_fixed_width(
      views::LayoutProvider::Get()->GetDistanceMetric(
          ChromeDistanceMetric::DISTANCE_EXTENSIONS_MENU_WIDTH));
  // Let anchor view's MenuButtonController handle the highlight.
  bubble_delegate->set_highlight_button_when_shown(false);
  bubble_delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  bubble_delegate->SetEnableArrowKeyTraversal(true);

  auto* bubble_contents = bubble_delegate->SetContentsView(
      views::Builder<views::View>().SetUseDefaultFillLayout(true).Build());
  bubble_view_observation_.Observe(bubble_contents);
  bubble_tracker_.SetView(bubble_contents);

  menu_delegate_ = std::make_unique<ExtensionsMenuDelegateDesktop>(
      browser_, &extensions_container_.get(), extensions_container_views,
      bubble_contents);
  menu_delegate_->OpenMainPage();

  return bubble_delegate;
}

void ExtensionsMenuCoordinator::OnViewIsDeleting(views::View* observed_view) {
  bubble_tracker_.SetView(nullptr);
  bubble_view_observation_.Reset();
  // Reset the delegate to keep 1:1 lifetime with the view.
  menu_delegate_.reset();
}
