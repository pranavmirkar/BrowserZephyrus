// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/zephyrus_tab_strip.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "base/timer/timer.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/views/frame/zephyrus_audio_button.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_sidebar_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_icons.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/prefs/pref_service.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
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
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/vector_icons.h"

namespace {

// A Safari-style tab bar in M3 colours. The tabs are EQUAL WIDTH and share the
// bar's whole width; an idle tab has no fill, only a hairline divider between it
// and the next; the active tab is RAISED -- a lighter rounded plate with a soft
// shadow -- and hovering one lays a state layer over it. The dividers hide
// beside the active tab and the hovered one, as Safari's do.
//
// Compact on purpose: 28dp tabs in a 32dp band. The strip is chrome the page
// gives up height for, on every window, all the time.
// The same height as the omnibox (toolbar_view.cc's kZephyrusContainer): the
// tabs and the field read as one family of controls.
constexpr int kTabHeight = 26;
constexpr int kOneStripPlateY = (ZephyrusTabStrip::kBandHeight - kTabHeight) / 2;
// One strip: the plate sits this far down its 32dp band so that it is CENTRED
// in it, the band itself centred in the title bar's row. Under the title bar
// (the horizontal layout) it is NOT: there the bar's own bottom padding is the
// gap above the tabs, and the 6dp the band has left below them matches it.
// The tab VIEW is the whole band (ZephyrusTabStrip::kBandHeight), so the raised
// tab's shadow has room below the 28dp plate.
constexpr int kEdge = 4;
// Tabs adjoin: the dividers, not gaps, mark where one ends.
constexpr int kSegmentGap = 0;
// Between the pinned tabs and the rest.
constexpr int kGroupGap = 8;
constexpr int kMinTabWidth = 64;
constexpr int kMaxTabWidth = 360;
constexpr int kPinnedWidth = 36;
// The split-view seam: a narrow segment between a split pair's two tabs.
constexpr int kSeamWidth = 24;
constexpr int kFaviconSize = 16;
constexpr int kCloseSize = 18;
// Room kept at each end of a tab for the close button, so the centred title does
// not shift when the button appears.
constexpr int kCloseZone = 28;
// Below this an idle tab shows no close button: a narrow tab cannot hold a
// favicon, a readable title AND a target.
constexpr int kCloseMinWidth = 96;
// A tab's visible plate sits this far in from its sides, so the plates of two
// neighbours (and the divider between them) never touch.
constexpr int kPlateInset = 2;
constexpr int kDividerHeight = 14;

constexpr float kTabRadius = 8.f;
// Where a split pair's tabs meet its seam: nearly square, so the pair reads as
// one block.
constexpr float kSeamRadius = 2.f;

constexpr base::TimeDelta kMorphDuration = base::Milliseconds(200);

// Context-menu commands.
enum Command {
  kCommandPin = 1,
  kCommandClose,
  kCommandCloseOthers,
  kCommandNewSet,
  kCommandRemoveFromSet,
  kCommandMute,
  // A set's own menu (on its pill).
  kCommandSetRename = 30,
  kCommandSetIcon,
  kCommandSetPin,
  kCommandSetNewTab,
  kCommandSetClose,
  kCommandSetUngroup,
  kCommandMoveBase = 100,
  // "Add to set": this plus the set's place in the submenu.
  kCommandAddToSetBase = 300,
};

constexpr size_t kMaxSetNameLength = 32;

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

// One piece of the bar. Its corner radii are its own: a split pair's tabs and
// seam have tight inner corners, everything else is a plain rounded plate.
class Segment : public views::Button {
  METADATA_HEADER(Segment, views::Button)

 public:
  using views::Button::Button;

  void SetRadii(float lead, float trail) {
    lead_ = lead;
    trail_ = trail;
    SchedulePaint();
  }

 protected:
  float LeadRadius() const { return lead_; }
  float TrailRadius() const { return trail_; }
  // How far the plate is in from the view's side: none where it joins a seam.
  int LeadInset() const { return lead_ <= kSeamRadius ? 0 : kPlateInset; }
  int TrailInset() const { return trail_ <= kSeamRadius ? 0 : kPlateInset; }
  gfx::Rect PlateBounds() const {
    return gfx::Rect(LeadInset(), 0, std::max(0, width() - LeadInset() - TrailInset()),
                     kTabHeight);
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    InvalidateLayout();
    SchedulePaint();
    // The dividers beside a hovered tab hide, and they are the strip's to draw.
    if (parent()) {
      parent()->SchedulePaint();
    }
  }

  float lead_ = kTabRadius;
  float trail_ = kTabRadius;
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
        identity_(reinterpret_cast<uintptr_t>(contents)),
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

      // The audio control sits at the right end, where the close zone's mirror
      // image is already kept clear.
      audio_ = AddChildView(std::make_unique<ZephyrusAudioButton>(
          base::BindRepeating(&StripTab::ToggleMute, base::Unretained(this)),
          kCloseSize));
      audio_->SetAudio(contents->IsCurrentlyAudible(),
                       contents->IsAudioMuted());
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
  uintptr_t identity() const { return identity_; }
  bool is_pinned() const { return pinned_; }
  bool in_split() const { return tone_ == Tone::kSplit; }
  bool is_active() const { return active_; }
  // Held by a drag: painted raised, as the active tab is.
  void SetDragged(bool dragged) {
    if (dragged_ != dragged) {
      dragged_ = dragged;
      SchedulePaint();
    }
  }

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
    if (audio_) {
      audio_->SetInk(ink);
    }
  }

  void Layout(PassKey key) override {
    if (!label_) {
      return;
    }
    const bool narrow = width() < kCloseMinWidth;
    const bool show_close =
        close_ && (active_ || IsMouseHovered() || HasFocus()) &&
        width() >= (active_ ? kMinTabWidth : kCloseMinWidth);
    close_->SetVisible(show_close);
    // Safari puts the close button at the LEFT edge.
    if (show_close) {
      close_->SetBounds(kPlateInset + 4, (kTabHeight - kCloseSize) / 2,
                        kCloseSize, kCloseSize);
    }
    // And the audio control at the RIGHT edge, whenever the tab is playing or
    // muted (it is not hover-gated: it is also the "this tab is making noise"
    // sign).
    const bool show_audio =
        audio_ && audio_->wanted() && width() >= kMinTabWidth;
    audio_->SetVisible(show_audio);
    const int audio_zone = kPlateInset + 4 + kCloseSize + 4;
    if (show_audio) {
      audio_->SetBounds(width() - kPlateInset - 4 - kCloseSize,
                        (kTabHeight - kCloseSize) / 2, kCloseSize, kCloseSize);
    }
    // Favicon and title are centred as one unit, in the room the two close
    // zones leave. A narrow tab left-aligns them instead.
    const int zone = narrow ? 8 : kCloseZone;
    const int right_zone = show_audio ? std::max(zone, audio_zone) : zone;
    const int room = std::max(0, width() - zone - right_zone);
    const int wanted =
        kFaviconSize + 6 + label_->GetPreferredSize().width();
    const int unit = std::min(wanted, room);
    const int start = narrow ? zone : zone + (room - unit) / 2;
    content_x_ = start;
    label_->SetBounds(start + kFaviconSize + 6, 0,
                      std::max(0, unit - kFaviconSize - 6), kTabHeight);
    // A tooltip only when the title does not fit: one repeating a fully
    // visible title just covers the row below.
    SetTooltipText(label_->IsDisplayTextTruncated()
                       ? std::u16string(label_->GetText())
                       : std::u16string());
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const double t = std::max(morph_.GetCurrentValue(), dragged_ ? 1.0 : 0.0);
    const SkRRect plate = SegmentShape(PlateBounds(), LeadRadius(), TrailRadius());
    const bool hovered = GetState() == STATE_HOVERED;
    const bool pressed = GetState() == STATE_PRESSED;

    if (tone_ == Tone::kSplit) {
      // A split pair is one tinted block; its active half takes the secondary
      // tone, as the active tab always has.
      SkColor fill = color_utils::AlphaBlend(
          zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer),
          zephyrus::m3::Role(*this, kColorZephyrusTertiaryContainer),
          static_cast<float>(t));
      if (hovered || pressed) {
        fill = zephyrus::m3::WithStateLayer(
            fill, Ink(), pressed ? zephyrus::m3::kPressed : zephyrus::m3::kHover);
      }
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(fill);
      canvas->sk_canvas()->drawRRect(plate, flags);
      return;
    }

    if (t > 0.0) {
      // RAISED: a lighter plate and a soft shadow under it. The plate fades in
      // and out with the morph, so a switch of tab is a handover, not a jump.
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(color_utils::AlphaBlend(RaisedColor(), SK_ColorTRANSPARENT,
                                             static_cast<float>(t)));
      flags.setLooper(gfx::CreateShadowDrawLooper({gfx::ShadowValue(
          gfx::Vector2d(0, 1), 3,
          SkColorSetA(SK_ColorBLACK, static_cast<uint8_t>(0x66 * t)))}));
      canvas->sk_canvas()->drawRRect(plate, flags);
    }
    if ((hovered || pressed) && t < 1.0) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(zephyrus::m3::StateLayer(
          zephyrus::m3::Role(*this, kColorZephyrusOnSurface),
          pressed ? zephyrus::m3::kPressed : zephyrus::m3::kHover));
      canvas->sk_canvas()->drawRRect(plate, flags);
    }
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const int x = pinned_ ? (width() - kFaviconSize) / 2 : content_x_;
    const int y = (kTabHeight - kFaviconSize) / 2;
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
  // The plate the active tab is raised on: lighter than the bar. A dark bar is
  // lifted toward white, a light one is mostly white, so the active tab is
  // always the brightest thing in the strip.
  SkColor RaisedColor() const {
    const SkColor ground = strip_->GroundColor();
    return color_utils::IsDark(ground)
               ? color_utils::AlphaBlend(
                     zephyrus::m3::Role(*this, kColorZephyrusOnSurface), ground,
                     0.14f)
               : color_utils::AlphaBlend(SK_ColorWHITE, ground, 0.75f);
  }

  SkColor Ink() const {
    if (tone_ == Tone::kSplit) {
      return zephyrus::m3::Role(*this, kColorZephyrusOnTertiaryContainer);
    }
    return zephyrus::m3::Role(*this, active_ ? kColorZephyrusOnSurface
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
  void ToggleMute(const ui::Event& event) { strip_->ToggleTabMuted(contents_); }

  raw_ptr<ZephyrusTabStrip> strip_;
  base::WeakPtr<content::WebContents> contents_;
  // The tab's address, kept from the start: when the tab closes its contents is
  // gone before the strip is rebuilt, and the closing tab still has to be
  // recognised to shrink away. Never dereferenced.
  const uintptr_t identity_;
  const bool active_;
  const bool pinned_;
  const Tone tone_;
  gfx::ImageSkia favicon_;
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<StripCloseButton> close_ = nullptr;
  raw_ptr<ZephyrusAudioButton> audio_ = nullptr;
  gfx::SlideAnimation morph_;
  int press_x_ = 0;
  bool dragging_ = false;
  bool dragged_ = false;
  // Where the favicon sits, set by Layout.
  int content_x_ = 10;
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
    SetRadii(kSeamRadius, kSeamRadius);
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
        SegmentShape(PlateBounds(), LeadRadius(), TrailRadius()), flags);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::ImageSkia glyph =
        ui::ImageModel::FromVectorIcon(
            kSplitSceneIcon,
            zephyrus::m3::Role(*this, kColorZephyrusOnTertiaryContainer), 14)
            .Rasterize(GetColorProvider());
    canvas->DrawImageInt(glyph, (width() - glyph.width()) / 2,
                         (kTabHeight - glyph.height()) / 2);
  }
};

BEGIN_METADATA(SplitSeam)
END_METADATA

// The trailing "+": a standalone icon button, clear of the group.
//   Hover: the plus turns a quarter turn on M3's fast-spatial spring.
//   Press: it shrinks while held.
//   Click: it turns another quarter and pops, as the new tab grows in beside it.
class NewTabChip : public views::Button {
  METADATA_HEADER(NewTabChip, views::Button)

 public:
  explicit NewTabChip(PressedCallback callback)
      : views::Button(base::BindRepeating(&NewTabChip::OnClicked,
                                          base::Unretained(this))),
        action_(std::move(callback)),
        hover_(&forwarder_),
        press_(&forwarder_),
        click_(&forwarder_) {
    hover_.SetTweenType(gfx::Tween::LINEAR);
    hover_.SetSlideDuration(base::Milliseconds(260));
    press_.SetTweenType(gfx::Tween::LINEAR);
    press_.SetSlideDuration(base::Milliseconds(100));
    click_.SetTweenType(gfx::Tween::LINEAR);
    click_.SetSlideDuration(base::Milliseconds(260));
    SetTooltipText(u"New tab");
    GetViewAccessibility().SetName(u"New tab");
    views::InstallCircleHighlightPathGenerator(this);
  }
  NewTabChip(const NewTabChip&) = delete;
  NewTabChip& operator=(const NewTabChip&) = delete;
  ~NewTabChip() override = default;

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    SchedulePaint();
  }

  void StateChanged(views::Button::ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    Retarget(hover_, GetState() == STATE_HOVERED || GetState() == STATE_PRESSED);
    Retarget(press_, GetState() == STATE_PRESSED);
    SchedulePaint();
  }

  // One strip, scrolled: the chip stays at the strip's end with tabs passing
  // beneath it, so it needs something opaque to stand on.
  void SetBackdrop(std::optional<SkColor> backdrop) {
    if (backdrop_ != backdrop) {
      backdrop_ = backdrop;
      SchedulePaint();
    }
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (backdrop_) {
      canvas->DrawColor(*backdrop_);
    }
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

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const float hover = zephyrus::m3::Curve(zephyrus::m3::Spring::kFastSpatial)
                            .Solve(hover_.GetCurrentValue());
    const float press = zephyrus::m3::Curve(zephyrus::m3::Spring::kFastEffects)
                            .Solve(press_.GetCurrentValue());
    const double click_t = click_.GetCurrentValue();
    const float click = zephyrus::m3::Curve(zephyrus::m3::Spring::kFastEffects)
                            .Solve(click_t);
    const float pop = 0.25f * static_cast<float>(std::sin(M_PI * click_t));
    canvas->Save();
    cc::PaintCanvas* const paint = canvas->sk_canvas();
    paint->translate(width() / 2.0f, height() / 2.0f);
    paint->rotate(90.0f * hover + 90.0f * click);
    const float scale = (1.0f - 0.18f * press) * (1.0f + pop);
    paint->scale(scale, scale);
    paint->translate(-kIconSize / 2.0f, -kIconSize / 2.0f);
    gfx::PaintVectorIcon(
        canvas, kAddIcon, kIconSize,
        zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant));
    canvas->Restore();
  }

 private:
  std::optional<SkColor> backdrop_;
  static constexpr int kIconSize = 16;

  // views::Button is itself a gfx::AnimationDelegate, so ours cannot be the
  // button: it is this.
  class Forwarder final : public gfx::AnimationDelegate {
   public:
    explicit Forwarder(NewTabChip* owner) : owner_(owner) {}
    void AnimationProgressed(const gfx::Animation*) override {
      owner_->SchedulePaint();
    }

   private:
    raw_ptr<NewTabChip> owner_;
  };

  void Retarget(gfx::SlideAnimation& animation, bool show) {
    if (!gfx::Animation::ShouldRenderRichAnimation()) {
      animation.Reset(show ? 1.0 : 0.0);
      SchedulePaint();
      return;
    }
    if (show) {
      animation.Show();
    } else {
      animation.Hide();
    }
  }

  void OnClicked(const ui::Event& event) {
    if (gfx::Animation::ShouldRenderRichAnimation()) {
      click_.Reset(0.0);
      click_.Show();
    }
    action_.Run(event);
  }

  PressedCallback action_;
  Forwarder forwarder_{this};
  gfx::SlideAnimation hover_;
  gfx::SlideAnimation press_;
  gfx::SlideAnimation click_;
};

BEGIN_METADATA(NewTabChip)
END_METADATA

// A text field that says when it loses focus, so a name being typed is kept
// rather than abandoned when the user clicks away.
class PillRenameField : public views::Textfield {
  METADATA_HEADER(PillRenameField, views::Textfield)

 public:
  explicit PillRenameField(base::RepeatingClosure on_blur)
      : on_blur_(std::move(on_blur)) {}
  PillRenameField(const PillRenameField&) = delete;
  PillRenameField& operator=(const PillRenameField&) = delete;
  ~PillRenameField() override = default;

  void OnBlur() override {
    views::Textfield::OnBlur();
    if (on_blur_) {
      on_blur_.Run();
    }
  }

 private:
  base::RepeatingClosure on_blur_;
};

BEGIN_METADATA(PillRenameField)
END_METADATA

// The head of a set: a small capsule with the set's icon and name, and a
// chevron that opens and closes it. Double-click the name to rename it and the
// icon to change it; right-click for the rest.
class SetPill : public views::Button, public views::TextfieldController {
  METADATA_HEADER(SetPill, views::Button)

 public:
  static constexpr int kHeight = 24;

  SetPill(ZephyrusTabStrip* strip,
          const tab_groups::TabGroupId& id,
          const std::u16string& title,
          const std::string& icon_key,
          bool collapsed,
          int count,
          bool has_active)
      : views::Button(views::Button::PressedCallback()),
        strip_(strip),
        id_(id),
        title_(title),
        collapsed_(collapsed),
        has_active_(has_active) {
    icon_ = zephyrus::FindWorkspaceIcon(icon_key);
    if (!icon_) {
      icon_ = zephyrus::FindWorkspaceIcon(std::string_view("files"));
    }
    label_ = AddChildView(std::make_unique<views::Label>(
        collapsed ? title + u"  " + base::NumberToString16(count) : title));
    label_->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelMedium));
    label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label_->SetElideBehavior(gfx::ELIDE_TAIL);
    label_->SetAutoColorReadabilityEnabled(false);
    label_->SetSubpixelRenderingEnabled(false);
    label_->SetCanProcessEventsWithinSubtree(false);
    editor_ = AddChildView(std::make_unique<PillRenameField>(base::BindRepeating(
        &SetPill::EndRename, base::Unretained(this), true)));
    editor_->set_controller(this);
    editor_->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelMedium));
    editor_->SetDefaultWidthInChars(10);
    editor_->GetViewAccessibility().SetName(u"Set name");
    editor_->SetVisible(false);
    GetViewAccessibility().SetName(title);
    SetTooltipText(title);
    SetNotifyEnterExitOnChild(true);
  }
  SetPill(const SetPill&) = delete;
  SetPill& operator=(const SetPill&) = delete;
  ~SetPill() override = default;

  const tab_groups::TabGroupId& set_id() const { return id_; }

  // A tab being dragged would join this set: it is ringed.
  void SetDropHighlight(bool on) {
    if (drop_highlight_ != on) {
      drop_highlight_ = on;
      SchedulePaint();
    }
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int text = renaming_ ? editor_->GetPreferredSize().width()
                               : std::min(label_->GetPreferredSize().width(), 120);
    return gfx::Size(kLead + kGlyph + 6 + text + 6 + kChevronBox + kTrail,
                     kHeight);
  }

  void Layout(PassKey) override {
    const int x = kLead + kGlyph + 6;
    const int w = std::max(0, width() - x - 6 - kChevronBox - kTrail);
    label_->SetBounds(x, 0, w, height());
    editor_->SetBounds(x - 4, 1, w + 8, height() - 2);
  }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    label_->SetEnabledColor(Ink());
    SchedulePaint();
  }

  void StateChanged(views::Button::ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    // As the sidebar's header: a quiet container, lit when the active tab is
    // inside the closed set.
    SkColor fill = zephyrus::m3::Role(
        *this, Lit() ? kColorZephyrusSecondaryContainer
                     : kColorZephyrusSurfaceContainerHighest);
    // No hover layer on the pill itself: the control is the round container at
    // its end, as in the sidebar's header.
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(fill);
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kHeight / 2.f, flags);
    if (drop_highlight_) {
      // The ring that says "drop here".
      cc::PaintFlags ring;
      ring.setAntiAlias(true);
      ring.setStyle(cc::PaintFlags::kStroke_Style);
      ring.setStrokeWidth(2);
      ring.setColor(zephyrus::m3::Role(*this, kColorZephyrusPrimary));
      gfx::RectF bounds(GetLocalBounds());
      bounds.Inset(1.0f);
      canvas->DrawRoundRect(bounds, kHeight / 2.f - 1.0f, ring);
    }
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (icon_) {
      zephyrus::PaintWorkspaceGlyph(
          canvas, *icon_,
          gfx::RectF(kLead, (height() - kGlyph) / 2.f, kGlyph, kGlyph), Ink(),
          1.6f);
    }
    // The dropdown control: the chevron in a round container of its own at the
    // pill's end, concentric with it (the pill's end has radius 12, the
    // container sits 3dp inside it: radius 9). The ink as a wash, so it reads on
    // the pill whatever its tone.
    const gfx::Rect box = ChevronRect();
    uint8_t alpha = 0x1F;
    if (chevron_pressed_) {
      alpha = 0x3D;
    } else if (chevron_hovered_) {
      alpha = 0x33;
    }
    cc::PaintFlags wash;
    wash.setAntiAlias(true);
    wash.setColor(SkColorSetA(Ink(), alpha));
    canvas->DrawCircle(gfx::PointF(box.CenterPoint()), kChevronBox / 2.f, wash);
    // The chevron points down when the set is open, right when it is closed.
    canvas->Save();
    cc::PaintCanvas* const paint = canvas->sk_canvas();
    paint->translate(box.CenterPoint().x(), box.CenterPoint().y());
    paint->rotate(collapsed_ ? -90.f : 0.f);
    paint->translate(-kChevron / 2.f, -kChevron / 2.f);
    gfx::PaintVectorIcon(canvas, kZephyrusExpandMoreIcon, kChevron, Ink());
    canvas->Restore();
  }

  void OnMouseMoved(const ui::MouseEvent& event) override {
    views::Button::OnMouseMoved(event);
    SetChevronHovered(ChevronRect().Contains(event.location()));
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
    views::Button::OnMouseExited(event);
    SetChevronHovered(false);
  }

  void BeginRename() {
    if (renaming_) {
      return;
    }
    renaming_ = true;
    strip_->SetRenameActive(true);
    editor_->SetText(title_);
    label_->SetVisible(false);
    editor_->SetVisible(true);
    PreferredSizeChanged();
    editor_->RequestFocus();
    editor_->SelectAll(true);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    press_x_ = event.x();
    if (!renaming_ && event.IsOnlyLeftMouseButton()) {
      gfx::Rect chevron = ChevronRect();
      chevron.Inset(-2);
      if (chevron.Contains(event.location())) {
        chevron_pressed_ = true;
        SchedulePaint();
        strip_->ToggleSetCollapsed(id_);
        return true;
      }
      if (event.GetClickCount() == 2) {
        if (event.x() < kLead + kGlyph + 3) {
          zephyrus::ShowIconPicker(
              this, base::BindRepeating(&ZephyrusTabStrip::ChangeSetIcon,
                                        base::Unretained(strip_.get()), id_));
        } else {
          BeginRename();
        }
        return true;
      }
    }
    return views::Button::OnMousePressed(event);
  }

  // Dragging the pill moves the set along the strip.
  bool OnMouseDragged(const ui::MouseEvent& event) override {
    constexpr int kDragThreshold = 6;
    if (renaming_ ||
        (!dragging_ && std::abs(event.x() - press_x_) < kDragThreshold)) {
      return views::Button::OnMouseDragged(event);
    }
    if (!dragging_) {
      dragging_ = true;
      if (GetWidget()) {
        GetWidget()->SetCapture(this);
      }
    }
    gfx::Point point = event.location();
    views::View::ConvertPointToTarget(this, strip_, &point);
    strip_->OnPillDragged(this, point.x() - press_x_);
    return true;
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (chevron_pressed_) {
      chevron_pressed_ = false;
      SchedulePaint();
    }
    if (!dragging_) {
      views::Button::OnMouseReleased(event);
      return;
    }
    dragging_ = false;
    if (GetWidget()) {
      GetWidget()->ReleaseCapture();
    }
    strip_->OnPillDragEnded(this, /*cancelled=*/false);
  }
  void OnMouseCaptureLost() override {
    if (!dragging_) {
      views::Button::OnMouseCaptureLost();
      return;
    }
    dragging_ = false;
    strip_->OnPillDragEnded(this, /*cancelled=*/true);
  }

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& event) override {
    if (event.type() != ui::EventType::kKeyPressed) {
      return false;
    }
    if (event.key_code() == ui::VKEY_RETURN) {
      EndRename(true);
      return true;
    }
    if (event.key_code() == ui::VKEY_ESCAPE) {
      EndRename(false);
      return true;
    }
    return false;
  }

 private:
  static constexpr int kLead = 8;
  // The container sits 3dp in from the pill's end.
  static constexpr int kTrail = 3;
  static constexpr int kGlyph = 14;
  static constexpr int kChevron = 12;
  static constexpr int kChevronBox = 18;

  gfx::Rect ChevronRect() const {
    return gfx::Rect(width() - kTrail - kChevronBox,
                     (height() - kChevronBox) / 2, kChevronBox, kChevronBox);
  }
  void SetChevronHovered(bool hovered) {
    if (chevron_hovered_ != hovered) {
      chevron_hovered_ = hovered;
      SchedulePaint();
    }
  }

  bool Lit() const { return has_active_ && collapsed_; }

  SkColor Ink() const {
    return zephyrus::m3::Role(*this, Lit() ? kColorZephyrusOnSecondaryContainer
                                           : kColorZephyrusOnSurface);
  }

  void EndRename(bool commit) {
    if (!renaming_) {
      return;
    }
    renaming_ = false;
    std::u16string text;
    base::TrimWhitespace(editor_->GetText(), base::TRIM_ALL, &text);
    if (text.size() > kMaxSetNameLength) {
      text.resize(kMaxSetNameLength);
    }
    editor_->SetVisible(false);
    label_->SetVisible(true);
    if (GetFocusManager()) {
      GetFocusManager()->ClearFocus();
    }
    PreferredSizeChanged();
    strip_->SetRenameActive(false);
    if (commit && !text.empty() && text != title_) {
      strip_->RenameSet(id_, text);
    }
  }

  raw_ptr<ZephyrusTabStrip> strip_;
  const tab_groups::TabGroupId id_;
  const std::u16string title_;
  const bool collapsed_;
  const bool has_active_;
  raw_ptr<const zephyrus::WorkspaceIcon> icon_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<PillRenameField> editor_ = nullptr;
  bool renaming_ = false;
  bool dragging_ = false;
  bool drop_highlight_ = false;
  bool chevron_hovered_ = false;
  bool chevron_pressed_ = false;
  int press_x_ = 0;
};

BEGIN_METADATA(SetPill)
END_METADATA

// What an item IS, across rebuilds: the same tab (or set) gets the same key
// however the strip is re-laid-out, so a rebuild can tell a tab that moved from
// one that is new or gone. Empty for pieces that just snap (a split seam).
std::string KeyOf(views::View* view) {
  if (auto* tab = views::AsViewClass<StripTab>(view)) {
    return "t" + base::NumberToString(tab->identity());
  }
  if (auto* pill = views::AsViewClass<SetPill>(view)) {
    return "s" + pill->set_id().ToString();
  }
  if (views::IsViewClass<NewTabChip>(view)) {
    return "+";
  }
  return std::string();
}

}  // namespace

ZephyrusTabStrip::ZephyrusTabStrip(BrowserView* browser_view)
    : browser_view_(browser_view),
      tab_strip_model_(browser_view->browser()->tab_strip_model()) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kTabList);
  GetViewAccessibility().SetName(u"Tabs");
  layout_anim_.SetSlideDuration(base::Milliseconds(240));
  layout_anim_.SetTweenType(gfx::Tween::EASE_OUT);
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
  // Held for the whole of a drag (rebuilding deletes the view being dragged) and
  // while a name is typed (it would delete the field).
  if (dragging_ || rename_active_) {
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
  if (dragging_ || rename_active_) {
    rebuild_deferred_ = true;
    return;
  }
  // The old views are kept until the new ones are built: a tab that is gone
  // shrinks away from where it was instead of vanishing.
  ClearGhosts();
  ignore_anim_end_ = true;
  layout_anim_.Reset(1.0);
  ignore_anim_end_ = false;
  struct Old {
    std::string key;
    gfx::Rect bounds;
    std::unique_ptr<views::View> view;
  };
  std::vector<Old> old;
  for (views::View* child :
       std::vector<views::View*>(children().begin(), children().end())) {
    Old entry;
    entry.key = KeyOf(child);
    entry.bounds = child->bounds();
    entry.view = RemoveChildViewT(child);
    old.push_back(std::move(entry));
  }
  items_.clear();
  order_.clear();
  gap_before_.clear();
  set_spans_.clear();
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
  auto emit_tabs = [&](const std::vector<int>& indices) {
  for (int index : indices) {
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
      StripTab* first = AddChildView(make_tab(members[0], Tone::kSplit));
      first->SetRadii(kTabRadius, kSeamRadius);
      items_.push_back(first);
      items_.push_back(
          AddChildView(std::make_unique<SplitSeam>(this, *split)));
      StripTab* second = AddChildView(make_tab(members[1], Tone::kSplit));
      second->SetRadii(kSeamRadius, kTabRadius);
      items_.push_back(second);
      consumed.insert(members.begin(), members.end());
      continue;
    }
    items_.push_back(AddChildView(make_tab(index, Tone::kNormal)));
  }
  };

  // The unpinned tabs by set: each set heads its own tabs, pinned sets first,
  // and the tabs in no set follow. The same order the sidebar shows.
  PrefService* prefs = browser_view_->browser()->GetProfile()->GetPrefs();
  std::vector<std::pair<tab_groups::TabGroupId, std::vector<int>>> grouped;
  std::vector<int> loose;
  for (int index : unpinned) {
    const std::optional<tab_groups::TabGroupId> group =
        tab_strip_model_->GetTabGroupForTab(index);
    // One strip: sets are off. The tabs stay in them in the model; they are
    // simply shown loose, collapsed or not, until another layout brings the
    // sets back.
    if (!group || one_strip_) {
      loose.push_back(index);
      continue;
    }
    auto it = std::find_if(grouped.begin(), grouped.end(),
                           [&](const auto& entry) { return entry.first == *group; });
    if (it == grouped.end()) {
      grouped.emplace_back(*group, std::vector<int>());
      it = grouped.end() - 1;
    }
    it->second.push_back(index);
  }
  std::vector<tab_groups::TabGroupId> set_ids;
  for (const auto& entry : grouped) {
    set_ids.push_back(entry.first);
  }
  std::vector<size_t> gaps;
  if (first_flexible_ > 0) {
    gaps.push_back(first_flexible_);
  }
  const int active_index = tab_strip_model_->active_index();
  for (const tab_groups::TabGroupId& id : OrderZephyrusSets(prefs, set_ids)) {
    const auto entry = std::find_if(
        grouped.begin(), grouped.end(),
        [&](const auto& candidate) { return candidate.first == id; });
    const std::vector<int>& members = entry->second;
    const ZephyrusSetInfo info = GetZephyrusSetInfo(prefs, id);
    const bool has_active =
        std::find(members.begin(), members.end(), active_index) !=
        members.end();
    const tab_groups::TabGroupVisualData* data =
        tab_strip_model_->group_model()->GetTabGroup(id)->visual_data();
    const size_t begin = items_.size();
    if (begin > 0) {
      gaps.push_back(begin);
    }
    auto* pill = AddChildView(std::make_unique<SetPill>(
        this, id, data && !data->title().empty() ? data->title() : u"Set",
        info.icon, info.collapsed, static_cast<int>(members.size()),
        has_active));
    pill->set_context_menu_controller(this);
    items_.push_back(pill);
    // A closed set shows only its pill; the pill is lit when the active tab is
    // inside it.
    if (!info.collapsed) {
      emit_tabs(members);
    }
    set_spans_.emplace_back(begin, items_.size());
  }
  if (!loose.empty()) {
    if (!set_ids.empty()) {
      gaps.push_back(items_.size());
    }
    emit_tabs(loose);
  }
  gap_before_.assign(items_.size(), false);
  for (size_t gap : gaps) {
    if (gap < gap_before_.size()) {
      gap_before_[gap] = true;
    }
  }

  new_tab_button_ = AddChildView(std::make_unique<NewTabChip>(
      base::BindRepeating([](ZephyrusTabStrip* strip,
                             const ui::Event&) { strip->NewTab(); },
                          base::Unretained(this))));
  // What moved, what is new, what went. Only a change that adds or removes
  // something is animated -- a title changing rebuilds too, and must not make
  // the whole strip stir.
  std::map<std::string, gfx::Rect> before;
  for (const Old& entry : old) {
    if (!entry.key.empty()) {
      before[entry.key] = entry.bounds;
    }
  }
  std::set<std::string> now;
  size_t kept = 0;
  anim_info_.assign(items_.size(), AnimInfo());
  entering_count_ = 0;
  bool added = false;
  for (size_t i = 0; i < items_.size(); ++i) {
    const std::string key = KeyOf(items_[i]);
    if (key.empty()) {
      continue;
    }
    now.insert(key);
    const auto it = before.find(key);
    if (it != before.end()) {
      anim_info_[i].from = it->second;
      anim_info_[i].has_from = true;
      ++kept;
    } else {
      anim_info_[i].entering = true;
      anim_info_[i].rank = entering_count_++;
      added = true;
    }
  }
  chip_info_ = AnimInfo();
  if (const auto it = before.find("+"); it != before.end()) {
    chip_info_.from = it->second;
    chip_info_.has_from = true;
  }
  bool removed = false;
  const bool continuity = kept > 0;
  if (gfx::Animation::ShouldRenderRichAnimation() && continuity) {
    for (size_t k = 0; k < old.size(); ++k) {
      Old& entry = old[k];
      if (entry.key.empty() || now.count(entry.key) ||
          !(views::IsViewClass<StripTab>(entry.view.get()) ||
            views::IsViewClass<SetPill>(entry.view.get()))) {
        continue;
      }
      entry.view->SetCanProcessEventsWithinSubtree(false);
      Ghost ghost;
      ghost.from = entry.bounds;
      for (size_t j = k; j-- > 0;) {
        if (!old[j].key.empty() && now.count(old[j].key)) {
          ghost.after_key = old[j].key;
          break;
        }
      }
      ghost.view = AddChildView(std::move(entry.view));
      ghosts_.push_back(std::move(ghost));
      removed = true;
    }
  }
  const bool animate = gfx::Animation::ShouldRenderRichAnimation() &&
                       continuity && (added || removed || force_animation_);
  force_animation_ = false;
  if (animate) {
    for (views::View* child : children()) {
      child->SetPaintToLayer();
      child->layer()->SetFillsBoundsOpaquely(false);
    }
    ignore_anim_end_ = true;
    layout_anim_.Reset(0.0);
    ignore_anim_end_ = false;
    layout_anim_.Show();
  } else {
    anim_info_.assign(items_.size(), AnimInfo());
    chip_info_ = AnimInfo();
    entering_count_ = 0;
    ClearGhosts();
  }
  InvalidateLayout();
  ScrollActiveIntoView();
}

void ZephyrusTabStrip::ClearGhosts() {
  for (Ghost& ghost : ghosts_) {
    if (ghost.view) {
      // Deleted here: the unique_ptr it comes back in is dropped.
      RemoveChildViewT(ghost.view.get());
    }
  }
  ghosts_.clear();
}

void ZephyrusTabStrip::ClearLayoutAnimation() {
  ClearGhosts();
  for (views::View* child : children()) {
    if (child->layer()) {
      child->DestroyLayer();
    }
  }
  anim_info_.assign(items_.size(), AnimInfo());
  chip_info_ = AnimInfo();
  entering_count_ = 0;
}

void ZephyrusTabStrip::AnimationProgressed(const gfx::Animation* animation) {
  InvalidateLayout();
  SchedulePaint();
}

void ZephyrusTabStrip::AnimationEnded(const gfx::Animation* animation) {
  if (ignore_anim_end_) {
    return;
  }
  ClearLayoutAnimation();
  InvalidateLayout();
  SchedulePaint();
}

int ZephyrusTabStrip::ContentWidth() const {
  return std::max(0, width() - 2 * kEdge - (one_strip_ ? kDragZone : 0));
}

SkColor ZephyrusTabStrip::GroundColor() const {
  return browser_view_->GetZephyrusThemeColor();
}

void ZephyrusTabStrip::SetTopInset(int inset) {
  if (inset != top_inset_) {
    top_inset_ = inset;
    InvalidateLayout();
  }
}

void ZephyrusTabStrip::OnPaintBackground(gfx::Canvas* canvas) {
  // The band between the title bar and the page belongs to no other view: the
  // frame paints only its own top area, and the page starts below this strip.
  // Left unpainted it shows whatever the compositor started with (black, or a
  // debug blue), most visibly in the gaps between tabs and right of "+".
  //
  // In the title bar (one strip) the bar paints its own background.
  if (!one_strip_) {
    canvas->DrawColor(browser_view_->GetZephyrusThemeColor());
  }

  // The hairlines between idle tabs. Hidden beside the active tab, the hovered
  // one and the one being dragged, where a plate takes their place.
  const SkColor divider =
      SkColorSetA(zephyrus::m3::Role(*this, kColorZephyrusOutlineVariant), 0xB0);
  const int plate_y = one_strip_ ? kOneStripPlateY : 0;
  const int y = top_inset_ + plate_y + (kTabHeight - kDividerHeight) / 2;
  const bool ordered = order_.size() == items_.size();
  for (size_t i = 0; i + 1 < items_.size(); ++i) {
    const size_t a = ordered ? order_[i] : i;
    const size_t b = ordered ? order_[i + 1] : i + 1;
    auto* left = views::AsViewClass<StripTab>(items_[a].get());
    auto* right = views::AsViewClass<StripTab>(items_[b].get());
    if (!left || !right || left->is_pinned() != right->is_pinned() ||
        left->in_split() || right->in_split() || GapBetween(a, b)) {
      continue;
    }
    auto quiet = [&](StripTab* tab) {
      return tab->is_active() || tab->GetState() == views::Button::STATE_HOVERED ||
             tab->GetState() == views::Button::STATE_PRESSED ||
             tab == drag_view_;
    };
    if (quiet(left) || quiet(right)) {
      continue;
    }
    canvas->FillRect(gfx::Rect(right->x(), y, 1, kDividerHeight), divider);
  }

  // Under each set, a thin line from its pill to its last tab: what says these
  // tabs belong together.
  cc::PaintFlags line;
  line.setAntiAlias(true);
  line.setColor(SkColorSetA(zephyrus::m3::Role(*this, kColorZephyrusPrimary), 0xA0));
  for (const auto& [begin, end] : set_spans_) {
    if (begin >= end || end > items_.size()) {
      continue;
    }
    // The span's extent, whatever order its tabs are in at the moment.
    int left = INT_MAX;
    int right = INT_MIN;
    for (size_t k = begin; k < end; ++k) {
      left = std::min(left, items_[k]->x());
      right = std::max(right, items_[k]->bounds().right());
    }
    left += 2;
    right -= 2;
    if (right <= left) {
      continue;
    }
    canvas->DrawRoundRect(
        gfx::RectF(left, top_inset_ + plate_y + kTabHeight + 1.f, right - left,
                   2.f),
        1.f,
        line);
  }
}

void ZephyrusTabStrip::Layout(PassKey key) {
  const int plate_y = one_strip_ ? kOneStripPlateY : 0;
  const int y = top_inset_ + plate_y;
  const int avail = ContentWidth();
  const bool animating = layout_anim_.is_animating();
  const double t = animating ? layout_anim_.GetCurrentValue() : 1.0;
  // While tabs come and go the layout is run again for each frame with each
  // one's SHARE of the room (a new tab's grows from 0, a closing tab's shrinks
  // to 0), so the tabs flow instead of sliding over one another.
  const bool weighted =
      animating && (entering_count_ > 0 || !ghosts_.empty());
  const int stagger_n = std::max(1, entering_count_ - 1);
  const double step = std::min(0.06, 0.3 / stagger_n);
  // How far in a new item has grown, later for each one that came with it.
  auto entering_share = [&](size_t i) {
    if (!weighted || i >= anim_info_.size() || !anim_info_[i].entering) {
      return 1.0;
    }
    return std::clamp(
        (t - step * anim_info_[i].rank) / std::max(0.4, 1.0 - step * stagger_n),
        0.0, 1.0);
  };

  struct Rects {
    std::vector<gfx::Rect> items;
    std::vector<gfx::Rect> ghosts;
    gfx::Rect chip;
  };
  auto compute = [&](bool with_motion) {
    struct Entry {
      int item = -1;
      int ghost = -1;
      views::View* view = nullptr;
      double share = 1.0;
      int gap = 0;
      // A width that does not depend on the room, or -1 for a tab.
      int fixed = -1;
      bool is_pill = false;
    };
    auto fixed_width = [&](views::View* view, bool pinned) {
      if (pinned) {
        return kPinnedWidth;
      }
      if (views::IsViewClass<SplitSeam>(view)) {
        return kSeamWidth;
      }
      if (auto* pill = views::AsViewClass<SetPill>(view)) {
        return pill->GetPreferredSize().width();
      }
      return -1;
    };

    // The order on screen: the items, with each closing one where it was.
    std::vector<size_t> ghost_slot(ghosts_.size(), items_.size());
    if (with_motion) {
      for (size_t g = 0; g < ghosts_.size(); ++g) {
        ghost_slot[g] = first_flexible_;
        if (ghosts_[g].after_key.empty()) {
          continue;
        }
        for (size_t i = 0; i < items_.size(); ++i) {
          if (KeyOf(items_[i]) == ghosts_[g].after_key) {
            ghost_slot[g] = i + 1;
            break;
          }
        }
      }
    }
    std::vector<Entry> entries;
    auto add_ghosts = [&](size_t slot) {
      if (!with_motion) {
        return;
      }
      for (size_t g = 0; g < ghosts_.size(); ++g) {
        if (ghost_slot[g] != slot || !ghosts_[g].view) {
          continue;
        }
        Entry e;
        e.ghost = static_cast<int>(g);
        e.view = ghosts_[g].view;
        e.share = 1.0 - t;
        e.gap = kSegmentGap;
        auto* tab = views::AsViewClass<StripTab>(ghosts_[g].view.get());
        e.fixed = fixed_width(e.view, tab && tab->is_pinned());
        e.is_pill = views::IsViewClass<SetPill>(e.view);
        entries.push_back(e);
      }
    };
    const bool ordered = order_.size() == items_.size();
    for (size_t k = 0; k < items_.size(); ++k) {
      add_ghosts(k);
      const size_t i = ordered ? order_[k] : k;
      Entry e;
      e.item = static_cast<int>(i);
      e.view = items_[i];
      e.share = with_motion ? entering_share(i) : 1.0;
      e.gap = k == 0 ? 0
                     : (GapBetween(ordered ? order_[k - 1] : k - 1, i)
                            ? kGroupGap
                            : kSegmentGap);
      e.fixed = fixed_width(e.view, i < first_flexible_);
      e.is_pill = views::IsViewClass<SetPill>(e.view);
      entries.push_back(e);
    }
    add_ghosts(items_.size());

    // What the fixed pieces take, and the tabs' shares of the rest.
    double fixed_total = kTabHeight + kGroupGap;  // "+" and the gap before it.
    double weight = 0;
    for (const Entry& e : entries) {
      fixed_total += e.gap * e.share;
      if (e.fixed >= 0) {
        fixed_total += e.fixed * e.share;
      } else {
        weight += e.share;
      }
    }
    double unit = kMaxTabWidth;
    if (weight > 0) {
      unit = std::clamp((avail - fixed_total) / weight,
                        static_cast<double>(kMinTabWidth),
                        static_cast<double>(kMaxTabWidth));
    }

    Rects out;
    out.items.assign(items_.size(), gfx::Rect());
    out.ghosts.assign(ghosts_.size(), gfx::Rect());
    double x = kEdge - scroll_offset_;
    for (const Entry& e : entries) {
      x += e.gap * e.share;
      const double w = (e.fixed >= 0 ? e.fixed : unit) * e.share;
      // A pill is a small capsule, centred on the tab's plate; a tab is the
      // whole band, so its shadow has room.
      const int h = e.is_pill ? SetPill::kHeight : kBandHeight - plate_y;
      const int top = y + (e.is_pill ? (kTabHeight - SetPill::kHeight) / 2 : 0);
      const gfx::Rect rect(static_cast<int>(std::lround(x)), top,
                           static_cast<int>(std::lround(w)), h);
      if (e.item >= 0) {
        out.items[e.item] = rect;
      } else {
        out.ghosts[e.ghost] = rect;
      }
      x += w;
    }
    out.chip = gfx::Rect(
        static_cast<int>(std::lround(entries.empty() ? x : x + kGroupGap)), y,
        kTabHeight, kTabHeight);
    return out;
  };

  // Where everything belongs once the slide is over.
  const Rects final_rects = compute(false);
  targets_ = final_rects.items;
  const Rects now = weighted ? compute(true) : final_rects;

  // A set being dragged takes its tabs with it.
  int span_dx = 0;
  const bool span_drag = drag_view_ && views::IsViewClass<SetPill>(drag_view_) &&
                         drag_span_.first < drag_span_.second &&
                         drag_span_.second <= targets_.size();
  if (span_drag) {
    const int w =
        targets_[drag_span_.second - 1].right() - targets_[drag_span_.first].x();
    span_dx = std::clamp(drag_x_, kEdge, std::max(kEdge, width() - w)) -
              targets_[drag_span_.first].x();
  }
  for (size_t i = 0; i < items_.size(); ++i) {
    views::View* item = items_[i];
    if (span_drag && i > drag_span_.first && i < drag_span_.second) {
      gfx::Rect rect = targets_[i];
      rect.Offset(span_dx, 0);
      item->SetBoundsRect(rect);
      continue;
    }
    if (item == drag_view_) {
      // The dragged tab follows the pointer; its slot stays reserved so the
      // others do not close up under it.
      const gfx::Rect& target = targets_[i];
      item->SetBounds(std::clamp(drag_x_, kEdge,
                                 std::max(kEdge, width() - target.width())),
                      target.y(), target.width(), target.height());
      continue;
    }
    gfx::Rect rect = now.items[i];
    float opacity = 1.f;
    if (weighted) {
      opacity = static_cast<float>(entering_share(i));
    } else if (animating && i < anim_info_.size() && anim_info_[i].has_from) {
      // A reorder: each one slides from where it was.
      rect = gfx::Tween::RectValueBetween(t, anim_info_[i].from, rect);
    }
    item->SetBoundsRect(rect);
    if (item->layer()) {
      item->layer()->SetOpacity(opacity);
    }
  }
  if (new_tab_button_) {
    gfx::Rect chip_rect = now.chip;
    std::optional<SkColor> backdrop;
    if (one_strip_) {
      // Past the end of the room, the "+" stays at the strip's edge (still
      // reachable) and the tabs scroll beneath it.
      const int limit = width() - kEdge - kDragZone - kTabHeight;
      if (chip_rect.x() > limit) {
        chip_rect.set_x(limit);
        if (GetColorProvider()) {
          backdrop = GetColorProvider()->GetColor(kColorToolbar);
        }
      }
      // The strip clips to its own bounds, less the title-bar room at its end --
      // and, with the "+" held at the end, to the "+" itself, so no tab shows
      // (or can be clicked) beyond it.
      const int visible_right = std::max(
          0, backdrop ? chip_rect.right() : width() - kDragZone);
      if (layer()) {
        layer()->SetClipRect(gfx::Rect(0, 0, visible_right, height()));
      }
      for (views::View* item : items_) {
        if (item == drag_view_) {
          continue;
        }
        const gfx::Rect& r = item->bounds();
        item->SetVisible(r.right() > 0 && r.x() < visible_right);
      }
    }
    if (auto* chip = views::AsViewClass<NewTabChip>(new_tab_button_.get())) {
      chip->SetBackdrop(backdrop);
    }
    new_tab_button_->SetBoundsRect(chip_rect);
    if (new_tab_button_->layer()) {
      new_tab_button_->layer()->SetOpacity(1.f);
    }
  }
  for (size_t g = 0; g < ghosts_.size(); ++g) {
    if (!ghosts_[g].view) {
      continue;
    }
    ghosts_[g].view->SetBoundsRect(now.ghosts[g]);
    if (ghosts_[g].view->layer()) {
      ghosts_[g].view->layer()->SetOpacity(static_cast<float>(1.0 - t));
    }
  }
}

gfx::Size ZephyrusTabStrip::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  if (!one_strip_) {
    return views::View::CalculatePreferredSize(available_size);
  }
  // Modest, on purpose: the toolbar gives a child its preferred width before the
  // controls beside it get theirs, and a strip that asked for every tab would
  // push them into the overflow menu. What is left over is offered afterwards.
  return gfx::Size(2 * kEdge + 2 * kMinTabWidth + kTabHeight + kGroupGap,
                   kBandHeight);
}

gfx::Size ZephyrusTabStrip::GetMinimumSize() const {
  if (!one_strip_) {
    return views::View::GetMinimumSize();
  }
  return gfx::Size(
      2 * kEdge + kMinTabWidth + kTabHeight + kGroupGap + kDragZone,
      kBandHeight);
}

void ZephyrusTabStrip::SetOneStrip(bool on) {
  if (one_strip_ == on) {
    return;
  }
  one_strip_ = on;
  top_inset_ = on ? 0 : kTopInsetAlone;
  scroll_offset_ = 0;
  // In the title bar the strip has neighbours it must not paint over when its
  // tabs scroll: clip to its own bounds.
  if (on) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetMasksToBounds(true);
  } else if (layer()) {
    DestroyLayer();
  }
  // The tabs are shown with or without their sets, so they are made again. A
  // strip that is not showing rebuilds when it next shows.
  ScheduleRebuild();
  InvalidateLayout();
  PreferredSizeChanged();
}

void ZephyrusTabStrip::ScrollActiveIntoView() {
  content::WebContents* active = tab_strip_model_->GetActiveWebContents();
  if (!active || width() <= 0) {
    return;
  }
  DeprecatedLayoutImmediately();
  for (size_t i = 0; i < items_.size(); ++i) {
    auto* tab = views::AsViewClass<StripTab>(items_[i].get());
    if (!tab || tab->contents() != active) {
      continue;
    }
    // Where it will be, not where the slide has got to.
    const gfx::Rect place =
        i < targets_.size() ? targets_[i] : items_[i]->bounds();
    const int left = place.x();
    const int right = place.right();
    const int limit = width() - kEdge - kTabHeight - kGroupGap -
                      (one_strip_ ? kDragZone : 0);
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

void ZephyrusTabStrip::ToggleTabMuted(
    base::WeakPtr<content::WebContents> contents) {
  content::WebContents* target = contents.get();
  if (!target) {
    return;
  }
  target->SetAudioMuted(!target->IsAudioMuted());
  // Posted: this is called from inside the audio button's own click, and the
  // rebuild replaces that button.
  ScheduleRebuild();
}

void ZephyrusTabStrip::BreakSplit(split_tabs::SplitTabId split_id) {
  if (tab_strip_model_->GetSplitData(split_id)) {
    tab_strip_model_->RemoveSplit(split_id);
  }
}

void ZephyrusTabStrip::NewTab() {
  chrome::NewTab(browser_view_->browser(), NewTabTypes::kNewTabButton);
}

size_t ZephyrusTabStrip::ItemIndex(const views::View* view) const {
  for (size_t i = 0; i < items_.size(); ++i) {
    if (items_[i] == view) {
      return i;
    }
  }
  return items_.size();
}

std::pair<size_t, size_t> ZephyrusTabStrip::TabGroupRange(size_t item) const {
  for (const auto& [begin, end] : set_spans_) {
    if (item > begin && item < end) {
      return {begin + 1, end};
    }
  }
  // Loose tabs come after every set.
  size_t lo = first_flexible_;
  if (!set_spans_.empty()) {
    lo = set_spans_.back().second;
  }
  return {lo, items_.size()};
}

std::vector<ZephyrusTabStrip::Unit> ZephyrusTabStrip::UnitsIn(
    size_t begin,
    size_t end) const {
  std::vector<Unit> units;
  for (size_t i = begin; i < end;) {
    auto* tab = views::AsViewClass<StripTab>(items_[i].get());
    if (tab && tab->in_split() && i + 2 < end &&
        views::IsViewClass<SplitSeam>(items_[i + 1].get())) {
      units.emplace_back(i, i + 3);
      i += 3;
    } else {
      units.emplace_back(i, i + 1);
      ++i;
    }
  }
  return units;
}

bool ZephyrusTabStrip::GapBetween(size_t before, size_t after) const {
  auto category = [&](size_t i) {
    if (i < first_flexible_) {
      return 0;  // Pinned.
    }
    for (const auto& [begin, end] : set_spans_) {
      if (i >= begin && i < end) {
        return 1;  // In a set.
      }
    }
    return 2;  // Loose.
  };
  return category(before) != category(after) ||
         views::IsViewClass<SetPill>(items_[after].get());
}

void ZephyrusTabStrip::BeginDragPreview() {
  // Whatever slide is still running is settled first.
  ClearLayoutAnimation();
  ignore_anim_end_ = true;
  layout_anim_.Reset(1.0);
  ignore_anim_end_ = false;
  order_.assign(items_.size(), 0);
  std::iota(order_.begin(), order_.end(), size_t{0});
}

void ZephyrusTabStrip::SlideFromCurrent(bool include_held) {
  anim_info_.assign(items_.size(), AnimInfo());
  for (size_t i = 0; i < items_.size(); ++i) {
    if (!include_held && items_[i] == drag_view_) {
      continue;  // It follows the pointer, not the slide.
    }
    anim_info_[i].from = items_[i]->bounds();
    anim_info_[i].has_from = true;
  }
  entering_count_ = 0;
  ignore_anim_end_ = true;
  layout_anim_.Reset(0.0);
  ignore_anim_end_ = false;
  layout_anim_.Show();
}

void ZephyrusTabStrip::SetPreviewOrder(std::vector<size_t> order) {
  if (order == order_) {
    return;
  }
  SlideFromCurrent(/*include_held=*/false);
  order_ = std::move(order);
  InvalidateLayout();
}

void ZephyrusTabStrip::SetDropHighlight(
    const std::optional<tab_groups::TabGroupId>& set) {
  for (views::View* item : items_) {
    if (auto* pill = views::AsViewClass<SetPill>(item)) {
      pill->SetDropHighlight(set && pill->set_id() == *set);
    }
  }
}

namespace {

// The set whose span (its pill and tabs, with a little air round it) holds `x`.
std::optional<tab_groups::TabGroupId> SetAt(
    int x,
    const std::vector<raw_ptr<views::View>>& items,
    const std::vector<std::pair<size_t, size_t>>& spans,
    const std::vector<gfx::Rect>& targets) {
  constexpr int kAir = 8;
  for (const auto& [begin, end] : spans) {
    if (begin >= end || end > targets.size()) {
      continue;
    }
    int left = INT_MAX;
    int right = INT_MIN;
    for (size_t k = begin; k < end; ++k) {
      left = std::min(left, targets[k].x());
      right = std::max(right, targets[k].right());
    }
    if (x >= left - kAir && x <= right + kAir) {
      if (auto* pill = views::AsViewClass<SetPill>(items[begin].get())) {
        return pill->set_id();
      }
    }
  }
  return std::nullopt;
}

}  // namespace

void ZephyrusTabStrip::OnTabDragged(views::View* tab, int x_in_strip) {
  if (!dragging_) {
    BeginDragPreview();
    if (auto* held = views::AsViewClass<StripTab>(tab)) {
      held->SetDragged(true);
    }
    // The held tab paints over its neighbours.
    ReorderChildView(tab, children().size() - 1);
  }
  dragging_ = true;
  drag_view_ = tab;
  drag_x_ = x_in_strip;

  // Live reorder among the tabs it may move between: it takes the slot whose
  // neighbours' middles it has passed, and they slide to make room.
  const size_t held = ItemIndex(tab);
  if (held < items_.size()) {
    const auto [lo, hi] = TabGroupRange(held);
    const int center = x_in_strip + tab->width() / 2;
    std::vector<Unit> others;
    std::optional<Unit> mine;
    for (const Unit& unit : UnitsIn(lo, hi)) {
      if (unit.first == held) {
        mine = unit;
      } else {
        others.push_back(unit);
      }
    }
    if (mine) {
      size_t slot = 0;
      for (const Unit& unit : others) {
        if (unit.second - 1 < targets_.size() &&
            (targets_[unit.first].x() + targets_[unit.second - 1].right()) / 2 <
                center) {
          ++slot;
        }
      }
      others.insert(others.begin() + slot, *mine);
      std::vector<size_t> order(items_.size());
      std::iota(order.begin(), order.end(), size_t{0});
      size_t pos = lo;
      for (const Unit& unit : others) {
        for (size_t j = unit.first; j < unit.second; ++j) {
          order[pos++] = j;
        }
      }
      SetPreviewOrder(std::move(order));
    }
  }
  // Over another set: it lights, and letting go adds the tab to it.
  std::optional<tab_groups::TabGroupId> over =
      SetAt(x_in_strip + tab->width() / 2, items_, set_spans_, targets_);
  if (over) {
    if (auto* strip_tab = views::AsViewClass<StripTab>(tab)) {
      const int index = strip_tab->contents()
                            ? tab_strip_model_->GetIndexOfWebContents(
                                  strip_tab->contents())
                            : TabStripModel::kNoTab;
      if (index != TabStripModel::kNoTab &&
          tab_strip_model_->GetTabGroupForTab(index) == over) {
        over.reset();  // Its own set: that is a reorder.
      }
    }
  }
  SetDropHighlight(over);
  InvalidateLayout();
}

void ZephyrusTabStrip::OnPillDragged(views::View* pill, int x_in_strip) {
  if (drag_view_ != pill) {
    drag_span_ = {0, 0};
    for (size_t i = 0; i < items_.size(); ++i) {
      if (items_[i] == pill) {
        for (const auto& span : set_spans_) {
          if (span.first == i) {
            drag_span_ = span;
          }
        }
      }
    }
  }
  if (!dragging_) {
    BeginDragPreview();
    // The set (its pill and tabs) paints over the others as it moves.
    for (size_t i = drag_span_.first; i < drag_span_.second; ++i) {
      ReorderChildView(items_[i], children().size() - 1);
    }
  }
  dragging_ = true;
  drag_view_ = pill;
  drag_x_ = x_in_strip;

  // Live reorder of the sets: the held one takes the place whose neighbours'
  // middles its pill has passed.
  if (!set_spans_.empty() && drag_span_.first < drag_span_.second &&
      drag_span_.second <= targets_.size()) {
    const int center = x_in_strip + targets_[drag_span_.first].width() / 2;
    std::vector<Unit> others;
    for (const auto& span : set_spans_) {
      if (span != drag_span_ && span.second <= targets_.size()) {
        others.push_back(span);
      }
    }
    size_t slot = 0;
    for (const Unit& unit : others) {
      if ((targets_[unit.first].x() + targets_[unit.second - 1].right()) / 2 <
          center) {
        ++slot;
      }
    }
    others.insert(others.begin() + slot, drag_span_);
    std::vector<size_t> order(items_.size());
    std::iota(order.begin(), order.end(), size_t{0});
    size_t pos = set_spans_.front().first;
    for (const Unit& unit : others) {
      for (size_t j = unit.first; j < unit.second; ++j) {
        order[pos++] = j;
      }
    }
    SetPreviewOrder(std::move(order));
  }
  InvalidateLayout();
}

void ZephyrusTabStrip::OnPillDragEnded(views::View* pill_view, bool cancelled) {
  auto* pill = views::AsViewClass<SetPill>(pill_view);
  const std::pair<size_t, size_t> span = drag_span_;
  // The set eases into its place from where it was let go.
  SlideFromCurrent(/*include_held=*/true);
  dragging_ = false;
  drag_view_ = nullptr;
  drag_span_ = {0, 0};
  if (pill && !cancelled && span.first < span.second &&
      span.second <= targets_.size()) {
    // The order the strip was showing when it was let go: the sets as they
    // stand on screen, which is what the preview has been promising.
    std::vector<tab_groups::TabGroupId> order;
    if (order_.size() == items_.size()) {
      for (size_t item : order_) {
        if (auto* other = views::AsViewClass<SetPill>(items_[item].get())) {
          order.push_back(other->set_id());
        }
      }
    }
    if (order.empty()) {
      order.push_back(pill->set_id());
    }
    SetZephyrusSetOrder(browser_view_->browser()->GetProfile()->GetPrefs(),
                        order);
  }
  rebuild_deferred_ = false;
  force_animation_ = true;
  ScheduleRebuild();
  InvalidateLayout();
}

void ZephyrusTabStrip::OnTabDragEnded(
    views::View* tab,
    base::WeakPtr<content::WebContents> contents) {
  // The slot the pointer ended over: the flexible item whose centre is
  // nearest the dragged tab's centre. Model indices, not view indices, since
  // the strip shows only this workspace's slice of the model.
  const int drop_center = drag_x_ + tab->width() / 2;
  SetDropHighlight(std::nullopt);
  // The tab eases into its place from where it was let go.
  SlideFromCurrent(/*include_held=*/true);
  // Dropped on another set, the tab joins it; dropped clear of its own set, it
  // leaves it; otherwise it only reorders (below).
  {
    const int from_index =
        contents ? tab_strip_model_->GetIndexOfWebContents(contents.get())
                 : TabStripModel::kNoTab;
    if (from_index != TabStripModel::kNoTab &&
        !tab_strip_model_->IsTabPinned(from_index)) {
      const std::optional<tab_groups::TabGroupId> own =
          tab_strip_model_->GetTabGroupForTab(from_index);
      const std::optional<tab_groups::TabGroupId> over =
          SetAt(drop_center, items_, set_spans_, targets_);
      bool moved = false;
      if (over && over != own) {
        tab_strip_model_->AddToExistingGroup({from_index}, *over);
        moved = true;
      } else if (!over && own) {
        tab_strip_model_->RemoveFromGroup({from_index});
        moved = true;
      }
      if (moved) {
        dragging_ = false;
        drag_view_ = nullptr;
        rebuild_deferred_ = false;
        force_animation_ = true;
        ScheduleRebuild();
        InvalidateLayout();
        return;
      }
    }
  }
  // The slot the preview left the tab in, among the tabs it moves between: read
  // from the order the strip was showing, not guessed again from the pointer.
  int target_index = TabStripModel::kNoTab;
  const size_t held = ItemIndex(tab);
  if (held < items_.size() && order_.size() == items_.size()) {
    const auto [lo, hi] = TabGroupRange(held);
    const std::vector<Unit> units = UnitsIn(lo, hi);
    size_t from_pos = units.size();
    for (size_t u = 0; u < units.size(); ++u) {
      if (units[u].first == held) {
        from_pos = u;
      }
    }
    // Where the held unit stands in the on-screen sequence.
    size_t to_pos = from_pos;
    size_t seen = 0;
    for (size_t k = lo; k < hi; ++k) {
      for (size_t u = 0; u < units.size(); ++u) {
        if (units[u].first == order_[k]) {
          if (order_[k] == held) {
            to_pos = seen;
          }
          ++seen;
        }
      }
    }
    if (from_pos < units.size() && to_pos != from_pos && to_pos < units.size()) {
      // Moving right lands after the tab that held the slot; moving left, before.
      const Unit& slot = units[to_pos];
      const size_t pick = to_pos > from_pos ? slot.second - 1 : slot.first;
      if (auto* other = views::AsViewClass<StripTab>(items_[pick].get());
          other && other->contents()) {
        target_index = tab_strip_model_->GetIndexOfWebContents(other->contents());
      }
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
  force_animation_ = true;
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

void ZephyrusTabStrip::TabGroupedStateChanged(
    TabStripModel* tab_strip_model,
    std::optional<tab_groups::TabGroupId> old_group,
    std::optional<tab_groups::TabGroupId> new_group,
    tabs::TabInterface* tab,
    int index) {
  ScheduleRebuild();
}

void ZephyrusTabStrip::OnTabGroupChanged(const TabGroupChange& change) {
  ScheduleRebuild();
}

void ZephyrusTabStrip::ToggleSetCollapsed(const tab_groups::TabGroupId& id) {
  if (ZephyrusSidebarView* sidebar = FindZephyrusSidebar(browser_view_)) {
    const bool collapsed = GetZephyrusSetInfo(
        browser_view_->browser()->GetProfile()->GetPrefs(), id).collapsed;
    sidebar->SetSetCollapsed(id, !collapsed);
  }
  ScheduleRebuild();
}

void ZephyrusTabStrip::RenameSet(const tab_groups::TabGroupId& id,
                                 const std::u16string& name) {
  if (ZephyrusSidebarView* sidebar = FindZephyrusSidebar(browser_view_)) {
    sidebar->RenameSet(id, name);
  }
  ScheduleRebuild();
}

void ZephyrusTabStrip::ChangeSetIcon(const tab_groups::TabGroupId& id,
                                     const std::u16string& key) {
  if (ZephyrusSidebarView* sidebar = FindZephyrusSidebar(browser_view_)) {
    sidebar->ChangeSetIcon(id, key);
  }
  ScheduleRebuild();
}

void ZephyrusTabStrip::SetRenameActive(bool active) {
  rename_active_ = active;
  if (!active && rebuild_deferred_) {
    rebuild_deferred_ = false;
    ScheduleRebuild();
  }
}

void ZephyrusTabStrip::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (auto* pill = views::AsViewClass<SetPill>(source)) {
    menu_set_ = pill->set_id();
    const ZephyrusSetInfo info = GetZephyrusSetInfo(
        browser_view_->browser()->GetProfile()->GetPrefs(), pill->set_id());
    menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    menu_model_->AddItem(kCommandSetRename, u"Rename");
    menu_model_->AddItem(kCommandSetIcon, u"Change icon\u2026");
    menu_model_->AddItem(kCommandSetPin,
                         info.pinned ? u"Unpin from top" : u"Pin to top");
    menu_model_->AddItem(kCommandSetNewTab, u"New tab in set");
    menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    menu_model_->AddItem(kCommandSetClose, u"Close set");
    menu_model_->AddItem(kCommandSetUngroup, u"Ungroup set");
    menu_runner_ = std::make_unique<views::MenuRunner>(
        menu_model_.get(), views::MenuRunner::HAS_MNEMONICS |
                               views::MenuRunner::CONTEXT_MENU);
    menu_runner_->RunMenuAt(GetWidget(), nullptr,
                            gfx::Rect(point, gfx::Size()),
                            views::MenuAnchorPosition::kTopLeft, source_type);
    return;
  }
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
  // Pinned tabs have no room for the audio control, so this is how they mute.
  if (tab->contents()->IsCurrentlyAudible() ||
      tab->contents()->IsAudioMuted()) {
    menu_model_->AddItem(kCommandMute, tab->contents()->IsAudioMuted()
                                           ? u"Unmute tab"
                                           : u"Mute tab");
  }

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
  // Sets: a pinned tab is in none and cannot be put in one; and there are none
  // in the one-strip layout.
  if (!tab_strip_model_->IsTabPinned(index) && !one_strip_) {
    menu_set_ids_.clear();
    set_submenu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    const std::optional<tab_groups::TabGroupId> current =
        tab_strip_model_->GetTabGroupForTab(index);
    if (ZephyrusSidebarView* sidebar = FindZephyrusSidebar(browser_view_)) {
      for (const tab_groups::TabGroupId& id : sidebar->SetsInCurrentWorkspace()) {
        if (id == current) {
          continue;
        }
        const tab_groups::TabGroupVisualData* data =
            tab_strip_model_->group_model()->GetTabGroup(id)->visual_data();
        set_submenu_model_->AddItem(
            kCommandAddToSetBase + static_cast<int>(menu_set_ids_.size()),
            data && !data->title().empty() ? data->title() : u"Set");
        menu_set_ids_.push_back(id);
      }
    }
    if (!menu_set_ids_.empty()) {
      set_submenu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    }
    set_submenu_model_->AddItem(kCommandNewSet, u"New set");
    menu_model_->AddSubMenu(0, u"Add to set", set_submenu_model_.get());
    if (current) {
      menu_model_->AddItem(kCommandRemoveFromSet, u"Remove from set");
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
  // A set's own menu.
  if (command_id >= kCommandSetRename && command_id <= kCommandSetUngroup) {
    ZephyrusSidebarView* sidebar = FindZephyrusSidebar(browser_view_);
    if (!menu_set_ || !sidebar) {
      return;
    }
    const tab_groups::TabGroupId set = *menu_set_;
    const std::vector<tab_groups::TabGroupId> all =
        tab_strip_model_->group_model()->ListTabGroups();
    if (std::find(all.begin(), all.end(), set) == all.end()) {
      return;
    }
    SetPill* pill = nullptr;
    for (views::View* item : items_) {
      auto* candidate = views::AsViewClass<SetPill>(item);
      if (candidate && candidate->set_id() == set) {
        pill = candidate;
      }
    }
    switch (command_id) {
      case kCommandSetRename:
        if (pill) {
          pill->BeginRename();
        }
        return;
      case kCommandSetIcon:
        if (pill) {
          zephyrus::ShowIconPicker(
              pill, base::BindRepeating(&ZephyrusTabStrip::ChangeSetIcon,
                                        weak_factory_.GetWeakPtr(), set));
        }
        return;
      case kCommandSetPin:
        sidebar->ToggleSetPinned(set);
        break;
      case kCommandSetNewTab:
        sidebar->NewTabInSet(set);
        break;
      case kCommandSetClose:
        sidebar->CloseSet(set);
        break;
      case kCommandSetUngroup:
        sidebar->UngroupSet(set);
        break;
    }
    ScheduleRebuild();
    return;
  }
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
    case kCommandMute:
      contents->SetAudioMuted(!contents->IsAudioMuted());
      ScheduleRebuild();
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
    case kCommandNewSet:
      if (ZephyrusSidebarView* sidebar = FindZephyrusSidebar(browser_view_)) {
        sidebar->CreateSetWithTab(index);
      }
      return;
    case kCommandRemoveFromSet:
      tab_strip_model_->RemoveFromGroup({index});
      return;
    default:
      break;
  }
  if (command_id >= kCommandAddToSetBase) {
    const size_t slot = static_cast<size_t>(command_id - kCommandAddToSetBase);
    if (slot < menu_set_ids_.size() && !tab_strip_model_->IsTabPinned(index)) {
      tab_strip_model_->AddToExistingGroup({index}, menu_set_ids_[slot]);
    }
    return;
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
