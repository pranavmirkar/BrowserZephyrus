// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "ui/views/controls/label.h"
#include "ui/views/border.h"
#include "ui/gfx/canvas.h"
#include "cc/paint/paint_flags.h"
#include "ui/native_theme/native_theme.h"

#include <map>

#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace zephyrus {
namespace {

// An anchored bubble closes when it loses activation — and the mouse PRESS on
// its own trigger button is what takes that activation away. The button's
// callback then runs on the RELEASE, by which time the bubble is already gone,
// so it opens a fresh one: the popup appears never to toggle shut.
//
// Views' own answer to this is MenuButtonController's PressedLock, but that
// moves the trigger to fire on press and needs the lock threaded through each
// bubble's lifetime — four separate button classes' worth of surgery for what
// is one shared rule.
//
// Two records are needed because Widget::Close() is ASYNCHRONOUS: whether the
// widget is gone by the time the button's release runs is a race, which is why
// a close-time stamp alone fixed the toggle only intermittently.
//
//   OpenBubbles() — the bubble is still alive, closing or not. The trigger
//                   closes it and swallows the click.
//   LastCloses()  — destruction already finished, so there is nothing left to
//                   find; the stamp is the only evidence the click had a
//                   bubble to dismiss.
//
// Between them the click is covered whichever side of the race it lands on.

// Only has to cover press-to-release of one ordinary click — well under 150ms —
// without swallowing a deliberate second visit.
constexpr base::TimeDelta kToggleWindow = base::Milliseconds(300);

// Both maps are keyed by anchor identity only. The key is never dereferenced,
// so an anchor destroyed while an entry is outstanding is harmless. Stale keys
// are pruned on write rather than tracked, since these only ever hold a handful
// of toolbar buttons.
//
// uintptr_t rather than `const void*` so that "identity token, never a pointer"
// is enforced by the type instead of asserted by this comment. It also keeps
// the anchor out of raw_ptr's scope, which is correct here: raw_ptr exists to
// catch dangling DEREFERENCES, and adopting it would imply a validity guarantee
// these keys deliberately do not have.
using AnchorKey = uintptr_t;

AnchorKey KeyFor(const void* anchor) {
  return reinterpret_cast<AnchorKey>(anchor);
}

std::map<AnchorKey, base::TimeTicks>& LastCloses() {
  static base::NoDestructor<std::map<AnchorKey, base::TimeTicks>> map;
  return *map;
}

// Values stay valid: entries are removed by the widget's own observer below,
// which fires before destruction.
std::map<AnchorKey, views::Widget*>& OpenBubbles() {
  static base::NoDestructor<std::map<AnchorKey, views::Widget*>> map;
  return *map;
}

// Stamps the close time, then deletes itself with the widget.
class ToggleCloseRecorder : public views::WidgetObserver {
 public:
  ToggleCloseRecorder(views::Widget* widget, const void* anchor)
      : anchor_(KeyFor(anchor)) {
    observation_.Observe(widget);
  }
  ToggleCloseRecorder(const ToggleCloseRecorder&) = delete;
  ToggleCloseRecorder& operator=(const ToggleCloseRecorder&) = delete;
  ~ToggleCloseRecorder() override = default;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override {
    // Hand over from the live-widget record to the stamp, so exactly one of the
    // two always speaks for this bubble.
    if (auto it = OpenBubbles().find(anchor_);
        it != OpenBubbles().end() && it->second == widget) {
      OpenBubbles().erase(it);
    }
    const base::TimeTicks now = base::TimeTicks::Now();
    std::erase_if(LastCloses(), [now](const auto& entry) {
      return now - entry.second > kToggleWindow;
    });
    LastCloses()[anchor_] = now;
  }
  void OnWidgetDestroyed(views::Widget* widget) override {
    observation_.Reset();
    delete this;
  }

 private:
  const AnchorKey anchor_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
};

}  // namespace

namespace {

// This revision of NativeTheme has no ShouldUseDarkColors(); the state lives in
// preferred_color_scheme(), which has THREE values, not two. kNoPreference is
// the common case on a machine that has never been switched, and it must fall
// to light rather than being lumped in with dark -- treating "no preference" as
// dark would ship a black browser to everyone who never chose anything.
bool OsPrefersDark() {
  return ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme() ==
         ui::NativeTheme::PreferredColorScheme::kDark;
}

}  // namespace

const Palette& Current() {
  // The OS decides. Zephyrus has no theme pref of its own, deliberately: the
  // native surfaces we do not own (context menus, WebUI, system dialogs) follow
  // the OS regardless, so any independent setting here would guarantee a
  // mismatch on some surface rather than remove one.
  //
  // NativeTheme notifies on change, and Views repaints on it, so this being a
  // live read rather than a cached value is what lets the browser follow a
  // theme flip without a restart.
  return OsPrefersDark() ? kDarkPalette : kLightPalette;
}

const Palette& PaletteFor(bool is_private) {
  // Private is its OWN palette, not a transform of the current one -- so it
  // looks the same whichever theme the machine is set to. "Am I private?"
  // should not have a different answer depending on the OS setting.
  if (is_private) {
    return kPrivatePalette;
  }
  return OsPrefersDark() ? kDarkPalette : kLightPalette;
}



void ConfigureBubble(views::BubbleDialogDelegate* bubble) {
  if (!bubble) {
    return;
  }
  // kRadiusPopup, not kRadiusCard: a bubble floats over the window rather than
  // sitting in the layout. See zephyrus_bubble_style.h.
  //
  // PROBE: on Windows, DWM rounds popup windows at the system radius (8dip) and
  // menu_config_win.cc already records a 10dip menu fill being shaved by it. A
  // bubble widget is translucent and larger than the bubble it draws, so it may
  // escape that clip where a menu window cannot. If these corners come out at
  // 28 rather than shaved back to 8, the clip does not apply here.
  bubble->set_corner_radius(kRadiusPopup);
}

void ApplyAnchoredNub(views::BubbleDialogDelegate* bubble) {
  if (!bubble) {
    return;
  }
  views::BubbleFrameView* frame = bubble->GetBubbleFrameView();
  if (!frame || !frame->bubble_border()) {
    return;
  }
  frame->bubble_border()->set_visible_arrow(true);

  // FORCE A BOUNDS RECOMPUTE, not just a relayout.
  //
  // The nub is drawn from `visible_arrow_rect_`, which BubbleBorder fills in
  // only while computing the widget's bounds. Turning the arrow on after the
  // widget exists leaves that rect empty, and an empty rect means no nub -- the
  // corners and placement come out right and the nub silently never appears.
  //
  // Bubbles constructed with autosize get this recompute for free, which is why
  // the first two popups worked and a bubble without it did not. Doing it here
  // means a caller does not have to know about that.
  frame->InvalidateLayout();
  bubble->SizeToContents();
}

void ApplyBubbleFrame(views::BubbleDialogDelegate* bubble) {
  if (!bubble) {
    return;
  }
  views::BubbleFrameView* frame = bubble->GetBubbleFrameView();
  if (!frame || !frame->bubble_border()) {
    return;
  }
  frame->bubble_border()->set_draw_border_stroke(false);

  // Every Zephyrus bubble reaches here once its widget exists, which makes this
  // the one place the toggle guard can be armed without each caller
  // remembering to.
  if (views::Widget* widget = bubble->GetWidget();
      widget && bubble->GetAnchorView()) {
    OpenBubbles()[KeyFor(bubble->GetAnchorView())] = widget;
    new ToggleCloseRecorder(widget, bubble->GetAnchorView());  // Owns itself.
  }
}

bool ConsumeReopenSuppression(const views::View* anchor) {
  if (!anchor) {
    return false;
  }
  const AnchorKey key = KeyFor(anchor);

  // Still alive — either the deactivate-close is in flight and its destruction
  // simply has not landed yet, or the bubble was never dismissed at all (a
  // keyboard-opened one holds no activation to lose). Both want the same thing
  // from a click on the trigger: shut it, and stop here.
  if (auto open = OpenBubbles().find(key); open != OpenBubbles().end()) {
    views::Widget* const widget = open->second;
    // Dropped now rather than waiting for OnWidgetDestroying, so a slow
    // teardown cannot leave the trigger deaf to the NEXT click.
    OpenBubbles().erase(open);
    if (widget && !widget->IsClosed()) {
      widget->Close();
    }
    return true;
  }

  auto& closes = LastCloses();
  const auto it = closes.find(key);
  if (it == closes.end()) {
    return false;
  }
  const bool within_click = base::TimeTicks::Now() - it->second < kToggleWindow;
  // Consumed either way: one recorded close can suppress at most one re-open,
  // so a stale stamp can never swallow a later, genuine click.
  closes.erase(it);
  return within_click;
}

}  // namespace zephyrus
