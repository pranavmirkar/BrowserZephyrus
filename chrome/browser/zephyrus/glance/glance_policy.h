// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_GLANCE_GLANCE_POLICY_H_
#define CHROME_BROWSER_ZEPHYRUS_GLANCE_GLANCE_POLICY_H_

#include <string_view>

#include "base/time/time.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

// Glance: look at a link without opening it. This file is the part that can be
// wrong without anything crashing -- which click opens one, which addresses are
// allowed, where the card sits and how it moves -- so it has no views or
// content in it and is tested on its own. See docs/glance-plan.md.
namespace zephyrus::glance {

// Settings. Both live in the profile's prefs and are read when a click arrives,
// so changing them needs no restart.
inline constexpr char kEnabledPref[] = "zephyrus.glance.enabled";
inline constexpr char kTriggerPref[] = "zephyrus.glance.trigger";

// The key held while clicking a link.
enum class Trigger { kAlt, kCtrl, kShift };

// "alt" / "ctrl" / "shift". Anything else (a hand-edited prefs file) is Alt,
// the default, rather than a trigger that silently never fires.
Trigger TriggerFromName(std::string_view name);
std::string_view NameOf(Trigger trigger);

struct Config {
  bool enabled = true;
  Trigger trigger = Trigger::kAlt;
};

struct Modifiers {
  bool alt = false;
  bool ctrl = false;
  bool shift = false;
  bool meta = false;
};

enum class Button { kLeft, kMiddle, kRight, kOther };
enum class MouseType { kDown, kUp, kOther };

// Only web addresses open a glance. A javascript:, data:, file: or chrome:
// address is something the user did not ask to have fetched into a card, and a
// card has no address bar to show them where they are.
bool IsGlanceableUrl(const GURL& url);

// True when exactly the trigger key is held with the left button. "Exactly":
// Alt+Ctrl+click is already split-view in Blink and Ctrl+Shift+click already
// means a foreground tab, so a chord that includes the trigger is not ours.
bool IsGlanceClick(const Config& config, Modifiers modifiers, Button button);

// What to do with one mouse event over a page.
//
// A glance opens when the button is RELEASED, not pressed. The card appears
// under the pointer, and a release that landed on it would count as a click on
// the dimmed page behind and close it again. The press is swallowed too, or the
// page would see half a click: Blink would take the Alt press for a download.
//
// One instance per window. Nothing here knows about tabs or widgets; the owner
// calls Reset() when the page under the pointer changes.
class ClickGate {
 public:
  enum class Action {
    kPass,     // Not ours. The page gets the event.
    kSwallow,  // Ours, and the glance is not ready yet. The page does not.
    kOpen,     // Ours, and the button just came up: open `*url` now.
  };

  ClickGate();
  ~ClickGate();

  Action OnMouse(const Config& config,
                 MouseType type,
                 Button button,
                 Modifiers modifiers,
                 const GURL& hovered_link,
                 GURL* url);

  // Forgets a swallowed press. Without it, a press swallowed just before the
  // tab changed would open a glance for the old tab's link on the next release.
  void Reset();

  bool has_pending() const { return pending_.is_valid(); }

 private:
  GURL pending_;
};

// ---- Geometry -----------------------------------------------------------

// The card's size is a share of the page area, as Zen's is, but never so wide
// that the button column has no room beside it.
inline constexpr double kCardWidthShare = 0.8;
inline constexpr int kCardVerticalMargin = 12;
inline constexpr int kButtonColumnWidth = 48;
inline constexpr int kButtonColumnGap = 10;
inline constexpr int kMinCardWidth = 320;

// Where the card rests, in the coordinates of `area` (the page's rect).
gfx::Rect CardBounds(const gfx::Size& area);

// The column of round buttons beside the card: to its right when there is room,
// and tucked inside its top-right corner when the window is too narrow for that.
gfx::Rect ButtonColumnBounds(const gfx::Rect& card, const gfx::Size& area);

// Where the card grows from. Chromium does not tell the browser the link's
// rectangle, only that the pointer is over one, so this is a link-sized patch
// centred on the click, kept inside `area`.
gfx::Rect OriginRect(const gfx::Point& click, const gfx::Size& area);

// ---- Motion -------------------------------------------------------------

inline constexpr base::TimeDelta kOpenDuration = base::Milliseconds(320);
inline constexpr base::TimeDelta kCloseDuration = base::Milliseconds(220);
// The buttons arrive after the card has landed, not with it.
inline constexpr base::TimeDelta kButtonsDelay = base::Milliseconds(120);
inline constexpr base::TimeDelta kButtonsDuration = base::Milliseconds(200);

// The dimming over the page while a card is open, at full progress.
inline constexpr double kScrimOpacity = 0.5;

// Ease-in-out, 0 to 1. Clamps outside the range.
double Ease(double t);

// The card's rect at progress `t` (0 = `from`, 1 = `to`). The centre travels in
// a shallow arc rather than a straight line, which reads as the card being
// lifted out of the link instead of slid.
gfx::Rect RectAt(double t, const gfx::Rect& from, const gfx::Rect& to);

// Progress of the buttons' fade-in given time since the card began to open.
double ButtonsProgress(base::TimeDelta since_open_began);

}  // namespace zephyrus::glance

#endif  // CHROME_BROWSER_ZEPHYRUS_GLANCE_GLANCE_POLICY_H_
