// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_MASCOT_OVERLAY_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_MASCOT_OVERLAY_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "chrome/browser/zephyrus/agent/mascot_commands.h"
#include "chrome/browser/zephyrus/agent/mascot_rig.h"
#include "chrome/browser/zephyrus/agent/pointer_events.h"
#include "ui/base/cursor/cursor.h"
#include "ui/events/event_handler.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/vector2d_f.h"
#include "ui/views/view.h"

class BrowserView;

namespace aura {
class Window;
}

namespace zephyrus::agent {

class ZephyrusMascotBubble;

// The agent's mascot, over the whole browser window.
//
// It is the agent's pointer. When the agent clicks, scrolls or types on a page,
// the mascot's outstretched hand is where the pointer is: it walks across the
// window to the target on the same curved, eased path the real mouse events
// follow (see PointerPath), and presses when the page receives the press. When
// the agent is doing something that is not on the page it goes to where that
// happens -- to the address bar to navigate, along the tab strip for tabs --
// and when a task ends, or the agent needs an answer, it walks home to the agent
// panel.
//
// **It watches; it does not act.** The pointer events come from the browser's
// own tool surface, which performs every action through the ordinary input
// pipeline whether or not anything is drawn. Nothing here can move the real
// pointer, click, or delay a click, and the mascot walking to the address bar
// does not press anything there: the agent's reach stops at the page.
//
// A transparent layer that passes every mouse event through to what is under
// it, so it never gets in the way of a person using the page.
class ZephyrusAgentMascotOverlay : public views::View, public PointerObserver {
  METADATA_HEADER(ZephyrusAgentMascotOverlay, views::View)

 public:
  // Where it can go besides the page.
  enum class Place {
    kStay,
    kHome,        // the panel's mascot slot
    kAddressBar,  // on the toolbar
    kTabStrip,    // along the tabs
  };

  explicit ZephyrusAgentMascotOverlay(BrowserView* browser_view);
  ZephyrusAgentMascotOverlay(const ZephyrusAgentMascotOverlay&) = delete;
  ZephyrusAgentMascotOverlay& operator=(const ZephyrusAgentMascotOverlay&) =
      delete;
  ~ZephyrusAgentMascotOverlay() override;

  // Whether the mascot is in the window at all. It lives in every window that
  // has an agent, and the person can turn it off (Model settings). Hidden, it
  // draws nothing and runs no timer.
  void SetPresent(bool present);
  bool present() const { return present_; }
  // Follows the "Show the mascot" setting.
  void RefreshPresence();

  void SetMood(MascotMood mood);
  MascotMood mood() const { return rig_.mood(); }
  // Goes to `place` -- but only if it can be there in `budget`, the time the
  // thing it is going to do will take. A task runs at the speed of the browser,
  // not of a walking character: the address bar can be a second's walk from
  // where it stands and the navigation it is going there for finishes in a
  // fraction of that. Arriving after the fact looks wrong and helps nobody, so
  // when it cannot make it, it stays where it is, turns toward the place, and
  // does its part from there. Returns whether it set off. Home is never
  // refused: coming back is not on anyone's clock.
  bool GoTo(Place place,
            base::TimeDelta budget = base::TimeDelta::Max());
  // A greeting, and a stretch out of sleep.
  void Wake();

  // The microphone is open for "Hey Zep": a small light on the mascot, so
  // nobody talks near a microphone they did not know was on.
  void SetListening(bool listening);
  bool listening() const { return listening_; }

  // The bubble that follows it around (see ZephyrusMascotBubble). Not owned.
  void SetBubble(ZephyrusMascotBubble* bubble) { bubble_ = bubble; }
  // Puts the bubble where it belongs now. Called by the tick as it moves, and
  // by whoever changes what the bubble says.
  void PlaceBubble();
  // The character's body, in this view's coordinates. It stands at home when it
  // is not shown, so a question still has somewhere to be asked from.
  gfx::Rect BodyBounds() const;

  // Moves it to a place the person asked for ("move to the left", "get out of
  // the way"). It walks there and stays: that becomes its home until it is sent
  // back (MascotSpot::kHome). Returns false when it is not in the window.
  bool MoveTo(MascotSpot spot);

  // The person has it by the cursor.
  bool held() const { return held_; }

  // The mouse, as the grab handle (below) hands it over. Positions are in this
  // view's coordinates.
  bool GrabPressed(gfx::Point at);
  void GrabDragged(gfx::Point at);
  void GrabReleased();


  // Developer hook (--zephyrus-test-compact=mascot-drag): picks it up through
  // the window's real mouse path, drags it round for a few seconds and puts it
  // down, so the hold and the relief can be looked at.
  void StartDragDemoForTesting();

  // Covers the whole parent and stays on top of it. Called from the parent's
  // layout, so it keeps up with resizes and with views added after it.
  void FitToParent();

  // Where the mascot's hand is now, in the overlay's coordinates. Test-facing.
  gfx::PointF TipForTesting() const { return Tip(); }
  gfx::PointF FeetForTesting() const { return feet_; }
  bool driven_for_testing() const { return driven_; }
  bool IsTickingForTesting() const { return timer_.IsRunning(); }
  void TickForTesting(base::TimeDelta dt) { Advance(dt); }

  // PointerObserver:
  void OnPointer(const PointerEvent& event) override;
  std::optional<gfx::Point> PointerHome() override;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;

 private:
  void UpdateTimer();
  void Tick();
  void Advance(base::TimeDelta dt);

  // What a frame may have changed: the character and the reach of its arm.
  gfx::Rect DirtyRect() const;
  // Where it was last painted, so that a move also erases the old place.
  gfx::Rect last_dirty_;
  // Where the last paint really drew, whoever asked for it.
  gfx::Rect painted_;
  // The frame last painted, so an identical one is not painted again.
  std::vector<zephyrus_setup::Rect> last_frame_;
  gfx::PointF last_feet_;
  float last_scale_ = 0.0f;
  static bool SameFrame(const std::vector<zephyrus_setup::Rect>& a,
                        const std::vector<zephyrus_setup::Rect>& b);
  // The microphone light. `canvas` is in physical pixels.
  void PaintListeningDot(gfx::Canvas* canvas, float dsf) const;

  // The body's rest position for a place, in this view's coordinates.
  gfx::PointF FeetFor(Place place) const;
  // Home is the bottom right of the page card, standing on its lower edge.
  gfx::PointF HomeFeet() const;
  bool HomeIsKnown() const;
  // Eyes follow the person's own mouse, when nothing else has its attention.
  void LookAtCursor();
  // Where the fist is, given where the feet are.
  gfx::PointF Tip() const;
  // Where the feet must be for the fist to be at `tip`.
  gfx::PointF FeetForTip(gfx::PointF tip) const;

  // Picking it up: the fists lock onto the cursor and the body hangs from them;
  // letting go is a sigh of relief, and where it was put becomes home.
  bool CanGrab() const;
  gfx::Rect GrabArea() const;
  // Watches the window's mouse events BEFORE the page sees them, so the
  // character can be picked up even where it stands over the web contents --
  // which is a native window of its own and never gets its events from a view.
  // A press on the character is taken; every other event is left alone.
  class MouseWatcher : public ui::EventHandler {
   public:
    explicit MouseWatcher(ZephyrusAgentMascotOverlay* overlay)
        : overlay_(overlay) {}
    void OnMouseEvent(ui::MouseEvent* event) override;

   private:
    const raw_ptr<ZephyrusAgentMascotOverlay> overlay_;
  };
  void HandleMouse(ui::MouseEvent* event);
  MouseWatcher mouse_watcher_{this};
  raw_ptr<aura::Window> watched_window_ = nullptr;
  void BeginHold(gfx::PointF at);
  void EndHold();
  // Where the person last put it, as a fraction of the window, so that it stays
  // in the same place when the window is resized. Empty: the default home.
  std::optional<gfx::PointF> parked_;
  bool held_ = false;
  base::RepeatingTimer demo_timer_;
  base::TimeTicks demo_start_;
  int demo_phase_ = 0;

  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<ZephyrusMascotBubble> bubble_ = nullptr;
  PrefChangeRegistrar pref_registrar_;
  bool observing_prefs_ = false;
  // At rest, and so following home as the card moves under it.
  bool at_home_ = true;
  // Set once home is a real place: before the window has been laid out it is
  // only a guess, and the character stood there would then walk across the
  // page to the real one.
  bool placed_ = false;

  MascotRig rig_;
  bool present_ = false;
  bool listening_ = false;
  base::RepeatingTimer timer_;
  base::TimeTicks last_tick_;
  base::TimeTicks mood_since_;

  // Body position (the middle of the feet), in this view's coordinates, and its
  // velocity in dip a second.
  gfx::PointF feet_;
  gfx::Vector2dF velocity_;
  gfx::PointF walk_target_;
  bool has_position_ = false;
  bool was_travelling_ = false;

  // The pointer is being driven by the agent: the fist follows it exactly.
  bool driven_ = false;
  gfx::PointF pointer_;

  // dip per unit of the art, easing toward `scale_target_`.
  float scale_ = 0.42f;
  float scale_target_ = 0.42f;
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_MASCOT_OVERLAY_H_
