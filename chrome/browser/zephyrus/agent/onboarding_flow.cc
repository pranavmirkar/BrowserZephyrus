// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/onboarding_flow.h"

#include <cmath>

#include "components/prefs/pref_service.h"

namespace zephyrus::agent {

bool OnboardingFlow::Next() {
  if (finished_) {
    return false;
  }
  if (IsLastStep()) {
    finished_ = true;
    return false;
  }
  ++index_;
  return true;
}

bool OnboardingFlow::Back() {
  if (!CanGoBack()) {
    return false;
  }
  --index_;
  return true;
}

bool OnboardingFlow::GoTo(int index) {
  if (finished_ || index < 0 || index >= kOnboardingStepCount ||
      index == index_) {
    return false;
  }
  index_ = index;
  return true;
}

void OnboardingFlow::Skip() {
  finished_ = true;
  skipped_ = true;
}

AgentAccess AgentAccessFor(bool bundled_available,
                           bool use_provided,
                           bool own_key_saved) {
  if (bundled_available && use_provided) {
    return AgentAccess::kBuiltIn;
  }
  return own_key_saved ? AgentAccess::kOwnKey : AgentAccess::kNoKeyYet;
}

SkyTime SkyTimeForClock(int hour, int minute) {
  const int at = hour * 60 + minute;
  if (at >= 5 * 60 && at < 8 * 60) {
    return SkyTime::kDawn;
  }
  if (at >= 8 * 60 && at < 17 * 60) {
    return SkyTime::kDay;
  }
  if (at >= 17 * 60 && at < 19 * 60 + 30) {
    return SkyTime::kSunset;
  }
  return SkyTime::kNight;
}

SkyTime SkyTimeFromName(std::string_view name, SkyTime fallback) {
  if (name == "dawn") {
    return SkyTime::kDawn;
  }
  if (name == "day") {
    return SkyTime::kDay;
  }
  if (name == "sunset") {
    return SkyTime::kSunset;
  }
  if (name == "night") {
    return SkyTime::kNight;
  }
  return fallback;
}

FlightPoint FlightPathAt(double seconds) {
  return FlightPathAt(seconds, 0.5, 0.48, 0.40, 0.36);
}

FlightPoint FlightPathAt(double seconds, double cx, double cy, double rx,
                         double ry) {
  // An ellipse around the centre, gently wobbling so it never feels mechanical:
  // one lap about every 52 seconds, a slow bob on top of it.
  constexpr double kTau = 6.283185307179586;
  constexpr double kLap = 52.0;
  const double a = kTau * seconds / kLap;
  const double wobble = 0.03 * std::sin(a * 3.0 + 0.7);
  const double bob = 0.018 * std::sin(kTau * seconds / 2.6);
  FlightPoint p;
  p.x = cx + (rx + wobble) * std::cos(a);
  p.y = cy + (ry + wobble) * std::sin(a) + bob;
  // Derivatives, for which way it faces and how much it leans.
  const double da = kTau / kLap;
  p.vx = -(rx + wobble) * std::sin(a) * da;
  p.vy = (ry + wobble) * std::cos(a) * da +
         0.018 * std::cos(kTau * seconds / 2.6) * kTau / 2.6;
  return p;
}

bool ShouldShowOnboarding(const PrefService& prefs, bool forced, bool enabled) {
  return forced || (enabled && !prefs.GetBoolean(kOnboardingDonePref));
}

void MarkOnboardingDone(PrefService& prefs) {
  prefs.SetBoolean(kOnboardingDonePref, true);
}

void ResetOnboarding(PrefService& prefs) {
  prefs.SetBoolean(kOnboardingDonePref, false);
}

}  // namespace zephyrus::agent
