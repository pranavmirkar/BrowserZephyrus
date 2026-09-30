// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_POINTER_EVENTS_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_POINTER_EVENTS_H_

#include <optional>

#include "ui/gfx/geometry/point.h"

namespace zephyrus::agent {

// What the agent's pointer is doing, for whoever draws it.
//
// The browser performs every action itself, through the same input pipeline a
// person's mouse uses, so it is also the one place that knows exactly where the
// pointer is and what it is about to do. This is that knowledge, handed on. It
// is one-way: an observer can only watch. Nothing about a drawing of the
// pointer can move the real one, click, or slow it down.
struct PointerEvent {
  enum class Kind {
    // The pointer is at `position` and moving.
    kMove,
    // It has arrived and is about to act.
    kArrive,
    // The button went down / came up at `position`.
    kPress,
    kRelease,
    // The wheel is turning at `position`; `direction` is +1 down, -1 up.
    kScroll,
    // Keys are being typed with the pointer resting at `position`.
    kTypeBegin,
    kTypeEnd,
    // The agent is not driving the pointer any more.
    kIdle,
  };

  Kind kind = Kind::kIdle;
  // Screen coordinates, in DIP.
  gfx::Point position;
  int direction = 0;
};

class PointerObserver {
 public:
  virtual ~PointerObserver();

  virtual void OnPointer(const PointerEvent& event) = 0;

  // Where the pointer already is, in screen DIP, if something is drawing one.
  // A move starts there, so the pointer appears to travel from where the
  // character stands instead of appearing beside its target.
  virtual std::optional<gfx::Point> PointerHome();
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_POINTER_EVENTS_H_
