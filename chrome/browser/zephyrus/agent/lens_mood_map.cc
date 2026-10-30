// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/lens_mood_map.h"

namespace zephyrus::agent {

LensState LensStateForMood(MascotMood mood, double seconds_in_mood) {
  switch (mood) {
    case MascotMood::kIdle:
      return LensState::kIdle;
    case MascotMood::kSleeping:
      return LensState::kSleeping;
    case MascotMood::kYawning:
      return LensState::kSleepy;
    case MascotMood::kWaving:
      return LensState::kGreeting;
    case MascotMood::kListening:
      return LensState::kListening;
    case MascotMood::kThinking:
      return LensState::kThinking;
    case MascotMood::kReading:
      return LensState::kReading;
    case MascotMood::kSearching:
      return LensState::kSearching;
    case MascotMood::kTyping:
      return LensState::kTyping;
    case MascotMood::kClicking:
      return LensState::kClicking;
    case MascotMood::kScrolling:
      return LensState::kScrolling;
    case MascotMood::kAsking:
      return LensState::kAsking;
    case MascotMood::kHappy:
      return seconds_in_mood < kLensDoneSeconds ? LensState::kDone
                                                : LensState::kJoy;
    case MascotMood::kConfused:
      return LensState::kConfused;
    case MascotMood::kAlert:
      return LensState::kSurprise;
    case MascotMood::kWorking:
      return LensState::kWorking;
    case MascotMood::kHeld:
      return LensState::kHeld;
    case MascotMood::kRelieved:
      return LensState::kRelieved;
  }
  return LensState::kIdle;
}

}  // namespace zephyrus::agent
