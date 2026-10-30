// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_STATES_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_STATES_H_

#include "chrome/browser/zephyrus/agent/lens_model.h"

namespace zephyrus::agent {

// The states' view into the model. Private to the lens implementation: a state
// reads and writes the model's targets, memory and gaze directly, exactly as
// the prototype's states touch `m`, and the model befriends this struct rather
// than exposing those internals to the rest of the browser.
struct LensStates {
  // Called when a state is entered, after the model has reset its memory.
  static void Init(LensModel& m, LensState s);
  // Called every frame, after the targets have been reset to neutral.
  static void Update(LensModel& m, LensState s, double t);

  static double Rand(LensModel& m, double lo, double hi) {
    return m.rng_->Range(lo, hi);
  }
  static bool Chance(LensModel& m, double p) { return m.rng_->Chance(p); }

  // The activities (the prototype's six plus the states added for browser
  // functions); lens_states_activities.cc.
  static void InitActivity(LensModel& m, LensState s);
  static void UpdateActivity(LensModel& m, LensState s, double t);
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_STATES_H_
