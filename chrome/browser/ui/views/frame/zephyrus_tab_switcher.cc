// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_tab_switcher.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"
#include "content/public/browser/web_contents.h"
#include "ui/aura/window.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace {

// Card geometry. Thumbnails are 16:10 and sized at runtime: we aim for
// kThumbWidthDesired but shrink toward kThumbWidthMin so the strip always fits
// inside the window instead of running off the edge with many tabs.
constexpr int kThumbWidthDesired = 260;
constexpr int kThumbWidthMin = 150;
constexpr int kCardSpacing = 14;
constexpr int kCardPadding = 10;
constexpr int kMaxCards = 8;

// 16:10, matching the shape of a browser viewport closely enough to read.
int ThumbHeightFor(int width) {
  return width * 10 / 16;
}

// Alphas of WHITE lift a dark surface and do nothing on a light one, so
// these are palette steps now. Cards HOLD things -> card radius.
SkColor CardBg() {
  return zephyrus::Surface();
}
SkColor CardSelectedBg() {
  return zephyrus::Raise(zephyrus::Surface(), 0x3A);
}
SkColor Placeholder() {
  return zephyrus::Rule();
}

// Matches the spotlight card, so the two frosted surfaces read as the same
// material rather than two different treatments.
constexpr float kPanelBlurSigma = 15.0f;
// Holds things -> card radius. Was 16.
constexpr int kPanelCornerRadius = zephyrus::kRadiusCard;

// Only one switching session at a time.
ZephyrusTabSwitcher* g_switcher = nullptr;

}  // namespace

ZephyrusTabSwitcher::ZephyrusTabSwitcher(BrowserView* browser_view)
    : browser_view_(browser_view) {
  // Full-window scrim, hidden until a session starts. Needs its own layer to
  // composite above the web contents.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetVisible(false);

  // The card strip, frosted over the page. Fill and blur are one setting in two
  // halves: an opaque fill would hide the blur entirely, which is what the
  // sidebar did before it was corrected.
  panel_ = AddChildView(std::make_unique<views::View>());
  panel_->SetPaintToLayer();
  panel_->layer()->SetFillsBoundsOpaquely(false);
  panel_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kPanelCornerRadius));
  panel_->layer()->SetBackgroundBlur(kPanelBlurSigma);
  panel_->SetBackground(views::CreateRoundedRectBackground(
      SkColorSetA(zephyrus::Surface(), 0xF2), kPanelCornerRadius));
  panel_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(12),
      kCardSpacing));
}

ZephyrusTabSwitcher::~ZephyrusTabSwitcher() {
  // The view outlives individual sessions now, so this only runs at window
  // teardown — but the handler must still come off, or it dangles.
  if (handler_target_) {
    handler_target_->RemovePreTargetHandler(&key_watcher_);
    handler_target_ = nullptr;
  }
  if (g_switcher == this) {
    g_switcher = nullptr;
  }
}

void ZephyrusTabSwitcher::ClearEntries() {
  // Entries first: they hold raw_ptrs into the card views, so dropping them
  // before the views means those pointers never dangle.
  entries_.clear();
  selected_ = 0;
  if (panel_) {
    panel_->RemoveAllChildViews();
  }
}

void ZephyrusTabSwitcher::EndSession() {
  if (handler_target_) {
    handler_target_->RemovePreTargetHandler(&key_watcher_);
    handler_target_ = nullptr;
  }
  if (g_switcher == this) {
    g_switcher = nullptr;
  }
  ClearEntries();
  SetVisible(false);
}

void ZephyrusTabSwitcher::Layout(PassKey) {
  if (!panel_) {
    return;
  }
  const gfx::Size preferred = panel_->GetPreferredSize();
  const int panel_width = std::min(preferred.width(), width());
  panel_->SetBounds((width() - panel_width) / 2,
                    std::max(0, (height() - preferred.height()) / 2),
                    panel_width, preferred.height());
}

// static
bool ZephyrusTabSwitcher::IsShowing() {
  return g_switcher != nullptr;
}

// static
bool ZephyrusTabSwitcher::CycleOrShow(Browser* browser, bool forward) {
  // Already switching: another Tab just moves the selection.
  if (g_switcher) {
    g_switcher->AdvanceSelection(forward);
    return true;
  }
  if (!browser) {
    return false;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view) {
    return false;
  }

  ZephyrusTabSwitcher* ptr = browser_view->zephyrus_tab_switcher();
  if (!ptr) {
    return false;
  }
  ptr->ClearEntries();
  if (!ptr->BuildEntries()) {
    return false;  // Nothing to switch between; caller falls back.
  }

  g_switcher = ptr;

  // Watch for the Ctrl release ourselves — accelerators are press-only.
  if (aura::Window* window = browser_view->GetWidget()->GetNativeWindow()) {
    ptr->handler_target_ = window;
    window->AddPreTargetHandler(&ptr->key_watcher_);
  }

  // Cover the window, then show. Focus is never taken: this is a view, so the
  // page keeps focus for free — the bubble had to ShowInactive() to fake it.
  if (ptr->parent()) {
    ptr->SetBoundsRect(ptr->parent()->GetLocalBounds());
  }
  ptr->SetVisible(true);
  ptr->DeprecatedLayoutImmediately();

  // The first Ctrl+Tab should already land on the next tab, matching the way
  // Alt+Tab opens with the previous window selected.
  ptr->AdvanceSelection(forward);
  return true;
}

bool ZephyrusTabSwitcher::BuildEntries() {
  Browser* const browser = browser_view_ ? browser_view_->browser() : nullptr;
  TabStripModel* model = browser ? browser->tab_strip_model() : nullptr;
  if (!model) {
    return false;
  }
  ZephyrusWorkspaceManager* workspaces =
      browser_view_ ? browser_view_->zephyrus_workspace_manager() : nullptr;

  // Collect the eligible tabs first: the card size depends on how many there
  // are, so nothing can be laid out until the count is known.
  std::vector<content::WebContents*> tabs;
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    // Only the workspace you are actually in — showing another workspace's tabs
    // would both confuse the switcher and undermine the separation workspaces
    // exist to provide.
    if (workspaces && !workspaces->IsContentsInCurrentWorkspace(contents)) {
      continue;
    }
    tabs.push_back(contents);
    if (tabs.size() >= kMaxCards) {
      break;
    }
  }

  if (tabs.size() < 2) {
    return false;  // A switcher for one tab is UI for nothing.
  }

  // Fit the strip to the window: start from the desired card size and shrink
  // (never below kThumbWidthMin) until the whole row fits with margins.
  const int available =
      browser_view_ ? browser_view_->width() - 96 : kThumbWidthDesired * 4;
  const int count = static_cast<int>(tabs.size());
  int thumb_width = kThumbWidthDesired;
  if (count > 0) {
    const int per_card =
        (available - kCardSpacing * (count - 1)) / count - 2 * kCardPadding;
    thumb_width = std::clamp(per_card, kThumbWidthMin, kThumbWidthDesired);
  }
  const int thumb_height = ThumbHeightFor(thumb_width);

  for (content::WebContents* contents : tabs) {

    auto* card = panel_->AddChildView(std::make_unique<views::View>());
    card->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets(kCardPadding), 8));
    card->SetBackground(views::CreateRoundedRectBackground(
        CardBg(), zephyrus::kRadiusCard));
    // Pin the card. Without this it sizes to its title label, so a tab with a
    // long title got a visibly wider card than its neighbours.
    card->SetPreferredSize(gfx::Size(thumb_width + 2 * kCardPadding,
                                     thumb_height + 2 * kCardPadding + 22));

    auto* image = card->AddChildView(std::make_unique<views::ImageView>());
    image->SetImageSize(gfx::Size(thumb_width, thumb_height));
    image->SetPreferredSize(gfx::Size(thumb_width, thumb_height));
    // Until the real thumbnail arrives, a flat panel rather than empty space.
    image->SetBackground(views::CreateRoundedRectBackground(Placeholder(),
                                          zephyrus::kRadiusCard));

    std::u16string title = contents->GetTitle();
    if (title.empty()) {
      title = base::UTF8ToUTF16(contents->GetVisibleURL().host());
    }
    auto* label = card->AddChildView(std::make_unique<views::Label>(title));
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetEnabledColor(SK_ColorWHITE);
    label->SetFontList(gfx::FontList("Segoe UI, 12px"));
    label->SetElideBehavior(gfx::ELIDE_TAIL);
    label->SetMultiLine(false);
    // Fixed, not just capped: a preferred size that grows with the text is what
    // made the cards uneven in the first place.
    label->SetPreferredSize(gfx::Size(thumb_width, 18));
    label->SetMaximumWidth(thumb_width);

    Entry entry;
    entry.contents = contents;
    entry.card = card;
    entry.image = image;
    entries_.push_back(std::move(entry));
  }

  thumb_size_ = gfx::Size(thumb_width, thumb_height);

  // Start on the active tab, so the first Advance moves off it.
  content::WebContents* active = model->GetActiveWebContents();
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].contents == active) {
      selected_ = i;
      break;
    }
  }
  RequestThumbnails();
  UpdateSelectionVisuals();
  return true;
}

void ZephyrusTabSwitcher::RequestThumbnails() {
  for (size_t i = 0; i < entries_.size(); ++i) {
    ThumbnailTabHelper* helper =
        ThumbnailTabHelper::FromWebContents(entries_[i].contents);
    if (!helper) {
      continue;
    }
    scoped_refptr<ThumbnailImage> thumbnail = helper->thumbnail();
    if (!thumbnail) {
      continue;
    }
    entries_[i].subscription = thumbnail->Subscribe();
    entries_[i].subscription->SetSizeHint(thumb_size_);
    entries_[i].subscription->SetUncompressedImageCallback(
        base::BindRepeating(&ZephyrusTabSwitcher::OnThumbnailReceived,
                            weak_factory_.GetWeakPtr(), i));
    // Delivery is async; the placeholder shows until it lands.
    thumbnail->RequestThumbnailImage();
  }
}

void ZephyrusTabSwitcher::OnThumbnailReceived(size_t index,
                                              gfx::ImageSkia image) {
  if (index >= entries_.size() || image.isNull()) {
    return;
  }
  views::ImageView* view = entries_[index].image;
  if (!view) {
    return;
  }
  view->SetImage(ui::ImageModel::FromImageSkia(image));
}

void ZephyrusTabSwitcher::AdvanceSelection(bool forward) {
  if (entries_.empty()) {
    return;
  }
  const size_t count = entries_.size();
  selected_ = forward ? (selected_ + 1) % count : (selected_ + count - 1) % count;
  UpdateSelectionVisuals();
}

void ZephyrusTabSwitcher::UpdateSelectionVisuals() {
  for (size_t i = 0; i < entries_.size(); ++i) {
    views::View* card = entries_[i].card;
    if (!card) {
      continue;
    }
    const bool is_selected = (i == selected_);
    card->SetBackground(views::CreateRoundedRectBackground(
        is_selected ? CardSelectedBg() : CardBg(), zephyrus::kRadiusCard));
    card->SetBorder(
        is_selected ? views::CreateRoundedRectBorder(
              zephyrus::kHairline * 2.f, zephyrus::kRadiusCard,
              zephyrus::Accent())
                    : views::CreateEmptyBorder(2));
    card->SchedulePaint();
  }
}

void ZephyrusTabSwitcher::KeyWatcher::OnKeyEvent(ui::KeyEvent* event) {
  switcher_->OnWindowKeyEvent(event);
}

void ZephyrusTabSwitcher::OnWindowKeyEvent(ui::KeyEvent* event) {
  // Escape abandons the switch entirely.
  if (event->type() == ui::EventType::kKeyPressed &&
      event->key_code() == ui::VKEY_ESCAPE) {
    event->StopPropagation();
    CancelSwitch();
    return;
  }
  // The commit: Ctrl came back up, so take the selection.
  if (event->type() == ui::EventType::kKeyReleased &&
      (event->key_code() == ui::VKEY_CONTROL ||
       event->key_code() == ui::VKEY_LCONTROL ||
       event->key_code() == ui::VKEY_RCONTROL)) {
    Commit();
  }
}

void ZephyrusTabSwitcher::Commit() {
  content::WebContents* target =
      selected_ < entries_.size() ? entries_[selected_].contents : nullptr;
  Browser* browser = browser_view_ ? browser_view_->browser() : nullptr;

  // Ends the session but does NOT destroy this view, so everything below is
  // safe to touch. The bubble version closed its widget here and had to warn
  // that `this` was already gone.
  EndSession();

  if (!target || !browser) {
    return;
  }
  TabStripModel* model = browser->tab_strip_model();
  if (!model) {
    return;
  }
  const int index = model->GetIndexOfWebContents(target);
  if (index != TabStripModel::kNoTab) {
    model->ActivateTabAt(
        index, TabStripUserGestureDetails(
                   TabStripUserGestureDetails::GestureType::kOther));
  }
}

void ZephyrusTabSwitcher::CancelSwitch() {
  EndSession();
}

BEGIN_METADATA(ZephyrusTabSwitcher)
END_METADATA
