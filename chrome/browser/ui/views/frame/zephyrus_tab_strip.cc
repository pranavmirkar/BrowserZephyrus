// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_tab_strip.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_icons.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "components/url_formatter/url_formatter.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/vector_icons.h"

namespace {

// M3 Expressive CONNECTED BUTTON GROUP. The tabs are one group of filled
// segments with 2dp between them: the group's two ends are fully rounded, the
// corners where segments meet are tight, and the active tab fills with primary
// and morphs into a full capsule -- it is the one segment whose shape and
// colour both change.
//
// Compact on purpose: 28dp segments in a 36dp strip. The strip is chrome the
// page gives up height for, on every window, all the time.
constexpr int kTabHeight = 28;
constexpr int kEdge = 4;
// Between segments of one group, and between the pinned group and the rest.
constexpr int kSegmentGap = 2;
constexpr int kGroupGap = 8;
constexpr int kMinTabWidth = 64;
constexpr int kMaxTabWidth = 200;
constexpr int kPinnedWidth = 36;
// The split-view seam: a narrow segment between a split pair's two tabs.
constexpr int kSeamWidth = 24;
constexpr int kFaviconSize = 16;
constexpr int kCloseSize = 18;
// Below this an idle tab shows no close button: a narrow segment cannot hold
// a favicon, a readable title AND a target.
constexpr int kCloseMinWidth = 96;

constexpr float kCapsuleRadius = kTabHeight / 2.f;
// Where two segments meet. M3's connected group uses a small fixed inner
// radius; 6 at this height keeps the seam readable without a gap looking like
// a crack.
constexpr float kInnerRadius = 6.f;

constexpr base::TimeDelta kMorphDuration = base::Milliseconds(200);

// Context-menu commands.
enum Command {
  kCommandPin = 1,
  kCommandClose,
  kCommandCloseOthers,
  kCommandMoveBase = 100,
};

// A split pair's segments take the tertiary tone, so the pair reads as one
// thing inside the group without a container around it.
enum class Tone { kNormal, kSplit };

gfx::ImageSkia GetFavicon(content::WebContents* contents) {
  if (auto* driver =
          favicon::ContentFaviconDriver::FromWebContents(contents)) {
    return driver->GetFavicon().AsImageSkia();
  }
  return gfx::ImageSkia();
}

std::u16string GetTitle(content::WebContents* contents) {
  std::u16string title = contents->GetTitle();
  if (title.empty()) {
    // Formatted, never the raw spec: that would print credentials carried in
    // the URL into a strip anyone looking at the screen can read.
    title = url_formatter::FormatUrl(
        contents->GetVisibleURL(),
        url_formatter::kFormatUrlOmitUsernamePassword |
            url_formatter::kFormatUrlOmitHTTPS,
        base::UnescapeRule::SPACES, nullptr, nullptr, nullptr);
  }
  return title;
}

SkRRect SegmentShape(const gfx::Rect& bounds, float lead, float trail) {
  const float cap = bounds.height() / 2.f;
  lead = std::min(lead, cap);
  trail = std::min(trail, cap);
  const SkVector radii[4] = {{lead, lead}, {trail, trail}, {trail, trail},
                             {lead, lead}};
  SkRRect rrect;
  rrect.setRectRadii(gfx::RectToSkRect(bounds), radii);
  return rrect;
}

// A segment of the group: where it sits decides its outer corners.
class Segment : public views::Button {
  METADATA_HEADER(Segment, views::Button)

 public:
  using views::Button::Button;

  void SetGroupEnds(bool first, bool last) {
    first_ = first;
    last_ = last;
    SchedulePaint();
  }

 protected:
  float LeadRadius() const { return first_ ? kCapsuleRadius : kInnerRadius; }
  float TrailRadius() const { return last_ ? kCapsuleRadius : kInnerRadius; }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    InvalidateLayout();
    SchedulePaint();
  }

  bool first_ = false;
  bool last_ = false;
};

BEGIN_METADATA(Segment)
END_METADATA

// The close control: an M3 icon button whose hover is a state layer of the
// segment's own ink, never a colour of its own.
class StripCloseButton : public views::ImageButton {
  METADATA_HEADER(StripCloseButton, views::ImageButton)

 public:
  explicit StripCloseButton(PressedCallback callback)
      : views::ImageButton(std::move(callback)) {
    SetImageHorizontalAlignment(ALIGN_CENTER);
    SetImageVerticalAlignment(ALIGN_MIDDLE);
    // Out of the tab order: focus moves tab to tab, and Delete closes the
    // focused tab (see StripTab::OnKeyPressed).
    SetFocusBehavior(FocusBehavior::NEVER);
    SetTooltipText(u"Close tab");
    GetViewAccessibility().SetName(u"Close tab");
    views::InstallCircleHighlightPathGenerator(this);
  }

  void SetInk(SkColor ink) {
    ink_ = ink;
    SetImageModel(STATE_NORMAL, ui::ImageModel::FromVectorIcon(
                                    views::kCloseIcon, ink, 12));
    SchedulePaint();
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kCloseSize, kCloseSize);
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const bool hovered = GetState() == STATE_HOVERED;
    const bool pressed = GetState() == STATE_PRESSED;
    if (!hovered && !pressed) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(zephyrus::m3::StateLayer(
        ink_, pressed ? zephyrus::m3::kPressed : zephyrus::m3::kHover));
    const gfx::RectF bounds(GetLocalBounds());
    canvas->DrawCircle(bounds.CenterPoint(), bounds.width() / 2.f, flags);
  }

 private:
  SkColor ink_ = SK_ColorTRANSPARENT;
};

BEGIN_METADATA(StripCloseButton)
END_METADATA

// One tab: a filled segment. The active one fills with primary and morphs to
// a capsule.
//
// views::Button is already an animation delegate (AnimationDelegateViews); a
// second gfx::AnimationDelegate base would be ambiguous.
class StripTab : public Segment {
  METADATA_HEADER(StripTab, Segment)

 public:
  StripTab(ZephyrusTabStrip* strip,
           content::WebContents* contents,
           bool active,
           bool pinned,
           Tone tone,
           bool morph_in)
      : Segment(base::BindRepeating(&StripTab::OnPressed,
                                    base::Unretained(this))),
        strip_(strip),
        contents_(contents->GetWeakPtr()),
        active_(active),
        pinned_(pinned),
        tone_(tone),
        morph_(this) {
    const std::u16string title = GetTitle(contents);
    GetViewAccessibility().SetRole(ax::mojom::Role::kTab);
    GetViewAccessibility().SetName(title.empty() ? u"Tab" : title);
    GetViewAccessibility().SetIsSelected(active);
    // Pinned tabs show no title, so the tooltip is their only name; others
    // show it when the title is cut off (see Layout).
    if (pinned_) {
      SetTooltipText(title);
    }
    SetNotifyEnterExitOnChild(true);
    // Middle-click closes, as in every tab strip.
    SetTriggerableEventFlags(ui::EF_LEFT_MOUSE_BUTTON |
                             ui::EF_MIDDLE_MOUSE_BUTTON);

    favicon_ = GetFavicon(contents);

    if (!pinned_) {
      label_ = AddChildView(std::make_unique<views::Label>(title));
      label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      label_->SetElideBehavior(gfx::ELIDE_TAIL);
      label_->SetAutoColorReadabilityEnabled(false);
      // Transparent background: subpixel text needs an opaque one.
      label_->SetSubpixelRenderingEnabled(false);
      label_->SetCanProcessEventsWithinSubtree(false);
      label_->SetFontList(label_->font_list().DeriveWithSizeDelta(-1));

      close_ = AddChildView(std::make_unique<StripCloseButton>(
          base::BindRepeating(&StripTab::Close, base::Unretained(this))));
    }

    morph_.SetSlideDuration(gfx::Animation::ShouldRenderRichAnimation()
                                ? kMorphDuration
                                : base::TimeDelta());
    morph_.SetTweenType(
        zephyrus::m3::TweenFor(zephyrus::m3::Spring::kFastSpatial));
    if (active_ && morph_in) {
      morph_.Show();
    } else {
      morph_.Reset(active_ ? 1.0 : 0.0);
    }
  }
  StripTab(const StripTab&) = delete;
  StripTab& operator=(const StripTab&) = delete;
  ~StripTab() override = default;

  content::WebContents* contents() const { return contents_.get(); }
  bool is_pinned() const { return pinned_; }
  bool in_split() const { return tone_ == Tone::kSplit; }

  // views::Button:
  void OnThemeChanged() override {
    Segment::OnThemeChanged();
    const SkColor ink = Ink();
    if (label_) {
      label_->SetEnabledColor(ink);
    }
    if (close_) {
      close_->SetInk(ink);
    }
  }

  void Layout(PassKey key) override {
    if (!label_) {
      return;
    }
    const gfx::Rect bounds = GetContentsBounds();
    const bool show_close =
        close_ && (active_ || IsMouseHovered() || HasFocus()) &&
        width() >= (active_ ? kMinTabWidth : kCloseMinWidth);
    close_->SetVisible(show_close);
    // The capsule ends need a little more room than a square seam.
    int right = bounds.right() - (active_ || last_ ? 8 : 5);
    if (show_close) {
      close_->SetBounds(right - kCloseSize,
                        bounds.y() + (bounds.height() - kCloseSize) / 2,
                        kCloseSize, kCloseSize);
      right -= kCloseSize + 2;
    }
    const int label_x = bounds.x() + ContentStart() + kFaviconSize + 6;
    label_->SetBounds(label_x, bounds.y(), std::max(0, right - label_x),
                      bounds.height());
    // A tooltip only when the title does not fit: one repeating a fully
    // visible title just covers the row below.
    SetTooltipText(label_->IsDisplayTextTruncated()
                       ? std::u16string(label_->GetText())
                       : std::u16string());
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const double t = morph_.GetCurrentValue();
    // Shape: this segment's place in the group, morphing to a capsule as it
    // becomes the active one.
    const float lead = static_cast<float>(
        gfx::Tween::FloatValueBetween(t, LeadRadius(), kCapsuleRadius));
    const float trail = static_cast<float>(
        gfx::Tween::FloatValueBetween(t, TrailRadius(), kCapsuleRadius));

    // The active tab is secondaryContainer, the idle ones sit a step below
    // on surfaceContainerHigh. It was full PRIMARY -- the brightest tone in the
    // palette, a pale block at the top of every window -- which the shape
    // change already made redundant as a marker, and which was glaring on a
    // dark theme.
    SkColor fill = color_utils::AlphaBlend(
        zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer),
        IdleContainer(), static_cast<float>(t));
    const bool hovered = GetState() == STATE_HOVERED;
    const bool pressed = GetState() == STATE_PRESSED;
    if (hovered || pressed) {
      fill = zephyrus::m3::WithStateLayer(
          fill, Ink(), pressed ? zephyrus::m3::kPressed : zephyrus::m3::kHover);
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(fill);
    canvas->sk_canvas()->drawRRect(SegmentShape(GetLocalBounds(), lead, trail),
                                   flags);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::Rect bounds = GetContentsBounds();
    const int x = pinned_ ? bounds.x() + (bounds.width() - kFaviconSize) / 2
                          : bounds.x() + ContentStart();
    const int y = bounds.y() + (bounds.height() - kFaviconSize) / 2;
    if (!favicon_.isNull()) {
      canvas->DrawImageInt(favicon_, 0, 0, favicon_.width(), favicon_.height(),
                           x, y, kFaviconSize, kFaviconSize, true);
    } else {
      const gfx::ImageSkia globe = ui::ImageModel::FromVectorIcon(
                                       vector_icons::kGlobeIcon, Ink(),
                                       kFaviconSize)
                                       .Rasterize(GetColorProvider());
      canvas->DrawImageInt(globe, x, y);
    }
  }

  void OnFocus() override {
    Segment::OnFocus();
    InvalidateLayout();
  }
  void OnBlur() override {
    Segment::OnBlur();
    InvalidateLayout();
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (event.key_code() == ui::VKEY_DELETE) {
      Close(event);
      return true;
    }
    return Segment::OnKeyPressed(event);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    press_x_ = event.x();
    dragging_ = false;
    return Segment::OnMousePressed(event);
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    // Pinned tabs and split halves stay put: a pinned tab's place is the
    // pinned group, and dragging half a pair out of it is breaking the split,
    // which has its own control.
    if (pinned_ || in_split()) {
      return Segment::OnMouseDragged(event);
    }
    if (!dragging_ && std::abs(event.x() - press_x_) < 6) {
      return Segment::OnMouseDragged(event);
    }
    dragging_ = true;
    gfx::Point point = event.location();
    views::View::ConvertPointToTarget(this, strip_, &point);
    strip_->OnTabDragged(this, point.x() - press_x_);
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (dragging_) {
      dragging_ = false;
      // May delete this view; nothing below touches it.
      strip_->OnTabDragEnded(this, contents_);
      return;
    }
    Segment::OnMouseReleased(event);
  }

  void OnMouseCaptureLost() override {
    if (dragging_) {
      dragging_ = false;
      strip_->OnTabDragEnded(this, contents_);
      return;
    }
    Segment::OnMouseCaptureLost();
  }

  // views::AnimationDelegateViews (via Button): the morph repaints.
  void AnimationProgressed(const gfx::Animation* animation) override {
    Segment::AnimationProgressed(animation);
    SchedulePaint();
  }

 private:
  // A capsule end rounds away the leading inset, so the content sits further
  // in there than beside a tight seam.
  int ContentStart() const { return active_ || first_ ? 10 : 8; }

  SkColor IdleContainer() const {
    return zephyrus::m3::Role(*this, tone_ == Tone::kSplit
                                         ? kColorZephyrusTertiaryContainer
                                         : kColorZephyrusSurfaceContainerHigh);
  }

  SkColor Ink() const {
    if (active_) {
      return zephyrus::m3::Role(*this, kColorZephyrusOnSecondaryContainer);
    }
    return zephyrus::m3::Role(*this, tone_ == Tone::kSplit
                                         ? kColorZephyrusOnTertiaryContainer
                                         : kColorZephyrusOnSurfaceVariant);
  }

  void OnPressed(const ui::Event& event) {
    if (event.IsMouseEvent() &&
        static_cast<const ui::MouseEvent&>(event).IsMiddleMouseButton()) {
      Close(event);
      return;
    }
    strip_->ActivateTab(contents_);
  }

  void Close(const ui::Event& event) { strip_->CloseTab(contents_); }

  raw_ptr<ZephyrusTabStrip> strip_;
  base::WeakPtr<content::WebContents> contents_;
  const bool active_;
  const bool pinned_;
  const Tone tone_;
  gfx::ImageSkia favicon_;
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<StripCloseButton> close_ = nullptr;
  gfx::SlideAnimation morph_;
  int press_x_ = 0;
  bool dragging_ = false;
};

BEGIN_METADATA(StripTab)
END_METADATA

// The seam of a split pair: a narrow segment, in the pair's tertiary tone,
// between its two tabs. It carries the split glyph and is the control that
// separates them -- where the pair is joined is where it comes apart.
class SplitSeam : public Segment {
  METADATA_HEADER(SplitSeam, Segment)

 public:
  SplitSeam(ZephyrusTabStrip* strip, split_tabs::SplitTabId split_id)
      : Segment(base::BindRepeating(
            [](ZephyrusTabStrip* strip, split_tabs::SplitTabId id,
               const ui::Event&) { strip->BreakSplit(id); },
            base::Unretained(strip), split_id)) {
    SetTooltipText(u"Separate these tabs");
    GetViewAccessibility().SetName(u"Split view: separate these tabs");
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    SkColor fill =
        zephyrus::m3::Role(*this, kColorZephyrusTertiaryContainer);
    if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      fill = zephyrus::m3::WithStateLayer(
          fill, zephyrus::m3::Role(*this, kColorZephyrusOnTertiaryContainer),
          GetState() == STATE_PRESSED ? zephyrus::m3::kPressed
                                      : zephyrus::m3::kHover);
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(fill);
    canvas->sk_canvas()->drawRRect(
        SegmentShape(GetLocalBounds(), LeadRadius(), TrailRadius()), flags);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::ImageSkia glyph =
        ui::ImageModel::FromVectorIcon(
            kSplitSceneIcon,
            zephyrus::m3::Role(*this, kColorZephyrusOnTertiaryContainer), 14)
            .Rasterize(GetColorProvider());
    canvas->DrawImageInt(glyph, (width() - glyph.width()) / 2,
                         (height() - glyph.height()) / 2);
  }
};

BEGIN_METADATA(SplitSeam)
END_METADATA

// The trailing "+": a standalone icon button, clear of the group.
class NewTabChip : public views::ImageButton {
  METADATA_HEADER(NewTabChip, views::ImageButton)

 public:
  explicit NewTabChip(PressedCallback callback)
      : views::ImageButton(std::move(callback)) {
    SetImageHorizontalAlignment(ALIGN_CENTER);
    SetImageVerticalAlignment(ALIGN_MIDDLE);
    SetTooltipText(u"New tab");
    GetViewAccessibility().SetName(u"New tab");
    views::InstallCircleHighlightPathGenerator(this);
  }

  void OnThemeChanged() override {
    views::ImageButton::OnThemeChanged();
    SetImageModel(STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(
                      kAddIcon,
                      zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant),
                      16));
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const bool hovered = GetState() == STATE_HOVERED;
    const bool pressed = GetState() == STATE_PRESSED;
    if (!hovered && !pressed) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(zephyrus::m3::StateLayer(
        zephyrus::m3::Role(*this, kColorZephyrusOnSurface),
        pressed ? zephyrus::m3::kPressed : zephyrus::m3::kHover));
    const gfx::RectF bounds(GetLocalBounds());
    canvas->DrawRoundRect(bounds, bounds.height() / 2.f, flags);
  }
};

BEGIN_METADATA(NewTabChip)
END_METADATA

// Width of an item in the flexible group, given the per-tab unit.
int ItemWidth(views::View* item, int unit) {
  return views::IsViewClass<SplitSeam>(item) ? kSeamWidth : unit;
}

}  // namespace

ZephyrusTabStrip::ZephyrusTabStrip(BrowserView* browser_view)
    : browser_view_(browser_view),
      tab_strip_model_(browser_view->browser()->tab_strip_model()) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kTabList);
  GetViewAccessibility().SetName(u"Tabs");
  tab_strip_model_->AddObserver(this);
  if (ZephyrusWorkspaceManager* manager =
          browser_view_->zephyrus_workspace_manager()) {
    // A workspace switch changes WHICH tabs belong here without necessarily
    // touching the strip model.
    workspace_subscription_ = manager->RegisterChangedCallback(
        base::BindRepeating(&ZephyrusTabStrip::ScheduleRebuild,
                            weak_factory_.GetWeakPtr()));
  }
  ScheduleRebuild();
}

ZephyrusTabStrip::~ZephyrusTabStrip() {
  tab_strip_model_->RemoveObserver(this);
}

void ZephyrusTabStrip::ScheduleRebuild() {
  // Held for the whole of a drag: rebuilding deletes the view being dragged.
  if (dragging_) {
    rebuild_deferred_ = true;
    return;
  }
  // Nothing to show while this layout is not the active one; becoming
  // visible rebuilds (VisibilityChanged).
  if (!GetVisible() || rebuild_pending_) {
    return;
  }
  rebuild_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ZephyrusTabStrip::Rebuild,
                                weak_factory_.GetWeakPtr()));
}

void ZephyrusTabStrip::Rebuild() {
  rebuild_pending_ = false;
  if (dragging_) {
    rebuild_deferred_ = true;
    return;
  }
  RemoveAllChildViews();
  items_.clear();
  new_tab_button_ = nullptr;
  first_flexible_ = 0;

  ZephyrusWorkspaceManager* manager =
      browser_view_->zephyrus_workspace_manager();
  content::WebContents* const active_contents =
      tab_strip_model_->GetActiveWebContents();
  const bool morph = active_contents && last_active_.get() != active_contents;
  last_active_ = active_contents ? active_contents->GetWeakPtr()
                                 : base::WeakPtr<content::WebContents>();

  std::vector<int> pinned;
  std::vector<int> unpinned;
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    content::WebContents* contents = tab_strip_model_->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    // This workspace's tabs only. Pinning is per workspace too (a pinned tab
    // is still one WebContents in one cookie jar).
    if (manager && !manager->IsContentsInCurrentWorkspace(contents)) {
      continue;
    }
    (tab_strip_model_->IsTabPinned(i) ? pinned : unpinned).push_back(i);
  }

  auto make_tab = [&](int index, Tone tone) {
    content::WebContents* contents = tab_strip_model_->GetWebContentsAt(index);
    auto tab = std::make_unique<StripTab>(
        this, contents, contents == active_contents,
        tab_strip_model_->IsTabPinned(index), tone,
        morph && contents == active_contents);
    tab->set_context_menu_controller(this);
    return tab;
  };

  for (int index : pinned) {
    items_.push_back(AddChildView(make_tab(index, Tone::kNormal)));
  }
  first_flexible_ = items_.size();

  std::set<int> consumed;
  for (int index : unpinned) {
    if (consumed.count(index)) {
      continue;
    }
    // A split whose members are both here: tab, seam, tab, in the pair's
    // own tone.
    tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(index);
    std::optional<split_tabs::SplitTabId> split =
        tab ? tab->GetSplit() : std::nullopt;
    split_tabs::SplitTabData* data =
        split ? tab_strip_model_->GetSplitData(*split) : nullptr;
    std::vector<int> members;
    if (data) {
      for (tabs::TabInterface* member : data->ListTabs()) {
        const int member_index = tab_strip_model_->GetIndexOfTab(member);
        if (member_index >= 0 &&
            std::find(unpinned.begin(), unpinned.end(), member_index) !=
                unpinned.end()) {
          members.push_back(member_index);
        }
      }
    }
    if (members.size() == 2) {
      items_.push_back(AddChildView(make_tab(members[0], Tone::kSplit)));
      items_.push_back(
          AddChildView(std::make_unique<SplitSeam>(this, *split)));
      items_.push_back(AddChildView(make_tab(members[1], Tone::kSplit)));
      consumed.insert(members.begin(), members.end());
      continue;
    }
    items_.push_back(AddChildView(make_tab(index, Tone::kNormal)));
  }

  // Outer corners: the first and last segment of each group are rounded.
  auto mark_group = [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
      if (auto* segment = views::AsViewClass<Segment>(items_[i].get())) {
        segment->SetGroupEnds(i == begin, i + 1 == end);
      }
    }
  };
  mark_group(0, first_flexible_);
  mark_group(first_flexible_, items_.size());

  new_tab_button_ = AddChildView(std::make_unique<NewTabChip>(
      base::BindRepeating([](ZephyrusTabStrip* strip,
                             const ui::Event&) { strip->NewTab(); },
                          base::Unretained(this))));
  InvalidateLayout();
  ScrollActiveIntoView();
}

int ZephyrusTabStrip::ContentWidth() const {
  return std::max(0, width() - 2 * kEdge);
}

void ZephyrusTabStrip::SetTopInset(int inset) {
  if (inset != top_inset_) {
    top_inset_ = inset;
    InvalidateLayout();
  }
}

void ZephyrusTabStrip::Layout(PassKey key) {
  const int y = top_inset_;
  const int avail = ContentWidth();
  const bool has_pinned = first_flexible_ > 0;
  const bool has_flexible = first_flexible_ < items_.size();

  // The flexible group divides what the pinned group, the seams, the gaps
  // and "+" leave.
  int tabs = 0;
  int fixed = kTabHeight + kGroupGap;  // "+" and the gap before it.
  if (has_pinned) {
    fixed += static_cast<int>(first_flexible_) * (kPinnedWidth + kSegmentGap) -
             kSegmentGap + (has_flexible ? kGroupGap : 0);
  }
  for (size_t i = first_flexible_; i < items_.size(); ++i) {
    if (views::IsViewClass<SplitSeam>(items_[i].get())) {
      fixed += kSeamWidth;
    } else {
      ++tabs;
    }
    if (i + 1 < items_.size()) {
      fixed += kSegmentGap;
    }
  }
  int unit = kMaxTabWidth;
  if (tabs > 0) {
    unit = std::clamp((avail - fixed) / tabs, kMinTabWidth, kMaxTabWidth);
  }

  int x = kEdge - scroll_offset_;
  for (size_t i = 0; i < items_.size(); ++i) {
    views::View* item = items_[i];
    if (i == first_flexible_ && has_pinned) {
      x += kGroupGap - kSegmentGap;
    }
    const int w = i < first_flexible_ ? kPinnedWidth : ItemWidth(item, unit);
    if (item == drag_view_) {
      // The dragged tab follows the pointer; its slot stays reserved so the
      // others do not close up under it.
      item->SetBounds(std::clamp(drag_x_, kEdge, std::max(kEdge, width() - w)),
                      y, w, kTabHeight);
    } else {
      item->SetBounds(x, y, w, kTabHeight);
    }
    x += w + kSegmentGap;
  }
  if (new_tab_button_) {
    new_tab_button_->SetBounds(
        items_.empty() ? x : x - kSegmentGap + kGroupGap, y, kTabHeight,
        kTabHeight);
  }
}

void ZephyrusTabStrip::ScrollActiveIntoView() {
  content::WebContents* active = tab_strip_model_->GetActiveWebContents();
  if (!active || width() <= 0) {
    return;
  }
  DeprecatedLayoutImmediately();
  for (views::View* item : items_) {
    auto* tab = views::AsViewClass<StripTab>(item);
    if (!tab || tab->contents() != active) {
      continue;
    }
    const int left = item->x();
    const int right = item->bounds().right();
    const int limit = width() - kEdge - kTabHeight - kGroupGap;
    if (left < kEdge) {
      scroll_offset_ = std::max(0, scroll_offset_ - (kEdge - left));
    } else if (right > limit) {
      scroll_offset_ += right - limit;
    }
    InvalidateLayout();
    return;
  }
}

bool ZephyrusTabStrip::OnMouseWheel(const ui::MouseWheelEvent& event) {
  // Either wheel axis scrolls the row, so a plain mouse can reach tabs that
  // have run off the end.
  const int delta = event.x_offset() != 0 ? event.x_offset() : event.y_offset();
  int content = kTabHeight + kGroupGap;
  for (size_t i = 0; i < items_.size(); ++i) {
    content += items_[i]->width() + kSegmentGap;
  }
  const int max_offset = std::max(0, content - ContentWidth());
  const int next = std::clamp(scroll_offset_ - delta, 0, max_offset);
  if (next == scroll_offset_) {
    return false;
  }
  scroll_offset_ = next;
  InvalidateLayout();
  return true;
}

void ZephyrusTabStrip::OnThemeChanged() {
  views::View::OnThemeChanged();
  SchedulePaint();
}

void ZephyrusTabStrip::VisibilityChanged(views::View* starting_from,
                                         bool is_visible) {
  if (is_visible) {
    ScheduleRebuild();
  }
}

void ZephyrusTabStrip::ActivateTab(
    base::WeakPtr<content::WebContents> contents) {
  const int index = contents
                        ? tab_strip_model_->GetIndexOfWebContents(contents.get())
                        : TabStripModel::kNoTab;
  if (index != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(index);
  }
}

void ZephyrusTabStrip::CloseTab(base::WeakPtr<content::WebContents> contents) {
  const int index = contents
                        ? tab_strip_model_->GetIndexOfWebContents(contents.get())
                        : TabStripModel::kNoTab;
  if (index != TabStripModel::kNoTab) {
    tab_strip_model_->CloseWebContentsAt(index, CLOSE_USER_GESTURE);
  }
}

void ZephyrusTabStrip::BreakSplit(split_tabs::SplitTabId split_id) {
  if (tab_strip_model_->GetSplitData(split_id)) {
    tab_strip_model_->RemoveSplit(split_id);
  }
}

void ZephyrusTabStrip::NewTab() {
  chrome::NewTab(browser_view_->browser(), NewTabTypes::kNewTabButton);
}

void ZephyrusTabStrip::OnTabDragged(views::View* tab, int x_in_strip) {
  dragging_ = true;
  drag_view_ = tab;
  drag_x_ = x_in_strip;
  InvalidateLayout();
}

void ZephyrusTabStrip::OnTabDragEnded(
    views::View* tab,
    base::WeakPtr<content::WebContents> contents) {
  // The slot the pointer ended over: the flexible item whose centre is
  // nearest the dragged tab's centre. Model indices, not view indices, since
  // the strip shows only this workspace's slice of the model.
  const int drop_center = drag_x_ + tab->width() / 2;
  int target_index = TabStripModel::kNoTab;
  int best = INT_MAX;
  for (size_t i = first_flexible_; i < items_.size(); ++i) {
    auto* other = views::AsViewClass<StripTab>(items_[i].get());
    if (!other || !other->contents() || other->in_split()) {
      continue;  // Split pairs are not drop targets; they move as a unit.
    }
    const int distance = std::abs(other->bounds().CenterPoint().x() - drop_center);
    if (distance < best) {
      best = distance;
      target_index = tab_strip_model_->GetIndexOfWebContents(other->contents());
    }
  }

  dragging_ = false;
  drag_view_ = nullptr;
  const int from = contents
                       ? tab_strip_model_->GetIndexOfWebContents(contents.get())
                       : TabStripModel::kNoTab;
  if (from != TabStripModel::kNoTab && target_index != TabStripModel::kNoTab &&
      from != target_index) {
    tab_strip_model_->MoveWebContentsAt(from, target_index,
                                        /*select_after_move=*/true);
  }
  // Posted either way: `tab` is still on the stack.
  rebuild_deferred_ = false;
  ScheduleRebuild();
  InvalidateLayout();
}

void ZephyrusTabStrip::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  ScheduleRebuild();
}

void ZephyrusTabStrip::OnTabChangedAt(tabs::TabInterface* tab,
                                      int index,
                                      TabChangeType change_type) {
  // Loading ticks arrive constantly; only a title or favicon change shows.
  if (change_type == TabChangeType::kLoadingOnly) {
    return;
  }
  ScheduleRebuild();
}

void ZephyrusTabStrip::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                               int index) {
  ScheduleRebuild();
}

void ZephyrusTabStrip::OnSplitTabChanged(const SplitTabChange& change) {
  ScheduleRebuild();
}

void ZephyrusTabStrip::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  auto* tab = views::AsViewClass<StripTab>(source);
  if (!tab || !tab->contents()) {
    return;
  }
  menu_contents_ = tab->contents()->GetWeakPtr();
  const int index = tab_strip_model_->GetIndexOfWebContents(tab->contents());
  if (index == TabStripModel::kNoTab) {
    return;
  }

  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  // Per workspace, like the sidebar's -- no "(all workspaces)".
  menu_model_->AddItem(kCommandPin, tab_strip_model_->IsTabPinned(index)
                                        ? u"Unpin tab"
                                        : u"Pin tab");

  menu_workspace_ids_.clear();
  ZephyrusWorkspaceManager* manager =
      browser_view_->zephyrus_workspace_manager();
  if (manager && manager->workspaces().size() > 1) {
    move_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    const int current = manager->GetWorkspaceForContents(tab->contents());
    for (const ZephyrusWorkspaceManager::Workspace& ws :
         manager->workspaces()) {
      const size_t slot = menu_workspace_ids_.size();
      menu_workspace_ids_.push_back(ws.id);
      if (ws.id == current) {
        continue;
      }
      std::u16string label =
          ws.name.empty() ? u"Workspace " + base::NumberToString16(slot + 1)
                          : ws.name;
      if (!ws.emoji.empty() && !zephyrus::FindWorkspaceIcon(ws.emoji)) {
        label = ws.emoji + u"  " + label;
      }
      move_menu_model_->AddItem(kCommandMoveBase + static_cast<int>(slot),
                                label);
    }
    if (move_menu_model_->GetItemCount() > 0) {
      menu_model_->AddSubMenu(0, u"Move to workspace", move_menu_model_.get());
    }
  }
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddItem(kCommandClose, u"Close tab");
  menu_model_->AddItem(kCommandCloseOthers, u"Close other tabs");

  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::HAS_MNEMONICS |
                             views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

bool ZephyrusTabStrip::IsCommandIdChecked(int command_id) const {
  return false;
}

bool ZephyrusTabStrip::IsCommandIdEnabled(int command_id) const {
  return true;
}

void ZephyrusTabStrip::ExecuteCommand(int command_id, int event_flags) {
  // Re-resolved by identity: the tab may have moved or gone while the menu
  // was open.
  content::WebContents* contents = menu_contents_.get();
  const int index = contents
                        ? tab_strip_model_->GetIndexOfWebContents(contents)
                        : TabStripModel::kNoTab;
  if (index == TabStripModel::kNoTab) {
    return;
  }
  switch (command_id) {
    case kCommandPin:
      tab_strip_model_->SetTabPinned(index,
                                     !tab_strip_model_->IsTabPinned(index));
      return;
    case kCommandClose:
      tab_strip_model_->CloseWebContentsAt(index, CLOSE_USER_GESTURE);
      return;
    case kCommandCloseOthers: {
      // Only this workspace's other tabs: the others are not on screen, and
      // closing tabs the user cannot see is not what the item says.
      ZephyrusWorkspaceManager* manager =
          browser_view_->zephyrus_workspace_manager();
      std::vector<base::WeakPtr<content::WebContents>> doomed;
      for (int i = 0; i < tab_strip_model_->count(); ++i) {
        content::WebContents* other = tab_strip_model_->GetWebContentsAt(i);
        if (!other || other == contents || tab_strip_model_->IsTabPinned(i)) {
          continue;
        }
        if (manager && !manager->IsContentsInCurrentWorkspace(other)) {
          continue;
        }
        doomed.push_back(other->GetWeakPtr());
      }
      for (const auto& weak : doomed) {
        CloseTab(weak);
      }
      return;
    }
    default:
      break;
  }
  if (command_id >= kCommandMoveBase) {
    const size_t slot = static_cast<size_t>(command_id - kCommandMoveBase);
    ZephyrusWorkspaceManager* manager =
        browser_view_->zephyrus_workspace_manager();
    if (manager && slot < menu_workspace_ids_.size()) {
      manager->MoveContentsToWorkspace(contents, menu_workspace_ids_[slot]);
    }
  }
}

BEGIN_METADATA(ZephyrusTabStrip)
END_METADATA
