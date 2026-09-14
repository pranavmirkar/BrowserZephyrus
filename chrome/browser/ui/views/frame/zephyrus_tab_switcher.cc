// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_tab_switcher.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/color_utils.h"
#include "ui/aura/window.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
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
// Spacing on an 8dp grid, which is the one thing Material's layout actually
// asks for. 14/10/12 were near-misses of it and read as slightly arbitrary.
constexpr int kCardSpacing = 12;
constexpr int kCardPadding = 8;
constexpr int kPanelPadding = 8;
constexpr int kMaxCards = 8;

// The meta row under each thumbnail: favicon + title.
constexpr int kFaviconSize = 16;
constexpr int kMetaGap = 8;
constexpr int kMetaHeight = 20;

// 16:10, matching the shape of a browser viewport closely enough to read.
int ThumbHeightFor(int width) {
  return width * 10 / 16;
}

// Null when the site has given us nothing yet; the caller draws the globe.
gfx::ImageSkia GetTabFavicon(content::WebContents* contents) {
  if (auto* driver =
          favicon::ContentFaviconDriver::FromWebContents(contents)) {
    return driver->GetFavicon().AsImageSkia();
  }
  return gfx::ImageSkia();
}

// Alphas of WHITE lift a dark surface and do nothing on a light one, so
// these are palette steps now.
SkColor CardBg() {
  return zephyrus::Surface();
}

// The selected card, as TONAL elevation plus the faintest accent tint.
//
// This is Material's own preference -- MD3 moved elevation from drop shadows to
// a surface tint -- and it is the only reading of "material" that survives
// contact with this design language, which forbids shadows outright
// (zephyrus_bubble_style.h: separation is a line, never a shadow).
//
// The accent appears here at 0x12, which is a tint and not a fill. That is
// deliberate restraint: the accent's entire value is how rarely it shows, and
// this is not a NEW use of it -- it reinforces the outline that already marks
// the selection, on the same card, for the same reason.
SkColor CardSelectedBg() {
  return color_utils::AlphaBlend(zephyrus::Accent(),
                                 zephyrus::Raise(zephyrus::Surface(), 0x3A),
                                 SkAlpha{0x12});
}

// Behind a thumbnail that has not arrived. A step off the card rather than the
// rule colour: a hairline tone used as a large fill reads as a grey slab, which
// is most of why the empty state looked dead.
SkColor Placeholder() {
  return zephyrus::Raise(zephyrus::Surface(), 0x14);
}

// Matches the spotlight card, so the two frosted surfaces read as the same
// material rather than two different treatments.
constexpr float kPanelBlurSigma = 15.0f;
// The switcher FLOATS over the page, so it takes the popup radius rather than
// the card one -- the same value the context menus and omnibox results use.
constexpr int kPanelCornerRadius = zephyrus::kRadiusPopup;

// CONCENTRIC, not copied.
//
// Nested corners share a centre only when the inner radius is the outer radius
// minus the gap between them. Give a child the same number as its parent and
// its corner is visibly too round for the space it sits in; give it an
// unrelated number and the two curves fight. Deriving it means the relationship
// survives anyone later retuning the padding.
//
//   panel 24 - panel padding  8 -> card  16
//   card  16 - card padding   8 -> thumb  8
//
// Note this deliberately puts the cards at 16, which zephyrus_bubble_style.h's
// binary-radius rule ("pill or card, nothing in between") would normally
// reject. Concentric nesting is the reason, and it is a geometric one: a card
// inside a 24 panel cannot also be 8 without looking loose in the corners.
constexpr int kCardCornerRadius = kPanelCornerRadius - kPanelPadding;
constexpr int kThumbCornerRadius = kCardCornerRadius - kCardPadding;

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
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(kPanelPadding),
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
        gfx::Insets(kCardPadding), kMetaGap));
    card->SetBackground(views::CreateRoundedRectBackground(
        CardBg(), kCardCornerRadius));
    // Pin the card. Without this it sizes to its title label, so a tab with a
    // long title got a visibly wider card than its neighbours.
    card->SetPreferredSize(
        gfx::Size(thumb_width + 2 * kCardPadding,
                  thumb_height + 2 * kCardPadding + kMetaGap + kMetaHeight));

    auto* image = card->AddChildView(std::make_unique<views::ImageView>());
    image->SetImageSize(gfx::Size(thumb_width, thumb_height));
    image->SetPreferredSize(gfx::Size(thumb_width, thumb_height));
    // Until the real thumbnail arrives, a flat panel rather than empty space.
    image->SetBackground(views::CreateRoundedRectBackground(
        Placeholder(), kThumbCornerRadius));
    // CLIP the thumbnail to the same radius.
    //
    // The rounded background above was only ever visible while the placeholder
    // showed: the delivered thumbnail is a plain bitmap and painted square
    // straight over those corners. A rounded card with square pictures in it is
    // most of what read as unfinished.
    image->SetPaintToLayer();
    image->layer()->SetFillsBoundsOpaquely(false);
    image->layer()->SetRoundedCornerRadius(
        gfx::RoundedCornersF(kThumbCornerRadius));
    image->layer()->SetIsFastRoundedCorner(true);

    // The meta row: favicon, then title. This is the line the switcher was
    // missing -- a page is recognised by its mark long before its title is
    // read, which is why a strip of grey thumbnails with text under them takes
    // real effort to scan.
    auto* meta = card->AddChildView(std::make_unique<views::View>());
    auto* meta_layout = meta->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kMetaGap));
    meta_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    meta->SetPreferredSize(gfx::Size(thumb_width, kMetaHeight));

    auto* favicon_view = meta->AddChildView(std::make_unique<views::ImageView>());
    favicon_view->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));
    favicon_view->SetPreferredSize(gfx::Size(kFaviconSize, kFaviconSize));
    const gfx::ImageSkia favicon = GetTabFavicon(contents);
    const bool fallback_icon = favicon.isNull();
    if (!fallback_icon) {
      // A real favicon is an IMAGE and is never recoloured -- sites keep their
      // own colour here, exactly as they do in the sidebar's tab list.
      favicon_view->SetImage(ui::ImageModel::FromImageSkia(favicon));
    }

    std::u16string title = contents->GetTitle();
    if (title.empty()) {
      title = base::UTF8ToUTF16(contents->GetVisibleURL().host());
    }
    auto* label = meta->AddChildView(std::make_unique<views::Label>(title));
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    // Colour is set by UpdateSelectionVisuals, not here: it depends on whether
    // this card is the selected one, and that is not known yet.
    label->SetAutoColorReadabilityEnabled(false);
    // The panel paints to a TRANSLUCENT layer, so subpixel text antialiasing --
    // which needs an opaque backing -- has to be off. The sidebar carries the
    // same note for the same reason, and Views DCHECKs on it in debug builds.
    label->SetSubpixelRenderingEnabled(false);
    label->SetFontList(gfx::FontList("Segoe UI, Medium 13px"));
    label->SetElideBehavior(gfx::ELIDE_TAIL);
    label->SetMultiLine(false);
    // Fixed, not just capped: a preferred size that grows with the text is what
    // made the cards uneven in the first place.
    const int label_width = std::max(0, thumb_width - kFaviconSize - kMetaGap);
    label->SetPreferredSize(gfx::Size(label_width, kMetaHeight));
    label->SetMaximumWidth(label_width);
    meta_layout->SetFlexForView(label, 1);

    Entry entry;
    entry.contents = contents;
    entry.card = card;
    entry.image = image;
    entry.favicon = favicon_view;
    entry.title = label;
    entry.fallback_icon = fallback_icon;
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
  // Selection moves THREE things, not one.
  //
  // It used to move only the card's fill and outline, which meant every title
  // in the strip was the same weight and the selected card had to be found by
  // spotting a 2px line. Stepping the text and the fallback glyph from muted to
  // full ink on the selected card is what makes the row scannable -- the eye
  // lands on the brightest text, not on the thinnest border.
  const SkColor ink = zephyrus::Ink();
  const SkColor muted = zephyrus::Muted();

  for (size_t i = 0; i < entries_.size(); ++i) {
    const Entry& entry = entries_[i];
    views::View* card = entry.card;
    if (!card) {
      continue;
    }
    const bool is_selected = (i == selected_);

    card->SetBackground(views::CreateRoundedRectBackground(
        is_selected ? CardSelectedBg() : CardBg(), kCardCornerRadius));
    // The unselected border is not empty any more: an unselected card carries a
    // hairline of the rule colour at the SAME inset, so nothing shifts by two
    // pixels as the selection moves, and the cards read as objects rather than
    // as floating tone patches.
    card->SetBorder(
        is_selected
            ? views::CreateRoundedRectBorder(zephyrus::kHairline * 2.f,
                                             kCardCornerRadius,
                                             zephyrus::Accent())
            : views::CreateRoundedRectBorder(zephyrus::kHairline * 2.f,
                                             kCardCornerRadius,
                                             zephyrus::Rule()));

    if (entry.title) {
      entry.title->SetEnabledColor(is_selected ? ink : muted);
    }
    // Only the fallback globe is ever tinted. A real favicon keeps the site's
    // own colours whether the card is selected or not.
    if (entry.favicon && entry.fallback_icon) {
      entry.favicon->SetImage(ui::ImageModel::FromVectorIcon(
          vector_icons::kGlobeIcon, is_selected ? ink : muted, kFaviconSize));
    }
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
