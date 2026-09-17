// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_tab_switcher.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/aura/window.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

// M3 MULTI-BROWSE CAROUSEL, centred on the selected tab.
//
// The sizing is Material's, taken from its component source rather than
// remembered: small items are 40-56dp (m3_carousel_small_item_size_min/max), a
// small item is the large one divided by three and clamped into that range,
// and a medium item is halfway between the two (Compose's
// multiBrowseKeylineList).
//
// Items are MASKED, not scaled. Every item draws its page at the large size and
// shows the middle slice that fits, so a medium or small item reads as the same
// page seen through a narrower window rather than as a shrunken picture.
constexpr int kLargePreferred = 360;
constexpr int kLargeMin = 200;
constexpr int kSmallMin = 40;
constexpr int kSmallMax = 56;
constexpr int kItemGap = 8;

// M3's extra-large shape (m3_carousel_small_item_default_corner_size), which
// makes a 56dp small item a full pill.
constexpr int kItemRadius = zephyrus::kRadiusXLarge;

// CONCENTRIC: the panel's radius is the item radius plus the padding between
// them, 28 + 20 = 48, which is also a step on the shape scale. An item at 28
// inside a panel at 28 would be visibly too round for its space (Rule 2).
constexpr int kPanelPadding = 20;
constexpr int kPanelCornerRadius = kItemRadius + kPanelPadding;

// The selected tab's favicon, title and domain, under the strip. M3 carousel
// items carry no text of their own at medium and small sizes, so the label
// belongs to the focal item and moves with the selection.
constexpr int kCaptionGap = 16;
constexpr int kCaptionHeight = 24;
constexpr int kCaptionSpacing = 8;
constexpr int kFaviconSize = 16;

// M3's FOCUS INDICATOR on the selected item, from Material's focus-ring
// tokens: a 3dp stroke in `secondary`, held 2dp outside the element, with the
// element's radius plus that offset — so the ring is concentric with what it
// surrounds by construction, not by a second number that has to agree.
constexpr float kRingWidth = 3.0f;
constexpr float kRingOffset = 2.0f;
// How far the ring reaches past its item. The strip is grown by this on every
// side so the ring is never cut off by the strip's own clip.
constexpr int kRingExtent = 5;
static_assert(kRingExtent == static_cast<int>(kRingOffset + kRingWidth));
static_assert(kPanelPadding >= kRingExtent,
              "the ring would reach past the panel's rounded edge");

// Kept clear on either side of the panel.
constexpr int kWindowMargin = 48;

// Far enough to reach every tab most people have open, without building views
// for every tab in a very large window.
constexpr size_t kMaxEntries = 50;

// HOT PATH. Tapping Tab with Ctrl held is among the most repeated motions in
// the browser, so this does not take M3's 350ms fast-spatial duration -- see
// the hot-path note in zephyrus_m3.h. It keeps the spatial CURVE, overshoot and
// all, because a carousel settling into place is what that curve is for.
constexpr base::TimeDelta kCycleDuration = base::Milliseconds(200);

// An item's corner radius at `size`: the extra-large shape, clamped so the
// corners never cross while an item is narrower than a full pill
// mid-animation. The ring derives its radius from this, so the two can never
// disagree.
SkScalar ItemRadiusFor(const gfx::Size& size) {
  return std::min<SkScalar>(
      kItemRadius, std::min(size.width(), size.height()) / 2.0f);
}

// 16:10, close enough to a browser viewport to read as one.
int HeightFor(int large) {
  return large * 10 / 16;
}

int WidthForOffset(int offset, int large) {
  const int small = std::clamp(large / 3, kSmallMin, kSmallMax);
  switch (std::abs(offset)) {
    case 0:
      return large;
    case 1:
      return (large + small) / 2;
    default:
      return small;
  }
}

int StripWidth(const std::vector<int>& offsets, int large) {
  int total = 0;
  for (int offset : offsets) {
    total += WidthForOffset(offset, large);
  }
  if (!offsets.empty()) {
    total += kItemGap * static_cast<int>(offsets.size() - 1);
  }
  return total;
}

// The largest focal item, stepping down from the preferred size, that lets
// `offsets` fit in `available`.
int FitLarge(const std::vector<int>& offsets, int available) {
  int large = kLargePreferred;
  while (large > kLargeMin && StripWidth(offsets, large) > available) {
    large -= 4;
  }
  return large;
}

// Which offsets from the selected tab get a slot, left to right, for `count`
// tabs: the selection, then its neighbours outward to `max_distance`, each tab
// at most once. Two tabs give {0, +1}; three give {-1, 0, +1}; five or more
// give the full {-2 .. +2}.
std::vector<int> OffsetsFor(size_t count, int max_distance) {
  const int n = static_cast<int>(count);
  std::vector<bool> used(count, false);
  std::vector<int> offsets;
  for (int distance = 0; distance <= max_distance; ++distance) {
    for (int offset : {distance, -distance}) {
      const int index = ((offset % n) + n) % n;
      if (!used[index]) {
        used[index] = true;
        offsets.push_back(offset);
      }
    }
  }
  std::sort(offsets.begin(), offsets.end());
  return offsets;
}

// Where tab `index` sits relative to the selection, wrapped into (-n/2, n/2].
// It has to agree with OffsetsFor, which prefers the positive side for the one
// ambiguous position when the count is even.
int SignedOffset(size_t index, size_t selected, size_t count) {
  const int n = static_cast<int>(count);
  int offset = (static_cast<int>(index) - static_cast<int>(selected)) % n;
  if (offset < 0) {
    offset += n;
  }
  if (offset > n / 2) {
    offset -= n;
  }
  return offset;
}

// Null when the site has given us nothing yet; the caller draws the globe.
gfx::ImageSkia GetTabFavicon(content::WebContents* contents) {
  if (auto* driver =
          favicon::ContentFaviconDriver::FromWebContents(contents)) {
    return driver->GetFavicon().AsImageSkia();
  }
  return gfx::ImageSkia();
}

// Only one switching session at a time.
ZephyrusTabSwitcher* g_switcher = nullptr;

}  // namespace

// One tab in the carousel: its page, masked to whatever width the item has.
class ZephyrusCarouselItem : public views::View {
  METADATA_HEADER(ZephyrusCarouselItem, views::View)

 public:
  ZephyrusCarouselItem() = default;
  ZephyrusCarouselItem(const ZephyrusCarouselItem&) = delete;
  ZephyrusCarouselItem& operator=(const ZephyrusCarouselItem&) = delete;
  ~ZephyrusCarouselItem() override = default;

  // The page is always drawn at `page_size` -- the large item's size -- and
  // centred, so a narrower item shows the middle slice of the same page.
  void SetPageSize(const gfx::Size& page_size) { page_size_ = page_size; }

  void SetPage(const gfx::ImageSkia& page) {
    page_ = page;
    SchedulePaint();
  }

  void SetPlaceholderColor(SkColor color) {
    placeholder_ = color;
    SchedulePaint();
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::Rect bounds = GetLocalBounds();
    if (bounds.IsEmpty()) {
      return;
    }
    const SkScalar radius = ItemRadiusFor(bounds.size());
    gfx::ScopedCanvas scoped(canvas);
    canvas->ClipPath(SkPath::RRect(SkRRect::MakeRectXY(
                         gfx::RectToSkRect(bounds), radius, radius)),
                     /*do_anti_alias=*/true);

    cc::PaintFlags fill;
    fill.setColor(placeholder_);
    canvas->DrawRect(bounds, fill);

    if (page_.isNull() || page_size_.IsEmpty()) {
      return;
    }
    const int x = bounds.CenterPoint().x() - page_size_.width() / 2;
    canvas->DrawImageInt(page_, 0, 0, page_.width(), page_.height(), x, 0,
                         page_size_.width(), page_size_.height(),
                         /*filter=*/true);
  }

 private:
  gfx::ImageSkia page_;
  gfx::Size page_size_;
  SkColor placeholder_ = SK_ColorTRANSPARENT;
};

BEGIN_METADATA(ZephyrusCarouselItem)
END_METADATA

// The focus indicator around the selected item. Its bounds are the item's
// bounds grown by kRingExtent on every side.
class ZephyrusCarouselRing : public views::View {
  METADATA_HEADER(ZephyrusCarouselRing, views::View)

 public:
  ZephyrusCarouselRing() { SetCanProcessEventsWithinSubtree(false); }
  ZephyrusCarouselRing(const ZephyrusCarouselRing&) = delete;
  ZephyrusCarouselRing& operator=(const ZephyrusCarouselRing&) = delete;
  ~ZephyrusCarouselRing() override = default;

  void SetColor(SkColor color) {
    color_ = color;
    SchedulePaint();
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::Size item(std::max(0, width() - 2 * kRingExtent),
                         std::max(0, height() - 2 * kRingExtent));
    if (item.IsEmpty()) {
      return;
    }
    // A stroke is centred on its path, so the path sits half a stroke inside
    // the ring's outer edge — and its radius is the item's plus the offset
    // plus that half stroke.
    gfx::RectF path(GetLocalBounds());
    path.Inset(kRingWidth / 2.0f);
    cc::PaintFlags stroke;
    stroke.setAntiAlias(true);
    stroke.setStyle(cc::PaintFlags::kStroke_Style);
    stroke.setStrokeWidth(kRingWidth);
    stroke.setColor(color_);
    canvas->DrawRoundRect(
        path, ItemRadiusFor(item) + kRingOffset + kRingWidth / 2.0f, stroke);
  }

 private:
  SkColor color_ = SK_ColorTRANSPARENT;
};

BEGIN_METADATA(ZephyrusCarouselRing)
END_METADATA

ZephyrusTabSwitcher::ZephyrusTabSwitcher(BrowserView* browser_view)
    : views::AnimationDelegateViews(this),
      browser_view_(browser_view),
      cycle_animation_(this) {
  // Full-window scrim, hidden until a session starts. Needs its own layer to
  // composite above the web contents.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetVisible(false);

  // The panel: an OPAQUE surfaceContainer popup, coloured in OnThemeChanged.
  // Its layer clips the carousel to the rounded panel, which is what gives the
  // strip clean edges as items grow out of them and shrink back in.
  panel_ = AddChildView(std::make_unique<views::View>());
  panel_->SetPaintToLayer();
  panel_->layer()->SetFillsBoundsOpaquely(false);
  panel_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kPanelCornerRadius));

  strip_ = panel_->AddChildView(std::make_unique<views::View>());

  caption_ = panel_->AddChildView(std::make_unique<views::View>());
  auto* caption_layout =
      caption_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kCaptionSpacing));
  caption_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  caption_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  caption_favicon_ =
      caption_->AddChildView(std::make_unique<views::ImageView>());
  caption_favicon_->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));

  caption_title_ = caption_->AddChildView(std::make_unique<views::Label>());
  caption_title_->SetFontList(
      zephyrus::m3::Font(zephyrus::m3::Type::kTitleMedium));
  caption_domain_ = caption_->AddChildView(std::make_unique<views::Label>());
  caption_domain_->SetFontList(
      zephyrus::m3::Font(zephyrus::m3::Type::kBodyMedium));
  for (views::Label* label : {caption_title_.get(), caption_domain_.get()}) {
    label->SetAutoColorReadabilityEnabled(false);
    // The panel's layer is non-opaque (its corners are transparent), and
    // subpixel antialiasing needs an opaque backing. Views DCHECKs on the pair.
    label->SetSubpixelRenderingEnabled(false);
    label->SetElideBehavior(gfx::ELIDE_TAIL);
    label->SetMultiLine(false);
  }
}

ZephyrusTabSwitcher::~ZephyrusTabSwitcher() {
  // The view outlives individual sessions, so this only runs at window
  // teardown — but the handler must still come off, or it dangles.
  if (handler_target_) {
    handler_target_->RemovePreTargetHandler(&key_watcher_);
    handler_target_ = nullptr;
  }
  if (g_switcher == this) {
    g_switcher = nullptr;
  }
}

void ZephyrusTabSwitcher::OnThemeChanged() {
  views::View::OnThemeChanged();
  panel_->SetBackground(views::CreateRoundedRectBackground(
      zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainer),
      kPanelCornerRadius));
  const SkColor placeholder =
      zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHighest);
  for (Entry& entry : entries_) {
    if (entry.item) {
      entry.item->SetPlaceholderColor(placeholder);
    }
  }
  if (ring_) {
    ring_->SetColor(zephyrus::m3::Role(*this, kColorZephyrusSecondary));
  }
  UpdateCaption();
}

void ZephyrusTabSwitcher::ClearEntries() {
  // A stopped animation leaves items where they are and calls nothing that
  // needs them, so this is safe before the entries go.
  cycle_animation_.Stop();
  // Entries first: they hold raw_ptrs into the item views, so dropping them
  // before the views means those pointers never dangle.
  entries_.clear();
  ring_ = nullptr;
  selected_ = 0;
  placed_ = false;
  geometry_ = Geometry();
  if (strip_) {
    strip_->RemoveAllChildViews();
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
  if (!panel_ || geometry_.strip_width == 0) {
    return;
  }
  const int panel_width =
      std::min(geometry_.strip_width + 2 * kPanelPadding, width());
  const int panel_height = kPanelPadding + geometry_.height + kCaptionGap +
                           kCaptionHeight + kPanelPadding;
  panel_->SetBounds((width() - panel_width) / 2,
                    std::max(0, (height() - panel_height) / 2), panel_width,
                    panel_height);
  // Grown by the ring's reach on every side, inside the panel padding, so the
  // ring is never clipped. Items are placed kRingExtent in from its edges.
  strip_->SetBounds(kPanelPadding - kRingExtent, kPanelPadding - kRingExtent,
                    geometry_.strip_width + 2 * kRingExtent,
                    geometry_.height + 2 * kRingExtent);
  caption_->SetBounds(kPanelPadding,
                      kPanelPadding + geometry_.height + kCaptionGap,
                      geometry_.strip_width, kCaptionHeight);
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
  }

  if (tabs.size() < 2) {
    return false;  // A switcher for one tab is UI for nothing.
  }

  // RECENCY ORDER, most recent first, which makes the first Tab land on the
  // tab you were on before this one — the Alt+Tab contract.
  //
  // GetLastActiveTimeTicks() is set when a tab is shown, but ALSO when it is
  // created, so a link just opened in the background ranks as recent even if
  // it was never looked at. That is Chromium's own notion of recency, used by
  // its tab search, and it is kept rather than second-guessed here. The active
  // tab is pinned first explicitly rather than trusted to have the newest
  // timestamp; stable_sort keeps tab-strip order for exact ties.
  content::WebContents* const active_contents = model->GetActiveWebContents();
  std::stable_sort(tabs.begin(), tabs.end(),
                   [active_contents](content::WebContents* a,
                                     content::WebContents* b) {
                     if ((a == active_contents) != (b == active_contents)) {
                       return a == active_contents;
                     }
                     return a->GetLastActiveTimeTicks() >
                            b->GetLastActiveTimeTicks();
                   });
  // The cap bounds how many item views one session builds. In recency order
  // it simply keeps the most recently used tabs.
  if (tabs.size() > kMaxEntries) {
    tabs.resize(kMaxEntries);
  }

  // Fit the carousel to the window. The edge slivers are the first thing to go
  // in a narrow window: a strip of medium-large-medium still browses, a large
  // item squeezed below its minimum does not.
  const int available =
      browser_view_ ? std::max(0, browser_view_->width() -
                                      2 * (kWindowMargin + kPanelPadding))
                    : kLargePreferred * 3;
  std::vector<int> offsets = OffsetsFor(tabs.size(), 2);
  int large = FitLarge(offsets, available);
  if (StripWidth(offsets, large) > available) {
    offsets = OffsetsFor(tabs.size(), 1);
    large = FitLarge(offsets, available);
  }
  geometry_.large = large;
  geometry_.height = HeightFor(large);
  geometry_.strip_width = StripWidth(offsets, large);
  geometry_.offsets = std::move(offsets);

  const SkColor placeholder =
      zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHighest);
  for (content::WebContents* contents : tabs) {
    auto* item =
        strip_->AddChildView(std::make_unique<ZephyrusCarouselItem>());
    item->SetPageSize(gfx::Size(geometry_.large, geometry_.height));
    item->SetPlaceholderColor(placeholder);
    Entry entry;
    entry.contents = contents;
    entry.item = item;
    entries_.push_back(std::move(entry));
  }

  ring_ = strip_->AddChildView(std::make_unique<ZephyrusCarouselRing>());
  ring_->SetColor(zephyrus::m3::Role(*this, kColorZephyrusSecondary));

  // Set once per session rather than in Layout(): a maximum width that changes
  // invalidates the label's preferred size, and doing that from Layout() is
  // how a layout pass re-enters itself.
  caption_title_->SetMaximumWidth(geometry_.strip_width * 3 / 5);
  caption_domain_->SetMaximumWidth(geometry_.strip_width / 3);

  // Start on the active tab, which recency order has put first, so the first
  // Advance moves to the previously used tab.
  content::WebContents* active = model->GetActiveWebContents();
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].contents == active) {
      selected_ = i;
      break;
    }
  }
  return true;
}

void ZephyrusTabSwitcher::EnsureThumbnail(size_t index) {
  if (index >= entries_.size() || entries_[index].thumbnail_requested) {
    return;
  }
  Entry& entry = entries_[index];
  entry.thumbnail_requested = true;
  ThumbnailTabHelper* helper = ThumbnailTabHelper::FromWebContents(entry.contents);
  if (!helper) {
    return;
  }
  scoped_refptr<ThumbnailImage> thumbnail = helper->thumbnail();
  if (!thumbnail) {
    return;
  }
  entry.subscription = thumbnail->Subscribe();
  // At the LARGE size: every item masks the same full-size page.
  entry.subscription->SetSizeHint(
      gfx::Size(geometry_.large, geometry_.height));
  entry.subscription->SetUncompressedImageCallback(
      base::BindRepeating(&ZephyrusTabSwitcher::OnThumbnailReceived,
                          weak_factory_.GetWeakPtr(), index));
  // Delivery is async; the placeholder shows until it lands.
  thumbnail->RequestThumbnailImage();
}

void ZephyrusTabSwitcher::OnThumbnailReceived(size_t index,
                                              gfx::ImageSkia image) {
  if (index >= entries_.size() || image.isNull()) {
    return;
  }
  if (ZephyrusCarouselItem* item = entries_[index].item) {
    item->SetPage(image);
  }
}

void ZephyrusTabSwitcher::AdvanceSelection(bool forward) {
  if (entries_.empty()) {
    return;
  }
  const size_t count = entries_.size();
  selected_ =
      forward ? (selected_ + 1) % count : (selected_ + count - 1) % count;
  ApplyLayout(/*animate=*/placed_);
  placed_ = true;
  UpdateCaption();
}

void ZephyrusTabSwitcher::ApplyLayout(bool animate) {
  const size_t count = entries_.size();
  if (count == 0 || geometry_.offsets.empty()) {
    return;
  }

  // The slot for each visible offset, left to right, in strip coordinates.
  std::vector<std::pair<int, gfx::Rect>> slots;
  int x = kRingExtent;
  for (int offset : geometry_.offsets) {
    const int width = WidthForOffset(offset, geometry_.large);
    slots.emplace_back(offset,
                       gfx::Rect(x, kRingExtent, width, geometry_.height));
    x += width + kItemGap;
  }
  // An item outside the slots waits at zero width on the edge it will come in
  // from or has just left, so it grows out of that edge and shrinks back into
  // it rather than sliding across the strip.
  const auto edge = [this](int offset) {
    return gfx::Rect(
        kRingExtent + (offset < 0 ? 0 : geometry_.strip_width), kRingExtent,
        0, geometry_.height);
  };

  for (size_t i = 0; i < count; ++i) {
    Entry& entry = entries_[i];
    if (!entry.item) {
      continue;
    }
    const int offset = SignedOffset(i, selected_, count);
    const auto slot =
        std::find_if(slots.begin(), slots.end(),
                     [offset](const auto& s) { return s.first == offset; });
    const bool visible = slot != slots.end();
    const gfx::Rect current = entry.item->bounds();
    const bool was_visible = !current.IsEmpty();

    entry.to = visible ? slot->second : edge(offset);
    if (!animate) {
      entry.from = entry.to;
    } else if (was_visible) {
      entry.from = current;
    } else if (visible) {
      // Entering: from the edge on its NEW side, which need not be the edge it
      // last left by.
      entry.from = edge(offset);
    } else {
      entry.from = entry.to;
    }

    // Ask for a thumbnail one step before an item comes into view, so it has
    // usually arrived by the time it is shown.
    if (offset >= geometry_.offsets.front() - 1 &&
        offset <= geometry_.offsets.back() + 1) {
      EnsureThumbnail(i);
    }
  }

  // Z-ORDER. The focal item is in front and each step out sits further back,
  // as an M3 carousel stacks them. It matters whenever an item crosses the
  // strip -- with five tabs or fewer every tab is on screen, so the one leaving
  // one edge travels to the other, and it should pass BEHIND the others.
  std::vector<size_t> order(count);
  std::iota(order.begin(), order.end(), size_t{0});
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return std::abs(SignedOffset(a, selected_, count)) >
           std::abs(SignedOffset(b, selected_, count));
  });
  for (size_t z = 0; z < order.size(); ++z) {
    if (ZephyrusCarouselItem* item = entries_[order[z]].item) {
      strip_->ReorderChildView(item, z);
    }
  }
  if (ring_) {
    strip_->ReorderChildView(ring_, strip_->children().size() - 1);
  }

  // A tap mid-animation restarts from where every item currently is. Stop()
  // leaves them there, which is exactly the `from` captured above.
  cycle_animation_.Stop();
  if (animate) {
    cycle_animation_.SetDuration(kCycleDuration);
    cycle_animation_.Start();
  } else {
    ApplyProgress(1.0);
  }
}

void ZephyrusTabSwitcher::ApplyProgress(double progress) {
  const auto lerp = [progress](int a, int b) {
    return static_cast<int>(std::lround(a + (b - a) * progress));
  };
  for (Entry& entry : entries_) {
    if (!entry.item) {
      continue;
    }
    // Every item shares one progress value, and every slot's gap is the same
    // at both ends, so the gaps hold even while the curve overshoots. Only a
    // shrinking width can go negative, and that is clamped.
    entry.item->SetBounds(lerp(entry.from.x(), entry.to.x()), entry.to.y(),
                          std::max(0, lerp(entry.from.width(), entry.to.width())),
                          entry.to.height());
  }
  // The ring jumps to the NEW selection at once and then grows with it, so it
  // is always on the tab Ctrl-up would open, never trailing the old one.
  if (ring_ && selected_ < entries_.size() && entries_[selected_].item) {
    gfx::Rect ring = entries_[selected_].item->bounds();
    ring.Inset(-kRingExtent);
    ring_->SetBoundsRect(ring);
  }
}

void ZephyrusTabSwitcher::AnimationProgressed(const gfx::Animation* animation) {
  ApplyProgress(zephyrus::m3::Curve(zephyrus::m3::Spring::kFastSpatial)
                    .Solve(animation->GetCurrentValue()));
}

void ZephyrusTabSwitcher::UpdateCaption() {
  // Colours are roles, so this waits for a Widget like everything else here.
  if (selected_ >= entries_.size() || !GetWidget()) {
    return;
  }
  content::WebContents* const contents = entries_[selected_].contents;
  if (!contents) {
    return;
  }
  const GURL url = contents->GetVisibleURL();
  const std::u16string domain = base::UTF8ToUTF16(url.host());
  std::u16string title = contents->GetTitle();
  if (title.empty()) {
    title = domain;
  }

  const SkColor on_surface = zephyrus::m3::Role(*this, kColorZephyrusOnSurface);
  const SkColor on_variant =
      zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant);

  caption_title_->SetText(title);
  caption_title_->SetEnabledColor(on_surface);
  caption_domain_->SetText(domain);
  caption_domain_->SetEnabledColor(on_variant);
  // Pages with no title fall back to the domain; saying it twice is noise.
  caption_domain_->SetVisible(!domain.empty() && domain != title);

  // A real favicon is an IMAGE and is never recoloured; only the fallback
  // globe takes a role.
  const gfx::ImageSkia favicon = GetTabFavicon(contents);
  caption_favicon_->SetImage(
      favicon.isNull() ? ui::ImageModel::FromVectorIcon(
                             vector_icons::kGlobeIcon, on_variant, kFaviconSize)
                       : ui::ImageModel::FromImageSkia(favicon));
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
