// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_ONBOARDING_FLOW_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_ONBOARDING_FLOW_H_

#include <string_view>

class PrefService;

namespace zephyrus::agent {

// Set once the person has finished the first-run screens or skipped them. Never
// set by anything else: a profile that has not seen onboarding shows it.
inline constexpr char kOnboardingDonePref[] = "zephyrus.onboarding.done";

// Dev builds only (see DevSwitchesEnabled): shows onboarding again whatever the
// pref says, so the screens can be inspected and photographed.
inline constexpr char kOnboardingForceSwitch[] = "zephyrus-test-onboarding";

enum class OnboardingStep : int {
  kWelcome = 0,      // Zep waves; what the browser is
  kLook,             // mascot look and theme
  kWorkspaces,       // separate spaces for work, home, study
  kAgentAndVoice,    // agent access and the Hey Zep voice
  kPrivacyAndImport  // blocking, search, bookmarks, default browser
};
inline constexpr int kOnboardingStepCount = 5;

// Where the person is in the flow. Pure: no views, no prefs, so the rules for
// moving about (Back on the first screen does nothing, Next on the last one
// finishes) are tested without a window.
class OnboardingFlow {
 public:
  OnboardingFlow() = default;

  OnboardingStep step() const { return static_cast<OnboardingStep>(index_); }
  int index() const { return index_; }
  int count() const { return kOnboardingStepCount; }

  bool CanGoBack() const { return !finished_ && index_ > 0; }
  bool IsLastStep() const { return index_ == kOnboardingStepCount - 1; }

  // Moves on one screen. On the last screen it finishes the flow instead.
  // Returns true if the screen changed.
  bool Next();
  // Moves back one screen. Does nothing on the first. Returns true if it moved.
  bool Back();
  // Jumps to a screen, ignoring out-of-range requests. Returns true if it moved.
  bool GoTo(int index);

  // Leaves the flow without going through the rest. Skipped is not the same as
  // finished, but both set the done pref: someone who skipped has said no.
  void Skip();

  bool finished() const { return finished_; }
  bool skipped() const { return skipped_; }

 private:
  int index_ = 0;
  bool finished_ = false;
  bool skipped_ = false;
};

// How Zep reaches its model, as the agent screen describes it. Pure, so the
// wording the person reads can only ever follow what is really configured.
enum class AgentAccess {
  kBuiltIn,   // this build's own access is on and in use
  kOwnKey,    // the person saved a key of their own and it is in use
  kNoKeyYet,  // nothing usable: Zep cannot do tasks until a key is added
};

// `bundled_available`: the build ships access of its own. `use_provided`: the
// provided-keys switch is on. `own_key_saved`: a key the person saved exists.
// Built-in wins only when it is both there and switched on.
AgentAccess AgentAccessFor(bool bundled_available,
                           bool use_provided,
                           bool own_key_saved);

// ---- The sky ------------------------------------------------------------------

// Which sky the onboarding shows: it follows the person's local time.
enum class SkyTime { kDawn, kDay, kSunset, kNight };

// Dawn 05:00-07:59, day 08:00-16:59, sunset 17:00-19:29, night 19:30-04:59.
SkyTime SkyTimeForClock(int hour, int minute);

// Dev builds only: --zephyrus-test-sky=dawn|day|sunset|night picks the sky, so
// each one can be looked at without waiting for the evening.
inline constexpr char kOnboardingSkySwitch[] = "zephyrus-test-sky";
// The sky a switch value names, or `fallback` for anything else.
SkyTime SkyTimeFromName(std::string_view name, SkyTime fallback);

// Where Zep is on its flight around the onboarding, `seconds` into it: x and y
// as fractions of the window (0..1), and which way it is heading. The path is a
// slow loop around the middle of the window, where the card is, so Zep flies
// AROUND the controls (over the headline, down the sides, under the card)
// instead of sitting behind them.
struct FlightPoint {
  double x = 0.5;
  double y = 0.5;
  double vx = 0;  // fractions of the window per second
  double vy = 0;
};
FlightPoint FlightPathAt(double seconds);
// The same loop round a centre (`cx`, `cy`) with half-widths `rx`, `ry`, all as
// fractions of the window, so it can be fitted around the card on any window.
FlightPoint FlightPathAt(double seconds, double cx, double cy, double rx,
                         double ry);

// Whether onboarding ships switched on. FALSE while the screens are being built:
// with the pref defaulting to "not done", every existing profile would otherwise
// meet half a flow on its next launch. Flip it when the four screens are finished
// and checked; until then only the dev switch shows them.
inline constexpr bool kOnboardingEnabled = false;

// Whether the first-run screens should show: forced by the dev switch, or the
// feature is on and they have not been seen. `forced` is computed by the caller
// (it needs the command line); `enabled` is kOnboardingEnabled, passed in so the
// rule is testable both ways.
bool ShouldShowOnboarding(const PrefService& prefs, bool forced, bool enabled);

// Remembers that onboarding has been seen, finished or skipped.
void MarkOnboardingDone(PrefService& prefs);

// "Run setup again" in Settings.
void ResetOnboarding(PrefService& prefs);

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_ONBOARDING_FLOW_H_
