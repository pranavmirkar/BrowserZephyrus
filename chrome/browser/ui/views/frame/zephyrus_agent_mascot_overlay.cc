// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_agent_mascot_overlay.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_mascot_bubble.h"
#include "chrome/browser/zephyrus/agent/lens_geometry.h"
#include "chrome/browser/zephyrus/agent/lens_mood_map.h"
#include "chrome/browser/zephyrus/agent/model_settings.h"
#include "chrome/browser/zephyrus/agent/pointer_path.h"
#include "components/prefs/pref_service.h"
#include "ui/display/screen.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view_class_properties.h"
#include "ui/aura/window.h"
#include "ui/views/widget/widget.h"

namespace zephyrus::agent {

namespace {

// The art's own reference points: the middle of the feet, which is where the
// body stands, and how far the fist reaches from there.
constexpr float kArtFeetX = 80.0f;
constexpr float kArtFeetY = 141.0f;

// Nothing walks faster than this, however far away the target is: a character
// that crosses the window in a blink is teleporting with extra steps.
constexpr float kMaxWalkSpeed = 800.0f;

// How big it is, in dip per unit of its art (the body is 120 units wide). It
// is small: a companion, not a cursor the size of a button. Standing at home it
// is about 50dp wide; out on a page a little smaller, so what it points at is
// mostly still visible.
constexpr float kHomeScale = 0.42f;
constexpr float kPageScale = 0.36f;
constexpr float kChromeScale = 0.32f;

// The lens is drawn into a square window of 240 units per unit of scale (at home
// scale, 0.42, about 100 px); its body is the 251 x 251 lens inside the 311 x 316
// artwork window, so about 81% of that wide and 80% tall. It stands on its lower
// edge like Zep stands on his feet: 91% of the way down the window.
constexpr float kLensWindowPerScale = 240.0f;
constexpr float kLensBodyWidthPerScale = kLensWindowPerScale * 251.0f / 316.0f;
constexpr float kLensBodyHeightPerScale = kLensWindowPerScale * 251.2f / 316.0f;
constexpr float kLensBaselineInWindow = 0.91f;
// Beside the target by this much, so the body never covers what it reaches for.
constexpr float kLensReachGap = 12.0f;
constexpr base::TimeDelta kPressRingFor = base::Milliseconds(650);

constexpr base::TimeDelta kFallAsleepAfter = base::Seconds(60);
constexpr base::TimeDelta kOutcomeShownFor = base::Seconds(9);

bool IsOutcome(MascotMood mood) {
  return mood == MascotMood::kHappy || mood == MascotMood::kConfused ||
         mood == MascotMood::kAlert;
}

}  // namespace

ZephyrusAgentMascotOverlay::ZephyrusAgentMascotOverlay(BrowserView* browser_view)
    : browser_view_(browser_view) {
  // It catches a mouse press ONLY on the character itself (see MouseWatcher), so
  // it can be picked up; everywhere else it is a drawing over the page and the
  // page is what the person is using.
  SetCanProcessEventsWithinSubtree(false);
  SetProperty(views::kViewIgnoredByLayoutKey, true);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  GetViewAccessibility().SetIsIgnored(true);
  SetVisible(false);

}

void ZephyrusAgentMascotOverlay::RefreshPresence() {
  // Watching the setting starts here, not in the constructor: the window's
  // browser is not always there yet when its views are being built.
  if (!observing_prefs_ && browser_view_ && browser_view_->browser()) {
    observing_prefs_ = true;
    pref_registrar_.Init(browser_view_->browser()->profile()->GetPrefs());
    pref_registrar_.Add(
        zephyrus::agent::kMascotPref,
        base::BindRepeating(&ZephyrusAgentMascotOverlay::RefreshPresence,
                            base::Unretained(this)));
    for (const char* pref : {zephyrus::agent::kMascotSkinPref,
                             zephyrus::agent::kMascotLensFillPref}) {
      pref_registrar_.Add(
          pref, base::BindRepeating(&ZephyrusAgentMascotOverlay::RefreshLook,
                                    base::Unretained(this)));
    }
  }
  RefreshLook();
  const bool enabled =
      !browser_view_ || !browser_view_->browser() ||
      browser_view_->browser()->profile()->GetPrefs()->GetBoolean(
          zephyrus::agent::kMascotPref);
  SetPresent(enabled);
}

ZephyrusAgentMascotOverlay::~ZephyrusAgentMascotOverlay() {
  if (watched_window_) {
    watched_window_->RemovePreTargetHandler(&mouse_watcher_);
  }
}

void ZephyrusAgentMascotOverlay::FitToParent() {
  if (!parent()) {
    return;
  }
  SetBoundsRect(parent()->GetLocalBounds());
  // On top of everything the window has, including views added after this one,
  // with the bubble above it: the order is [... overlay, bubble]. Reordered only
  // when it is wrong -- a reorder that ran on every layout would be a layout.
  std::vector<views::View*> want = {this};
  if (bubble_) {
    want.push_back(bubble_);
  }
  const auto& kids = parent()->children();
  const bool in_order =
      kids.size() >= want.size() &&
      std::equal(want.begin(), want.end(), kids.end() - want.size());
  if (!in_order) {
    for (views::View* view : want) {
      parent()->ReorderChildView(view, -1);
    }
  }
  PlaceBubble();
}

gfx::Rect ZephyrusAgentMascotOverlay::BodyBounds() const {
  const gfx::PointF feet = has_position_ ? feet_ : HomeFeet();
  const float scale = has_position_ ? scale_ : kHomeScale;
  const int w = std::max(1, static_cast<int>(std::lround(BodyWidth() * scale)));
  const int h = std::max(1, static_cast<int>(std::lround(BodyHeight() * scale)));
  return gfx::Rect(static_cast<int>(std::lround(feet.x())) - w / 2,
                   static_cast<int>(std::lround(feet.y())) - h, w, h);
}

void ZephyrusAgentMascotOverlay::SetListening(bool listening) {
  if (listening_ == listening) {
    return;
  }
  listening_ = listening;
  // Shown for the light alone when the mascot is off.
  SetVisible(present_ || listening_);
  SchedulePaint();
}

void ZephyrusAgentMascotOverlay::PlaceBubble() {
  if (bubble_ && bubble_->HasContent()) {
    // Kept inside the page while the mascot is on it: a bubble wider than the
    // room beside the mascot would otherwise hang over the agent panel and hide
    // the very card that asks the same question. Anywhere else (the address bar,
    // the tabs) the whole window is the room.
    gfx::Rect area = GetLocalBounds();
    const gfx::Rect body = BodyBounds();
    if (views::View* card =
            browser_view_ ? browser_view_->contents_container() : nullptr) {
      if (card->IsDrawn() && card->width() > 120) {
        gfx::Rect page = card->GetLocalBounds();
        gfx::Point origin = page.origin();
        views::View::ConvertPointToTarget(card, this, &origin);
        page.set_origin(origin);
        if (page.Contains(body.CenterPoint())) {
          area = page;
        }
      }
    }
    bubble_->PlaceNear(body, area);
  }
}

void ZephyrusAgentMascotOverlay::SetPresent(bool present) {
  if (present == present_) {
    return;
  }
  present_ = present;
  SetVisible(present || listening_);
  if (present) {
    if (!has_position_) {
      feet_ = HomeFeet();
      has_position_ = true;
    }
    walk_target_ = feet_;
    velocity_ = gfx::Vector2dF();
    driven_ = false;
    at_home_ = true;
    scale_target_ = kHomeScale;
    scale_ = kHomeScale;
    last_tick_ = base::TimeTicks::Now();
    SetMood(MascotMood::kIdle);
  } else {
    has_position_ = false;
    driven_ = false;
    held_ = false;
    timer_.Stop();
  }
  UpdateTimer();
}

void ZephyrusAgentMascotOverlay::SetMood(MascotMood mood) {
  // Nothing changes its mind while it is being held.
  if (held_) {
    return;
  }
  rig_.SetMood(mood);
  mood_since_ = base::TimeTicks::Now();
  UpdateTimer();
  SchedulePaint();
}

void ZephyrusAgentMascotOverlay::Wake() {
  if (!present_) {
    return;
  }
  SetMood(MascotMood::kWaving);
}

bool ZephyrusAgentMascotOverlay::GoTo(Place place, base::TimeDelta budget) {
  if (!present_ || place == Place::kStay || held_) {
    return false;
  }
  driven_ = false;
  const gfx::PointF target = FeetFor(place);
  const bool reachable =
      place == Place::kHome ||
      EstimateWalkTime((target - feet_).Length()) <= budget;
  if (!reachable) {
    // Not in time. It stays put and does its part from here, turned toward
    // where it would have gone: the address bar is above and to one side, and a
    // character that faces it reads as reaching for it.
    rig_.SetFacingLeft(target.x() < feet_.x());
    walk_target_ = feet_;
    UpdateTimer();
    return false;
  }
  at_home_ = place == Place::kHome;
  walk_target_ = target;
  scale_target_ = place == Place::kHome ? kHomeScale : kChromeScale;
  if (!gfx::Animation::ShouldRenderRichAnimation()) {
    feet_ = walk_target_;
    scale_ = scale_target_;
    SchedulePaint();
  }
  UpdateTimer();
  return true;
}

// ---- Where things are -------------------------------------------------------

bool ZephyrusAgentMascotOverlay::HomeIsKnown() const {
  views::View* card = browser_view_ ? browser_view_->contents_container() : nullptr;
  return card && card->IsDrawn() && card->width() > 120;
}

gfx::PointF ZephyrusAgentMascotOverlay::HomeFeet() const {
  // Where the person put it, if they did.
  if (parked_ && width() > 0 && height() > 0) {
    return gfx::PointF(parked_->x() * width(), parked_->y() * height());
  }
  // Standing on the lower edge of the page card, near its right end. The card
  // shrinks when the agent panel opens, and home moves with it.
  views::View* card = browser_view_ ? browser_view_->contents_container() : nullptr;
  if (card && card->IsDrawn() && card->width() > 120) {
    gfx::Point p(card->width() - 52, card->height() - 6);
    views::View::ConvertPointToTarget(card, this, &p);
    return gfx::PointF(p);
  }
  return gfx::PointF(std::max(60, width() - 80), std::max(80, height() - 24));
}

gfx::PointF ZephyrusAgentMascotOverlay::FeetFor(Place place) const {
  views::View* bar = nullptr;
  float across = 0.45f;
  switch (place) {
    case Place::kHome:
    case Place::kStay:
      return place == Place::kStay ? feet_ : HomeFeet();
    case Place::kAddressBar:
      bar = browser_view_ ? browser_view_->toolbar() : nullptr;
      break;
    case Place::kTabStrip:
      bar = browser_view_ ? browser_view_->tab_strip_view() : nullptr;
      across = 0.25f;
      break;
  }
  if (!bar || !bar->IsDrawn()) {
    return HomeFeet();
  }
  gfx::Point p(static_cast<int>(bar->width() * across), bar->height());
  views::View::ConvertPointToTarget(bar, this, &p);
  return gfx::PointF(p);
}

gfx::PointF ZephyrusAgentMascotOverlay::Tip() const {
  if (lens()) {
    // No hand: the point it reaches for is at the middle of the side it faces,
    // or the top of the body while the person has it by the cursor.
    const float w = BodyWidth() * scale_;
    const float h = BodyHeight() * scale_;
    if (held_) {
      return feet_ - grab_offset_;
    }
    const float side = rig_.facing_left() ? -1.0f : 1.0f;
    return gfx::PointF(feet_.x() + side * (w / 2 + kLensReachGap),
                       feet_.y() - h / 2);
  }
  const gfx::PointF tip = rig_.PointerTip();
  return gfx::PointF(feet_.x() + (tip.x() - kArtFeetX) * scale_,
                     feet_.y() + (tip.y() - kArtFeetY) * scale_);
}

gfx::PointF ZephyrusAgentMascotOverlay::FeetForTip(gfx::PointF tip) const {
  if (lens()) {
    const float w = BodyWidth() * scale_;
    const float h = BodyHeight() * scale_;
    if (held_) {
      return tip + grab_offset_;
    }
    const float side = rig_.facing_left() ? -1.0f : 1.0f;
    return gfx::PointF(tip.x() - side * (w / 2 + kLensReachGap), tip.y() + h / 2);
  }
  const gfx::PointF art = rig_.PointerTip();
  return gfx::PointF(tip.x() - (art.x() - kArtFeetX) * scale_,
                     tip.y() - (art.y() - kArtFeetY) * scale_);
}

std::optional<gfx::Point> ZephyrusAgentMascotOverlay::PointerHome() {
  if (!present_ || !has_position_) {
    return std::nullopt;
  }
  gfx::Point p = gfx::ToRoundedPoint(Tip());
  views::View::ConvertPointToScreen(this, &p);
  return p;
}

// ---- The agent's pointer ----------------------------------------------------

void ZephyrusAgentMascotOverlay::OnPointer(const PointerEvent& event) {
  // The person has it: the agent's pointer waits, and its actions on the page
  // carry on without the drawing.
  if (held_) {
    return;
  }
  if (!present_) {
    // A task running with the panel closed still wants its mascot.
    SetPresent(true);
  }
  using Kind = PointerEvent::Kind;
  const auto to_local = [&]() {
    gfx::Point p = event.position;
    views::View::ConvertPointFromScreen(this, &p);
    return gfx::PointF(p);
  };

  switch (event.kind) {
    case Kind::kMove:
    case Kind::kArrive:
    case Kind::kPress:
    case Kind::kRelease:
    case Kind::kScroll: {
      pointer_ = to_local();
      driven_ = true;
      at_home_ = false;
      scale_target_ = kPageScale;
      if (event.kind == Kind::kScroll) {
        rig_.SetScrollDirection(event.direction);
        if (rig_.mood() != MascotMood::kScrolling) {
          SetMood(MascotMood::kScrolling);
        }
      } else if (rig_.mood() != MascotMood::kClicking) {
        SetMood(MascotMood::kClicking);
      }
      if (event.kind == Kind::kPress) {
        rig_.Kick(MascotKick::kPress);
        if (lens_model_) {
          // The lens flinches and the click is shown where the page got it.
          lens_model_->Squash();
          lens_model_->Press(kLensCenterX, 262);
          press_ring_ = PressRing{pointer_, base::TimeTicks::Now()};
        }
      }
      if (!gfx::Animation::ShouldRenderRichAnimation()) {
        feet_ = FeetForTip(pointer_);
        SchedulePaint();
      }
      break;
    }
    case Kind::kTypeBegin:
      // Stands where it is and types; the keyboard is at its feet.
      driven_ = false;
      walk_target_ = feet_;
      SetMood(MascotMood::kTyping);
      break;
    case Kind::kTypeEnd:
    case Kind::kIdle:
      driven_ = false;
      walk_target_ = feet_;
      if (rig_.mood() == MascotMood::kClicking ||
          rig_.mood() == MascotMood::kScrolling ||
          rig_.mood() == MascotMood::kTyping) {
        SetMood(MascotMood::kThinking);
      }
      break;
  }
  UpdateTimer();
}

// ---- Time -------------------------------------------------------------------

void ZephyrusAgentMascotOverlay::UpdateTimer() {
  const bool should_run = present_ && GetWidget() && IsDrawn() &&
                          gfx::Animation::ShouldRenderRichAnimation();
  if (!should_run) {
    timer_.Stop();
    return;
  }
  const bool moving = driven_ || held_ || velocity_.Length() > 1.0f ||
                      (walk_target_ - feet_).Length() > 1.0f;
  // Always present now, so the resting cost matters: a character that is only
  // breathing does not need thirty frames a second, and a window that is
  // minimised needs almost none. (A ticking timer nobody can see was found
  // once before, at fifty wakeups a second on an idle page.)
  const bool hidden = GetWidget()->IsMinimized() || !GetWidget()->IsVisible();
  // Ten frames a second while it stands there breathing, four while it sleeps:
  // slow motion is all that is on screen, and every frame is a wake-up for the
  // browser and a composite for the GPU.
  base::TimeDelta interval = base::Milliseconds(100);
  if (lens()) {
    // The lens's eyes drift and its body breathes continuously, so ten frames a
    // second looks like stuttering; thirty is the spec's idle ceiling. Waiting
    // (idle) is slow motion, and every frame is a composite of the whole window
    // for the GPU, so it gets twenty; anything it is doing gets thirty.
    interval = base::Milliseconds(rig_.mood() == MascotMood::kIdle ? 50 : 33);
  }
  if (hidden) {
    interval = base::Seconds(1);
  } else if (moving) {
    interval = base::Milliseconds(16);
  } else if (rig_.mood() == MascotMood::kSleeping) {
    interval = base::Milliseconds(lens() ? 100 : 250);
  }
  if (timer_.IsRunning() && timer_.GetCurrentDelay() == interval) {
    return;
  }
  last_tick_ = base::TimeTicks::Now();
  timer_.Start(FROM_HERE, interval,
               base::BindRepeating(&ZephyrusAgentMascotOverlay::Tick,
                                   base::Unretained(this)));
}

void ZephyrusAgentMascotOverlay::Tick() {
  const base::TimeTicks now = base::TimeTicks::Now();
  const base::TimeDelta dt = std::min(now - last_tick_, base::Milliseconds(100));
  last_tick_ = now;
  Advance(dt);
  // The bubble goes where it goes.
  PlaceBubble();

  // It falls asleep when nothing has happened for a while, and an outcome
  // fades back to waiting rather than hopping forever.
  const base::TimeDelta shown = now - mood_since_;
  if (rig_.mood() == MascotMood::kIdle && shown > kFallAsleepAfter) {
    SetMood(MascotMood::kYawning);
  } else if (IsOutcome(rig_.mood()) && shown > kOutcomeShownFor) {
    SetMood(MascotMood::kIdle);
  }
  // Only where the character is, and was. The overlay covers the whole window,
  // and repainting all of it twenty times a second while nothing happened was
  // measured at about 23% of a core on an idle browser (the GPU process alone
  // 20%), against 0.5% with the mascot off. Its previous place is included so
  // that what it leaves behind is erased.
  //
  // And not at all when the frame would come out the same. The art is drawn in
  // whole pixels, so a character standing still and breathing slowly repeats
  // the same picture for many ticks in a row.
  SnapRestingBreathToPixels();
  std::vector<zephyrus_setup::Rect> frame = rig_.Draw();
  if (lens()) {
    // The lens never repeats a picture (its eyes drift, it breathes), and its
    // quality follows how the machine is coping.
    lens_quality_.OnFrame(dt.InSecondsF(),
                          look_ == zephyrus::agent::MascotLook::kLensFluid);
    if (press_ring_ && now - press_ring_->since > kPressRingFor) {
      press_ring_.reset();
    }
    const gfx::Rect dirty = DirtyRect();
    SchedulePaintInRect(
        gfx::UnionRects(gfx::UnionRects(dirty, last_dirty_), painted_));
    last_dirty_ = dirty;
    UpdateTimer();
    return;
  }
  // Compared as it will be DRAWN, in whole device pixels. The rig's rectangles
  // move by fractions of a pixel while it breathes (a breath is less than one
  // device pixel at home size), so comparing the floats called every tick a new
  // picture and repainted ten times a second to draw the same pixels.
  QuantizeToDevicePixels(frame);
  const bool same = feet_ == last_feet_ && scale_ == last_scale_ &&
                    SameFrame(frame, last_frame_);
  if (!same) {
    const gfx::Rect dirty = DirtyRect();
    // Also wherever it was ACTUALLY painted last, not only where the previous
    // tick meant to. The first drawing happens before the character has been
    // placed at home, and the move to home is a jump, not a step: repainting
    // only the new place left the first drawing standing as a second mascot in
    // the top-left corner of every new window.
    SchedulePaintInRect(
        gfx::UnionRects(gfx::UnionRects(dirty, last_dirty_), painted_));
    last_dirty_ = dirty;
    last_frame_ = std::move(frame);
    last_feet_ = feet_;
    last_scale_ = scale_;
  }
  // The pace depends on whether it is moving, which just changed.
  UpdateTimer();
}

void ZephyrusAgentMascotOverlay::SnapRestingBreathToPixels() {
  // One device pixel in the art's units, so a resting breath moves in whole
  // pixels (see MascotRig::SetPixelSnap).
  const float dsf = layer() ? layer()->device_scale_factor() : 1.0f;
  rig_.SetPixelSnap(1.0f / std::max(0.05f, scale_ * dsf));
}

void ZephyrusAgentMascotOverlay::QuantizeToDevicePixels(
    std::vector<zephyrus_setup::Rect>& frame) const {
  // The same arithmetic as OnPaint, so "same" here means "same pixels there".
  const float dsf = layer() ? layer()->device_scale_factor() : 1.0f;
  zephyrus::agent::QuantizeToDevicePixels(
      frame, std::round((feet_.x() - kArtFeetX * scale_) * dsf),
      std::round((feet_.y() - kArtFeetY * scale_) * dsf), scale_ * dsf);
}

bool ZephyrusAgentMascotOverlay::SameFrame(
    const std::vector<zephyrus_setup::Rect>& a,
    const std::vector<zephyrus_setup::Rect>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].w != b[i].w ||
        a[i].h != b[i].h || a[i].color != b[i].color) {
      return false;
    }
  }
  return true;
}

gfx::Rect ZephyrusAgentMascotOverlay::DirtyRect() const {
  // The body, the arm that reaches out of it, the listening light and the
  // shadow, with room to spare: a region that is too small leaves pieces of the
  // character behind, which is worse than repainting a little extra.
  constexpr int kMargin = 72;
  gfx::Rect rect = BodyBounds();
  rect.Outset(kMargin);
  const gfx::PointF tip = Tip();
  rect.Union(gfx::Rect(static_cast<int>(tip.x()) - 48,
                       static_cast<int>(tip.y()) - 48, 96, 96));
  if (lens() && press_ring_) {
    // The ring where the page got the press.
    rect.Union(gfx::Rect(static_cast<int>(press_ring_->at.x()) - 44,
                         static_cast<int>(press_ring_->at.y()) - 44, 88, 88));
  }
  return rect;
}

void ZephyrusAgentMascotOverlay::Advance(base::TimeDelta dt) {
  const float s = static_cast<float>(dt.InSecondsF());
  if (s <= 0) {
    return;
  }
  scale_ += (scale_target_ - scale_) * std::min(1.0f, 8.0f * s);

  // The first moment home is a real place, stand there. Not walk there.
  if (!placed_ && HomeIsKnown()) {
    placed_ = true;
    // A jump, not a walk: the drawing made before this stays on the layer
    // unless the whole thing is redrawn once.
    SchedulePaint();
    feet_ = HomeFeet();
    walk_target_ = feet_;
    velocity_ = gfx::Vector2dF();
  }

  if (held_) {
    // Hanging from the cursor. The body follows a hair behind it, which is what
    // makes it swing when the hand moves, and the sway is how hard it is being
    // dragged sideways.
    gfx::PointF want = FeetForTip(pointer_);
    if (lens()) {
      // Held by a point that can be anywhere on the body, so its feet could be
      // asked to go below the window: keep the whole body on the window.
      want.set_x(std::clamp(want.x(), 30.0f,
                            std::max(30.0f, static_cast<float>(width()) - 30.0f)));
      want.set_y(std::clamp(want.y(), BodyHeight() * scale_ + 4.0f,
                            std::max(BodyHeight() * scale_ + 4.0f,
                                     static_cast<float>(height()) - 2.0f)));
    }
    gfx::Vector2dF step = want - feet_;
    step.Scale(std::min(1.0f, 22.0f * s));
    gfx::Vector2dF instant = step;
    instant.Scale(1.0f / std::max(s, 0.001f));
    gfx::Vector2dF change = instant - velocity_;
    change.Scale(std::min(1.0f, 10.0f * s));
    velocity_ += change;
    feet_ = feet_ + step;
    rig_.SetSway(std::clamp(-velocity_.x() / 500.0f, -1.0f, 1.0f));
    rig_.StopLooking();
  } else if (driven_) {
    // The fist is the pointer. Which way it faces is chosen so the body is
    // never off the edge of the window: reaching for something near the left
    // edge, it turns around and reaches with its other arm.
    const float reach = lens()
                            ? (BodyWidth() / 2 + kLensReachGap + 20.0f) * scale_
                            : 145.0f * scale_;
    const bool fits_left_of = pointer_.x() - reach >= 0;
    const bool fits_right_of = pointer_.x() + reach <= width();
    if (rig_.facing_left() && !fits_right_of && fits_left_of) {
      rig_.SetFacingLeft(false);
    } else if (!rig_.facing_left() && !fits_left_of && fits_right_of) {
      rig_.SetFacingLeft(true);
    }
    const gfx::PointF want = FeetForTip(pointer_);
    gfx::Vector2dF instant = want - feet_;
    instant.Scale(1.0f / std::max(s, 0.001f));
    gfx::Vector2dF change = instant - velocity_;
    change.Scale(std::min(1.0f, 18.0f * s));
    velocity_ += change;
    feet_ = want;
    rig_.LookAt(rig_.facing_left() ? -0.6f : 0.6f, 0.0f);
  } else {
    // At rest at home, it goes where home goes: the card moves when the panel
    // opens or the window resizes, and the character walks to keep up.
    if (at_home_) {
      walk_target_ = HomeFeet();
    }
    const gfx::Vector2dF to_go = walk_target_ - feet_;
    const float distance = to_go.Length();
    gfx::Vector2dF desired;
    if (distance > 0.75f) {
      const float speed = std::min(kMaxWalkSpeed, distance * 5.0f);
      desired = gfx::Vector2dF(to_go.x() / distance * speed,
                               to_go.y() / distance * speed);
    }
    gfx::Vector2dF change = desired - velocity_;
    change.Scale(std::min(1.0f, 9.0f * s));
    velocity_ += change;
    feet_ = feet_ + gfx::Vector2dF(velocity_.x() * s, velocity_.y() * s);
    if (distance <= 0.75f && velocity_.Length() < 6.0f) {
      feet_ = walk_target_;
      velocity_ = gfx::Vector2dF();
    }
    if (std::abs(velocity_.x()) > 25.0f) {
      rig_.SetFacingLeft(velocity_.x() < 0);
    }
    LookAtCursor();
  }

  // The legs walk at the speed the body moves, capped so a fast reach across
  // the window is a stride and not a blur.
  const float art_speed =
      std::min(velocity_.Length() / std::max(scale_, 0.1f), 700.0f);
  rig_.SetTravel(art_speed);
  if (was_travelling_ && art_speed < 20.0f) {
    rig_.Kick(MascotKick::kLand);
  }
  was_travelling_ = art_speed > 60.0f;

  rig_.Update(dt);
  if (lens_model_) {
    AdvanceLens(dt);
  }
}

void ZephyrusAgentMascotOverlay::LookAtCursor() {
  // Its eyes follow the person's own mouse whenever nothing else has its
  // attention: a small thing, and most of what makes a still character feel
  // like it is aware of you.
  const MascotMood mood = rig_.mood();
  const bool attentive = mood == MascotMood::kIdle ||
                         mood == MascotMood::kThinking ||
                         mood == MascotMood::kAsking ||
                         mood == MascotMood::kHappy;
  display::Screen* screen = display::Screen::Get();
  if (!attentive || !screen) {
    rig_.StopLooking();
    if (lens_model_) {
      lens_model_->ClearCursor();
    }
    return;
  }
  gfx::Point cursor = screen->GetCursorScreenPoint();
  views::View::ConvertPointFromScreen(this, &cursor);
  if (!GetLocalBounds().Contains(cursor)) {
    rig_.StopLooking();
    if (lens_model_) {
      lens_model_->ClearCursor();
    }
    return;
  }
  if (lens_model_) {
    // The eyes follow the person's pointer, relative to the middle of the body.
    const gfx::PointF centre(feet_.x(),
                             feet_.y() - BodyHeight() * scale_ / 2);
    lens_model_->SetFollowCursor(true);
    lens_model_->SetCursor(
        std::clamp((cursor.x() - centre.x()) / 260.0f, -1.0f, 1.0f),
        std::clamp((cursor.y() - centre.y()) / 260.0f, -1.0f, 1.0f));
    return;
  }
  const float eye_y = feet_.y() - 66.0f * scale_;
  float dx = std::clamp((cursor.x() - feet_.x()) / 260.0f, -1.0f, 1.0f);
  const float dy = std::clamp((cursor.y() - eye_y) / 260.0f, -1.0f, 1.0f);
  // The art is mirrored when it faces left, and so is what it looks at.
  rig_.LookAt(rig_.facing_left() ? -dx : dx, dy);
}

// ---- Being picked up --------------------------------------------------------

bool ZephyrusAgentMascotOverlay::CanGrab() const {
  // Not while the agent has its fist on the page: its hand is the pointer.
  return present_ && has_position_ && !driven_ && GetWidget();
}

gfx::Rect ZephyrusAgentMascotOverlay::GrabArea() const {
  gfx::Rect area = BodyBounds();
  area.Outset(4);
  return area;
}

bool ZephyrusAgentMascotOverlay::GrabPressed(gfx::Point at) {
  if (!CanGrab() || !GrabArea().Contains(at)) {
    return false;
  }
  BeginHold(gfx::PointF(at));
  return true;
}

void ZephyrusAgentMascotOverlay::GrabDragged(gfx::Point at) {
  if (!held_) {
    return;
  }
  // Kept inside the window: a mascot dragged off the edge would be lost.
  pointer_ = gfx::PointF(
      std::clamp(static_cast<float>(at.x()), 20.0f,
                 std::max(20.0f, static_cast<float>(width()) - 20.0f)),
      std::clamp(static_cast<float>(at.y()), 8.0f,
                 std::max(8.0f, static_cast<float>(height()) - 8.0f)));
  if (!gfx::Animation::ShouldRenderRichAnimation()) {
    feet_ = FeetForTip(pointer_);
    SchedulePaint();
  }
  UpdateTimer();
}

void ZephyrusAgentMascotOverlay::GrabReleased() {
  if (held_) {
    EndHold();
  }
}

void ZephyrusAgentMascotOverlay::MouseWatcher::OnMouseEvent(
    ui::MouseEvent* event) {
  overlay_->HandleMouse(event);
}

void ZephyrusAgentMascotOverlay::HandleMouse(ui::MouseEvent* event) {
  if (event->handled()) {
    return;
  }
  display::Screen* screen = display::Screen::Get();
  if (!screen) {
    return;
  }
  gfx::Point at = screen->GetCursorScreenPoint();
  views::View::ConvertPointFromScreen(this, &at);
  switch (event->type()) {
    case ui::EventType::kMousePressed:
      if (event->IsOnlyLeftMouseButton() && GrabPressed(at)) {
        event->SetHandled();
        event->StopPropagation();
      }
      break;
    case ui::EventType::kMouseDragged:
    case ui::EventType::kMouseMoved:
      if (held_) {
        GrabDragged(at);
        event->SetHandled();
        event->StopPropagation();
      }
      break;
    case ui::EventType::kMouseReleased:
      if (held_) {
        GrabReleased();
        event->SetHandled();
        event->StopPropagation();
      }
      break;
    case ui::EventType::kMouseCaptureChanged:
      // Alt-tab, a dialog, the window closing: let go wherever it is.
      if (held_) {
        GrabReleased();
      }
      break;
    default:
      break;
  }
}

void ZephyrusAgentMascotOverlay::BeginHold(gfx::PointF at) {
  // The lens is taken by whatever part of the body was pressed, and stays where
  // it is under the hand; Zep hangs from the cursor.
  grab_offset_ = lens() ? feet_ - at : gfx::Vector2dF();
  held_ = true;
  driven_ = false;
  at_home_ = false;
  pointer_ = at;
  velocity_ = gfx::Vector2dF();
  rig_.SetSway(0);
  rig_.SetMood(MascotMood::kHeld);
  mood_since_ = base::TimeTicks::Now();
  scale_target_ = kHomeScale;
  last_tick_ = base::TimeTicks::Now();
  UpdateTimer();
  SchedulePaint();
}

void ZephyrusAgentMascotOverlay::EndHold() {
  held_ = false;
  // Put down inside the window, on its feet: the head has to fit above.
  const float head = BodyHeight() * scale_target_;
  const float max_x = std::max(30.0f, static_cast<float>(width()) - 30.0f);
  const float max_y =
      std::max(head + 4.0f, static_cast<float>(height()) - 2.0f);
  feet_ = gfx::PointF(std::clamp(feet_.x(), 30.0f, max_x),
                      std::clamp(feet_.y(), head + 4.0f, max_y));
  // Here is home now, in every window size.
  if (width() > 0 && height() > 0) {
    parked_ = gfx::PointF(feet_.x() / width(), feet_.y() / height());
  }
  at_home_ = true;
  driven_ = false;
  walk_target_ = feet_;
  velocity_ = gfx::Vector2dF();
  rig_.SetSway(0);
  rig_.SetMood(MascotMood::kRelieved);
  mood_since_ = base::TimeTicks::Now();
  UpdateTimer();
  SchedulePaint();
  PlaceBubble();
}

void ZephyrusAgentMascotOverlay::StartDragDemoForTesting() {
  demo_start_ = base::TimeTicks::Now();
  demo_phase_ = 0;
  demo_timer_.Start(
      FROM_HERE, base::Milliseconds(16),
      base::BindRepeating(
          [](ZephyrusAgentMascotOverlay* self) {
            views::Widget* widget = self->GetWidget();
            if (!widget || !self->present_) {
              self->demo_timer_.Stop();
              return;
            }
            const double t = (base::TimeTicks::Now() - self->demo_start_).InSecondsF();
            if (self->demo_phase_ == 0 && t > 6.0) {
              self->demo_phase_ = 1;
              self->GrabPressed(self->BodyBounds().CenterPoint());
            } else if (self->demo_phase_ == 1) {
              const double u = t - 6.0;
              if (u < 3.0) {
                // A wide sweep left and right, then up.
                gfx::Point p(
                    static_cast<int>(self->width() * (0.55 + 0.25 * std::sin(u * 3.2))),
                    static_cast<int>(self->height() * (0.6 - 0.09 * u)));
                // Straight to the overlay: the widget only routes a drag
                // while the OS holds the mouse, which a synthetic press
                // does not get.
                self->GrabDragged(p);
              } else {
                self->demo_phase_ = 2;
                self->GrabReleased();
                self->demo_timer_.Stop();
              }
            }
          },
          base::Unretained(this)));
}

void ZephyrusAgentMascotOverlay::StartClickDemoForTesting() {
  demo_start_ = base::TimeTicks::Now();
  demo_phase_ = 0;
  demo_timer_.Start(
      FROM_HERE, base::Milliseconds(16),
      base::BindRepeating(
          [](ZephyrusAgentMascotOverlay* self) {
            if (!self->GetWidget() || !self->present_) {
              self->demo_timer_.Stop();
              return;
            }
            const double t = (base::TimeTicks::Now() - self->demo_start_).InSecondsF();
            gfx::Point target(self->width() / 2, self->height() / 2);
            views::View::ConvertPointToScreen(self, &target);
            using Kind = PointerEvent::Kind;
            const auto send = [&](Kind kind) {
              PointerEvent event;
              event.kind = kind;
              event.position = target;
              self->OnPointer(event);
            };
            if (t > 4.0 && t < 6.5) {
              send(Kind::kMove);
            } else if (self->demo_phase_ == 0 && t >= 6.5) {
              self->demo_phase_ = 1;
              send(Kind::kArrive);
            } else if (self->demo_phase_ == 1 && t >= 7.0) {
              self->demo_phase_ = 2;
              send(Kind::kPress);
            } else if (self->demo_phase_ == 2 && t >= 7.15) {
              self->demo_phase_ = 3;
              send(Kind::kRelease);
            } else if (self->demo_phase_ == 3 && t >= 9.0) {
              self->demo_phase_ = 4;
              send(Kind::kIdle);
              self->demo_timer_.Stop();
            }
          },
          base::Unretained(this)));
}

bool ZephyrusAgentMascotOverlay::MoveTo(MascotSpot spot) {
  if (!present_ || held_ || width() <= 0 || height() <= 0) {
    return false;
  }
  // The page is where it gets in the way, so the page is the room it moves in.
  gfx::Rect area = GetLocalBounds();
  if (views::View* card =
          browser_view_ ? browser_view_->contents_container() : nullptr) {
    if (card->IsDrawn() && card->width() > 120) {
      gfx::Rect page = card->GetLocalBounds();
      gfx::Point origin = page.origin();
      views::View::ConvertPointToTarget(card, this, &origin);
      page.set_origin(origin);
      area = page;
    }
  }
  const float side = 52.0f;
  const float head = BodyHeight() * kHomeScale + 12.0f;
  const float left = area.x() + side;
  const float right = area.right() - side;
  const float top = area.y() + head;
  const float bottom = area.bottom() - 6.0f;
  const float mid_x = static_cast<float>(area.CenterPoint().x());
  const float mid_y = static_cast<float>(area.CenterPoint().y()) + 30.0f;

  gfx::PointF target = feet_;
  switch (spot) {
    case MascotSpot::kHome:
      parked_.reset();
      target = HomeFeet();
      break;
    case MascotSpot::kLeft:
      target = gfx::PointF(left, mid_y);
      break;
    case MascotSpot::kRight:
      target = gfx::PointF(right, mid_y);
      break;
    case MascotSpot::kTop:
      target = gfx::PointF(mid_x, top);
      break;
    case MascotSpot::kBottom:
      target = gfx::PointF(mid_x, bottom);
      break;
    case MascotSpot::kTopLeft:
      target = gfx::PointF(left, top);
      break;
    case MascotSpot::kTopRight:
      target = gfx::PointF(right, top);
      break;
    case MascotSpot::kBottomLeft:
      target = gfx::PointF(left, bottom);
      break;
    case MascotSpot::kBottomRight:
      target = gfx::PointF(right, bottom);
      break;
    case MascotSpot::kCenter:
      target = gfx::PointF(mid_x, mid_y);
      break;
    case MascotSpot::kAway: {
      // The corner farthest from where it stands now: wherever it stands is
      // what it is covering.
      const gfx::PointF corners[] = {gfx::PointF(left, top),
                                     gfx::PointF(right, top),
                                     gfx::PointF(left, bottom),
                                     gfx::PointF(right, bottom)};
      float best = -1.0f;
      for (const gfx::PointF& corner : corners) {
        const float d = (corner - feet_).Length();
        if (d > best) {
          best = d;
          target = corner;
        }
      }
      break;
    }
  }
  if (spot != MascotSpot::kHome) {
    parked_ = gfx::PointF(target.x() / width(), target.y() / height());
  }
  driven_ = false;
  at_home_ = true;
  scale_target_ = kHomeScale;
  walk_target_ = HomeFeet();
  if (!gfx::Animation::ShouldRenderRichAnimation()) {
    feet_ = walk_target_;
    scale_ = scale_target_;
    SchedulePaint();
  }
  UpdateTimer();
  return true;
}

// ---- Drawing ----------------------------------------------------------------

void ZephyrusAgentMascotOverlay::PaintListeningDot(gfx::Canvas* canvas,
                                                   float dsf) const {
  // A dot at the top-right of the body: solid, not pulsing, so it costs no
  // timer and cannot be mistaken for an animation that has stalled. `canvas`
  // is already in physical pixels.
  const gfx::Rect body = BodyBounds();
  const float radius = 4.0f * dsf;
  const float cx = std::round((body.right() - 2) * dsf);
  const float cy = std::round((body.y() + 4) * dsf);
  cc::PaintFlags ring;
  ring.setAntiAlias(true);
  ring.setColor(SK_ColorWHITE);
  canvas->DrawCircle(gfx::PointF(cx, cy), radius + 1.5f * dsf, ring);
  cc::PaintFlags dot;
  dot.setAntiAlias(true);
  dot.setColor(SkColorSetRGB(0xE5, 0x39, 0x35));
  canvas->DrawCircle(gfx::PointF(cx, cy), radius, dot);
}

void ZephyrusAgentMascotOverlay::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  if (!present_ || !has_position_) {
    // The mascot is switched off, but an open microphone still has to be
    // visible: the light stands where the mascot would.
    if (listening_) {
      gfx::ScopedCanvas scoped(canvas);
      const float dsf = canvas->UndoDeviceScaleFactor();
      PaintListeningDot(canvas, dsf);
    }
    return;
  }
  if (lens()) {
    gfx::ScopedCanvas scoped_lens(canvas);
    const float lens_dsf = canvas->UndoDeviceScaleFactor();
    PaintLensBody(canvas, lens_dsf);
    if (listening_) {
      PaintListeningDot(canvas, lens_dsf);
    }
    painted_ = DirtyRect();
    return;
  }
  SnapRestingBreathToPixels();
  const std::vector<zephyrus_setup::Rect> rects = rig_.Draw();

  // Painted in PHYSICAL pixels with every edge rounded to one, and the body's
  // origin snapped to a pixel too. Pixel art at a fractional position or scale
  // otherwise shimmers as it moves and grows hairline seams between its blocks,
  // which is the one way this character can look wrong.
  gfx::ScopedCanvas scoped(canvas);
  const float dsf = canvas->UndoDeviceScaleFactor();
  const float scale = scale_ * dsf;
  const float ox = std::round((feet_.x() - kArtFeetX * scale_) * dsf);
  const float oy = std::round((feet_.y() - kArtFeetY * scale_) * dsf);

  for (const zephyrus_setup::Rect& r : rects) {
    const int left = std::lround(ox + r.x * scale);
    const int top = std::lround(oy + r.y * scale);
    const int right = std::lround(ox + (r.x + r.w) * scale);
    const int bottom = std::lround(oy + (r.y + r.h) * scale);
    if (right <= left || bottom <= top) {
      continue;
    }
    canvas->FillRect(gfx::Rect(left, top, right - left, bottom - top),
                     SkColorSetRGB((r.color >> 16) & 0xFF,
                                   (r.color >> 8) & 0xFF, r.color & 0xFF));
  }

  if (listening_) {
    PaintListeningDot(canvas, dsf);
  }
  painted_ = DirtyRect();
}

void ZephyrusAgentMascotOverlay::AddedToWidget() {
  if (!watched_window_ && GetWidget() && GetWidget()->GetNativeWindow()) {
    watched_window_ = GetWidget()->GetNativeWindow();
    watched_window_->AddPreTargetHandler(&mouse_watcher_);
  }
  UpdateTimer();
}

void ZephyrusAgentMascotOverlay::RemovedFromWidget() {
  if (watched_window_) {
    watched_window_->RemovePreTargetHandler(&mouse_watcher_);
    watched_window_ = nullptr;
  }
  held_ = false;
  timer_.Stop();
}

// ---- The lens look ----------------------------------------------------------

float ZephyrusAgentMascotOverlay::BodyWidth() const {
  return lens() ? kLensBodyWidthPerScale : 110.0f;
}

float ZephyrusAgentMascotOverlay::BodyHeight() const {
  return lens() ? kLensBodyHeightPerScale : 135.0f;
}

void ZephyrusAgentMascotOverlay::RefreshLook() {
  const MascotLook want =
      browser_view_ && browser_view_->browser()
          ? zephyrus::agent::ReadMascotLook(
                *browser_view_->browser()->profile()->GetPrefs())
          : MascotLook::kZep;
  const bool was_lens = lens();
  const bool fill_changed = want != look_;
  look_ = want;
  if (lens()) {
    if (!lens_model_) {
      lens_rng_ = std::make_unique<SystemLensRandom>();
      lens_model_ = std::make_unique<LensModel>(lens_rng_.get());
      lens_model_->SetState(LensStateForMood(rig_.mood(), 10.0), /*force=*/true);
    }
    lens_model_->SetFill(want == MascotLook::kLensFluid ? LensFill::kFluid
                                                        : LensFill::kStatic);
    lens_model_->SetMotionScale(gfx::Animation::ShouldRenderRichAnimation() ? 1.0
                                                                            : 0.35);
    if (want != MascotLook::kLensFluid) {
      lens_quality_.Reset();
    }
  } else {
    lens_model_.reset();
    lens_rng_.reset();
    lens_quality_.Reset();
    press_ring_.reset();
  }
  if (fill_changed || was_lens != lens()) {
    // A different character is a different size and shape: forget where the old
    // one was drawn, and draw the whole thing once.
    last_frame_.clear();
    painted_ = gfx::Rect();
    last_dirty_ = gfx::Rect();
    SchedulePaint();
    UpdateTimer();
    PlaceBubble();
  }
}

void ZephyrusAgentMascotOverlay::AdvanceLens(base::TimeDelta dt) {
  const double seconds = dt.InSecondsF();
  const double in_mood = (base::TimeTicks::Now() - mood_since_).InSecondsF();
  // Zep's mood is the one the rest of the browser sets; the lens shows it.
  lens_model_->SetState(LensStateForMood(rig_.mood(), in_mood));

  // Reaching for the page: look at where the agent is acting.
  if (driven_) {
    const gfx::PointF centre(feet_.x(), feet_.y() - BodyHeight() * scale_ / 2);
    lens_model_->Look(
        std::clamp((pointer_.x() - centre.x()) / 160.0f, -1.0f, 1.0f),
        std::clamp((pointer_.y() - centre.y()) / 160.0f, -1.0f, 1.0f));
  }
  // Walking leans the body into the direction of travel.
  if (std::abs(velocity_.x()) > 25.0f) {
    lens_model_->SetManualSlant(
        std::clamp(static_cast<double>(velocity_.x()) / 700.0, -0.8, 0.8));
  } else {
    lens_model_->SetManualSlant(std::nullopt);
  }
  lens_model_->Update(seconds);
  lens_frame_ = lens_model_->ComputeFrame();
}

void ZephyrusAgentMascotOverlay::PaintLensBody(gfx::Canvas* canvas, float dsf) {
  if (!lens_model_) {
    return;
  }
  // The window is a square standing on its feet point: centred on x, with the
  // body's lower edge at the baseline.
  const float side = kLensWindowPerScale * scale_ * dsf;
  const float top = std::round(feet_.y() * dsf - kLensBaselineInWindow * side);
  LensPaintOptions options;
  options.lite = lens_quality_.lite();
  const float width = side * static_cast<float>(kLensViewWidth / kLensViewHeight);
  const int w = std::max(1, static_cast<int>(std::lround(width)));
  const int h = std::max(1, static_cast<int>(std::lround(side)));
  // Painted into a small bitmap of its own and then drawn: the browser's canvas
  // is a recording, and the lens (blurs, blend modes) is drawn straight with Skia.
  const SkBitmap bitmap = RenderLensToBitmap(lens_frame_, w, h, options);
  canvas->DrawImageInt(gfx::ImageSkia::CreateFrom1xBitmap(bitmap),
                       static_cast<int>(std::lround(feet_.x() * dsf - w / 2.0f)),
                       static_cast<int>(std::lround(top)));
  PaintPressRing(canvas, dsf);
}

void ZephyrusAgentMascotOverlay::PaintPressRing(gfx::Canvas* canvas, float dsf) const {
  if (!press_ring_) {
    return;
  }
  const double age = (base::TimeTicks::Now() - press_ring_->since).InSecondsF() /
                     kPressRingFor.InSecondsF();
  if (age < 0 || age > 1) {
    return;
  }
  const float p = static_cast<float>(1.0 - std::pow(1.0 - age, 3.0));  // ease out
  // A white ring over a dark one: white alone vanishes on a light page, which is
  // most pages, and dark alone vanishes on a dark one.
  const gfx::PointF centre(press_ring_->at.x() * dsf, press_ring_->at.y() * dsf);
  const float radius = (6.0f + 30.0f * p) * dsf;
  const float fade = static_cast<float>(std::pow(1.0 - age, 1.5));
  for (const bool dark : {true, false}) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth((3.0f - 2.0f * p + (dark ? 2.0f : 0.0f)) * dsf);
    flags.setColor(dark ? SkColorSetRGB(20, 20, 30) : SK_ColorWHITE);
    flags.setAlphaf((dark ? 0.55f : 0.95f) * fade);
    canvas->DrawCircle(centre, radius, flags);
  }
}

BEGIN_METADATA(ZephyrusAgentMascotOverlay)
END_METADATA


}  // namespace zephyrus::agent
