// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_MOOD_MAP_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_MOOD_MAP_H_

#include "chrome/browser/zephyrus/agent/lens_model.h"
#include "chrome/browser/zephyrus/agent/mascot_rig.h"

namespace zephyrus::agent {

// What the lens skin shows for each of Zep's moods.
//
// Zep's mood is the one thing the rest of the browser knows (the panel, voice
// and the task controller all say "thinking", "asking", "happy"), so the lens
// skin listens to it rather than to a second set of calls. The rig stays the
// mood's state machine (it settles a wave into idle and a yawn into sleep by
// itself); this only says how each mood looks on the lens. The table was
// approved on 2026-10-08 (docs/mascot-lens-plan.md).
//
// `seconds_in_mood` matters for one mood: a task that has finished (happy) is
// first the lens's "done" (the body swings across, two bounces, a flash) and
// then settles into joy.
LensState LensStateForMood(MascotMood mood, double seconds_in_mood);

// How long "done" lasts before it settles into joy.
inline constexpr double kLensDoneSeconds = 2.4;

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_MOOD_MAP_H_
