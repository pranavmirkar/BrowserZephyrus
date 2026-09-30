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
#include "chrome/browser/zephyrus/agent/model_settings.h"
#include "chrome/browser/zephyrus/agent/pointer_path.h"
#include "components/prefs/pref_service.h"
#include "ui/display/screen.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view_class_properties.h"
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

constexpr base::TimeDelta kFallAsleepAfter = base::Seconds(60);
constexpr base::TimeDelta kOutcomeShownFor = base::Seconds(9);

bool IsOutcome(MascotMood mood) {
  return mood == MascotMood::kHappy || mood == MascotMood::kConfused ||
         mood == MascotMood::kAlert;
}

}  // namespace

ZephyrusAgentMascotOverlay::ZephyrusAgentMascotOverlay(BrowserView* browser_view)
    : browser_view_(browser_view) {
  // It must never catch a click or take focus: it is a drawing over the page,
  // and the page is what the person is using.
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
  }
  const bool enabled =
      !browser_view_ || !browser_view_->browser() ||
      browser_view_->browser()->profile()->GetPrefs()->GetBoolean(
          zephyrus::agent::kMascotPref);
  SetPresent(enabled);
}

ZephyrusAgentMascotOverlay::~ZephyrusAgentMascotOverlay() = default;

void ZephyrusAgentMascotOverlay::FitToParent() {
  if (!parent()) {
    return;
  }
  SetBoundsRect(parent()->GetLocalBounds());
  // On top of everything the window has, including views added after this one,
  // with the bubble above it: the order is [... overlay, bubble]. Reordered only
  // when it is wrong -- a reorder that ran on every layout would be a layout.
  const auto& kids = parent()->children();
  views::View* top = bubble_ ? static_cast<views::View*>(bubble_) : this;
  const bool in_order =
      kids.back() == top &&
      (!bubble_ || (kids.size() >= 2 && kids[kids.size() - 2] == this));
  if (!in_order) {
    parent()->ReorderChildView(this, -1);
    if (bubble_) {
      parent()->ReorderChildView(bubble_, -1);
    }
  }
  PlaceBubble();
}

gfx::Rect ZephyrusAgentMascotOverlay::BodyBounds() const {
  const gfx::PointF feet = has_position_ ? feet_ : HomeFeet();
  const float scale = has_position_ ? scale_ : kHomeScale;
  const int w = std::max(1, static_cast<int>(std::lround(110.0f * scale)));
  const int h = std::max(1, static_cast<int>(std::lround(135.0f * scale)));
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
    timer_.Stop();
  }
  UpdateTimer();
}

void ZephyrusAgentMascotOverlay::SetMood(MascotMood mood) {
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
  if (!present_ || place == Place::kStay) {
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
  const gfx::PointF tip = rig_.PointerTip();
  return gfx::PointF(feet_.x() + (tip.x() - kArtFeetX) * scale_,
                     feet_.y() + (tip.y() - kArtFeetY) * scale_);
}

gfx::PointF ZephyrusAgentMascotOverlay::FeetForTip(gfx::PointF tip) const {
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
  const bool moving = driven_ || velocity_.Length() > 1.0f ||
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
  if (hidden) {
    interval = base::Seconds(1);
  } else if (moving) {
    interval = base::Milliseconds(16);
  } else if (rig_.mood() == MascotMood::kSleeping) {
    interval = base::Milliseconds(250);
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
  std::vector<zephyrus_setup::Rect> frame = rig_.Draw();
  const bool same = feet_ == last_feet_ && scale_ == last_scale_ &&
                    SameFrame(frame, last_frame_);
  if (!same) {
    const gfx::Rect dirty = DirtyRect();
    SchedulePaintInRect(gfx::UnionRects(dirty, last_dirty_));
    last_dirty_ = dirty;
    last_frame_ = std::move(frame);
    last_feet_ = feet_;
    last_scale_ = scale_;
  }
  // The pace depends on whether it is moving, which just changed.
  UpdateTimer();
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
    feet_ = HomeFeet();
    walk_target_ = feet_;
    velocity_ = gfx::Vector2dF();
  }

  if (driven_) {
    // The fist is the pointer. Which way it faces is chosen so the body is
    // never off the edge of the window: reaching for something near the left
    // edge, it turns around and reaches with its other arm.
    const float reach = 145.0f * scale_;
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
    return;
  }
  gfx::Point cursor = screen->GetCursorScreenPoint();
  views::View::ConvertPointFromScreen(this, &cursor);
  if (!GetLocalBounds().Contains(cursor)) {
    rig_.StopLooking();
    return;
  }
  const float eye_y = feet_.y() - 66.0f * scale_;
  float dx = std::clamp((cursor.x() - feet_.x()) / 260.0f, -1.0f, 1.0f);
  const float dy = std::clamp((cursor.y() - eye_y) / 260.0f, -1.0f, 1.0f);
  // The art is mirrored when it faces left, and so is what it looks at.
  rig_.LookAt(rig_.facing_left() ? -dx : dx, dy);
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
}

void ZephyrusAgentMascotOverlay::AddedToWidget() {
  UpdateTimer();
}

void ZephyrusAgentMascotOverlay::RemovedFromWidget() {
  timer_.Stop();
}

BEGIN_METADATA(ZephyrusAgentMascotOverlay)
END_METADATA

}  // namespace zephyrus::agent
