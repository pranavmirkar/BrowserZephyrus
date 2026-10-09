// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_MODEL_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_MODEL_H_

#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include "chrome/browser/zephyrus/agent/lens_eyes.h"
#include "chrome/browser/zephyrus/agent/lens_geometry.h"
#include "chrome/browser/zephyrus/agent/lens_math.h"
#include "chrome/browser/zephyrus/agent/lens_random.h"
#include "chrome/browser/zephyrus/agent/lens_spring.h"

// The "lens" mascot, without a screen: what it is doing, as numbers.
//
// A port of the Mascot class in the handoff prototype (agent-mascot.html) minus
// its DOM. `Update(dt)` advances the character; `ComputeFrame()` turns the
// current springs into the exact numbers a painter needs (body transform, lid
// paths, eye transforms, tears, ripples, fluid-fill geometry). No views, no
// Skia, no clock: that is what makes 25 emotions testable, and what lets the
// port be compared number for number against the prototype running under node.
//
// A state never animates anything directly. Each frame it writes TARGETS into
// `targets_` (reset to neutral first) and the springs chase them, so moving
// between any two states is smooth for free.

namespace zephyrus::agent {

// Everything the character can be doing. The first 6 are the prototype's
// activities and the next 25 its emotions; the rest are new, added for what
// the browser does that the prototype had no state for (typing, reading,
// scrolling, being listened to, waiting for a decision, handing over to the
// user, and so on). The new ones are built from the same pose keys, so the
// painter needs nothing new to show them.
enum class LensState : int {
  // Prototype activities.
  kIdle,
  kWorking,
  kSearching,
  kClicking,
  kDone,
  kError,
  // Prototype emotions.
  kJoy,
  kSadness,
  kAnger,
  kFear,
  kSurprise,
  kDisgust,
  kContempt,
  kLove,
  kExcited,
  kLaughing,
  kPleading,
  kShy,
  kCurious,
  kConfused,
  kThinking,
  kSuspicious,
  kDetermined,
  kSmug,
  kMischievous,
  kWink,
  kBored,
  kSleepy,
  kCrying,
  kAnxious,
  kDizzy,
  // Added for browser functions (not in the prototype).
  kTyping,
  kReading,
  kScrolling,
  kListening,
  kSpeaking,
  kAsking,
  kHandoff,
  kDownloading,
  kLoading,
  kSleeping,
  kGreeting,
  kHeld,
  kRelieved,
  kSwitching,
  kCount,
};

inline constexpr int kLensStateCount = static_cast<int>(LensState::kCount);
inline constexpr int kLensPrototypeStateCount =
    static_cast<int>(LensState::kTyping);

const char* LensStateKey(LensState state);    // "idle", "joy", "typing"...
const char* LensStateLabel(LensState state);  // "Idle", "Joy", "Typing"...
std::optional<LensState> LensStateFromKey(std::string_view key);
bool LensStateIsEmotion(LensState state);

// How the body is filled. Opal and Space exist in the weights (the layer
// maths is shared) but only Static and Fluid are drawn in version 1.
enum class LensFill { kStatic, kFluid, kOpal, kSpace };

enum class LensHighlight : int { kBowl, kHeart, kStar, kDot, kSpiral, kCount };
inline constexpr int kLensHighlightCount =
    static_cast<int>(LensHighlight::kCount);

enum class LensPalette : int {
  kNeutral,
  kWarm,
  kRed,
  kBlue,
  kViolet,
  kGreen,
  kPink,
  kTeal,
  kOpal,
  kPurple,
};
using LensPaletteColors = std::array<LensRgb, 7>;
const LensPaletteColors& LensPaletteFor(LensPalette palette);

// One of the fluid fill's seven soft colour fields. `r` is the radius of the
// radial field, `rx`/`ry` the orbit radii, `w` the orbit speed, `p` the phase,
// `st` how stretched the streak is. The last two are blended with "screen".
struct LensBlobSpec {
  double r, rx, ry, w, p, st;
  bool core;
  bool screen;
};
const std::array<LensBlobSpec, 7>& LensBlobSpecs();

// The pose springs. Order is the prototype's POSE_KEYS.
enum class LensPose : int {
  kS,  // body slant, -1..1
  kK,  // body breathing, about 0.85..1.13
  kOpen,
  kOpenR,
  kTilt,
  kTiltR,
  kCheek,
  kCheekR,
  kSc,
  kScR,
  kEsx,
  kEsy,
  kRot,
  kEyeY,
  kGap,
  kHsx,
  kHsy,
  kHs,
  kSx,
  kSy,
  kBodyY,
  kScale,
  kRotB,
  kLean,
  kTremor,
  kEnergy,
  kTintA,
  kSat,
  kBri,
  kBeamA,
  kCount,
};
inline constexpr int kLensPoseCount = static_cast<int>(LensPose::kCount);

// What a state may ask for this frame. Reset to neutral before every state
// update; `*R` (right eye) values default to the left eye's.
struct LensTargets {
  double s = 0, k = 1, open = 1, tilt = 0, cheek = 0, sc = 1;
  std::optional<double> open_r, tilt_r, cheek_r, sc_r;
  double esx = 1, esy = 1, rot = 0, eye_y = 0, gap = 0;
  double hsx = 1, hsy = 1, hs = 1;
  LensHighlight hl = LensHighlight::kBowl;
  double glint = 0;
  double sx = 1, sy = 1, body_y = 0, scale = 1, rot_b = 0, lean = 0;
  double tremor = 0, hz = 12, energy = 0.3, sat = 1, bri = 1;
  LensRgb tint_color{255, 70, 70};
  double tint_alpha = 0, beam_alpha = 0;
  LensPalette pal = LensPalette::kNeutral;
  double pal_mix = 0;
  LensRgb eye_color{15, 92, 171};
  double eye_mix = 0;
};

// Per-state scratch memory (the prototype's `m.mem`).
struct LensMemory {
  double t0 = 0;
  double next = 0;
  int col = 0, line = 0;
  std::vector<std::array<double, 2>> pts;
  int i = 0;
  std::optional<std::array<double, 2>> back;
  int stage = 0;
  double tx = 0, ty = 0, gap = 0, ph0 = 0;
  bool flashed = false, slow = false, looked = false, down = false;
  double nl = 0, ng = 0, nt = 0;
  int side = 1;
  bool away = false, peek = false, jolt = false;
  double hold = 0, sh = -9;
  // Used by the states added for browser functions.
  double a = 0, b = 0;
  int n = 0;
};

// ---- what a painter is given -------------------------------------------------

struct LensEyeFrame {
  LensLid lid;
  // translate(tx, ty) rotate(rot_deg about (0,-5)) scale(sx, sy), applied to
  // the eye drawn with its dome's flat base centred at the origin.
  double tx = 0, ty = 0, rot_deg = 0, sx = 1, sy = 1;
  // The highlight group's own offset, which moves a little more than the dome
  // when the gaze moves (depth).
  double hl_tx = 0, hl_ty = 0;
  struct Shape {
    bool show = false;
    double sx = 1, sy = 1, rot_deg = 0;
  };
  std::array<Shape, kLensHighlightCount> shapes;
};

struct LensTearFrame {
  double x = 0, y = 0, sx = 1, sy = 1, opacity = 0;
};

struct LensRippleFrame {
  bool active = false;
  double cx = 0, cy = 0, r = 0, stroke = 0, opacity = 0;
};

struct LensBlobFrame {
  double tx = 0, ty = 0, rot_deg = 0, sx = 1, sy = 1;
};

// The fluid fill's geometry; only computed while that layer is visible.
struct LensFluidFrame {
  bool computed = false;
  LensPaletteColors palette;
  std::array<LensBlobFrame, 7> blobs;
  // Three waveform ribbons, 31 points each (x, y).
  std::array<std::array<LensPoint, 31>, 3> ribbons;
  double rim_rotation_deg = 0;
  double rim_glow_opacity = 0;
  double gloss_tx = 0, gloss_ty = 0, gloss_rot_deg = 0;
  double blobs_scale = 1;
};

struct LensFrame {
  double s = 0, u = 0.5, k = 1;
  LensLayerOpacities layers;
  double sat = 1, bri = 1;
  // translate(tx, ty) rotate(rot_deg) scale(sx, sy), about (125.5, 330).
  double body_tx = 0, body_ty = 0, body_rot_deg = 0, body_sx = 1, body_sy = 1;
  // The face turns with the slant: rotate(face_rot_deg about (125.5, 260)).
  double face_rot_deg = 0;
  std::array<LensEyeFrame, 2> eyes;  // [0] left, [1] right
  LensRgb eye_bottom, eye_middle;
  std::vector<LensTearFrame> tears;
  LensRgb tint_color;
  double tint_opacity = 0;
  double beam_opacity = 0, beam_x = kLensCenterX;
  double flash_opacity = 0;
  std::array<LensRippleFrame, 3> ripples;
  LensFluidFrame fluid;
};

// ---- the model ------------------------------------------------------------------

class LensModel {
 public:
  // `random` is not owned and must outlive the model. Construction enters
  // idle, which draws from `random` (the first blink time), as the prototype's
  // constructor does.
  explicit LensModel(LensRandom* random);
  ~LensModel();
  LensModel(const LensModel&) = delete;
  LensModel& operator=(const LensModel&) = delete;

  // Back to a neutral pose at time 0, as if just built, WITHOUT drawing any
  // randomness; the state is left at idle until SetState. Used to start a
  // seeded comparison run, and when the skin is switched.
  void Reset();
  void SetRandom(LensRandom* random) { rng_ = random; }

  // `force` replays a state that is already current.
  void SetState(LensState state, bool force = false);
  LensState state() const { return state_; }

  // 0.3 to 1: how far an emotion moves from neutral.
  void SetIntensity(double intensity);
  double intensity() const { return intensity_; }

  void SetFill(LensFill fill);
  LensFill fill() const { return fill_; }

  // Motion scale: 1, or 0.35 when the user prefers reduced motion.
  void SetMotionScale(double amp) { amp_ = amp; }
  double motion_scale() const { return amp_; }

  // Overrides the automatic body slant; nullopt returns to automatic.
  void SetManualSlant(std::optional<double> s) { manual_s_ = s; }

  // Gaze, -1..1 on each axis.
  void Look(double x, double y);

  // Where the pointer is, normalised -1..1; used by idle when following.
  void SetCursor(double nx, double ny);
  void ClearCursor();
  void SetFollowCursor(bool follow) { follow_cursor_ = follow; }

  void BlinkNow(double slow = 1.0, bool double_blink = false);
  // A click ripple at body coordinates, with the quick blink of a press.
  void Press(double x, double y);
  // A ripple and a white flash on their own (surprise uses both).
  void AddRipple(double x, double y);
  void Flash() { flash_v_ = 1; }
  // The body flinches for a moment (a squash that eases back by itself), on top
  // of whatever the state is doing. The overlay calls it when the page really
  // receives the agent's press, so the squash lands with the click and not on
  // the state's own timer.
  void Squash(double amount = 1.0) { squash_ = std::max(squash_, amount); }
  // 0 left, 1 right.
  void Tear(int side);

  // Advance by `dt` seconds (clamped to 50 ms, like the prototype's frame).
  void Update(double dt);

  // The numbers a painter needs for the current instant.
  LensFrame ComputeFrame() const;

  double time() const { return t_; }

  // For tests and diagnostics.
  double PoseValue(LensPose pose) const {
    return pose_[static_cast<int>(pose)].x;
  }
  double GazeX() const { return gx_.x; }
  double GazeY() const { return gy_.x; }
  double HighlightWeight(LensHighlight h) const {
    return hw_[static_cast<int>(h)].x;
  }
  const std::vector<LensBlink>& blinks() const { return blinks_; }
  struct TearDrop {
    double t0 = 0;
    int side = 0;
    double dx = 0;
  };
  const std::vector<TearDrop>& tears() const { return tears_; }
  const LensRgb& eye_color() const { return eye_cur_; }
  const LensRgb& tint_color() const { return tint_cur_; }
  double flash() const { return flash_v_; }
  double fluid_phase() const { return fluid_phase_; }
  const LensPaletteColors& palette() const { return pal_cur_; }
  double next_blink_time() const { return next_blink_; }
  double Rate() const { return rate_; }
  double LayerWeightFluid() const { return wf_.x; }
  double GazeTargetX() const { return g_.x; }
  double GazeTargetY() const { return g_.y; }
  struct Ripple {
    double t0 = -9, x = 0, y = 0;
  };
  const std::array<Ripple, 3>& ripples() const { return ripples_; }

 private:
  friend struct LensStates;

  void ResetPoseSprings();

  LensRandom* rng_;
  LensState state_ = LensState::kIdle;
  LensMemory mem_;
  LensTargets targets_;
  double t_ = 0;
  double rate_ = 1;
  double intensity_ = 1;
  double amp_ = 1;
  LensFill fill_ = LensFill::kStatic;
  std::optional<double> manual_s_;

  std::array<LensSpring, kLensPoseCount> pose_;
  LensSpring gx_, gy_, wf_, wo_, wp_, beam_x_;
  std::array<LensSpring, kLensHighlightCount> hw_;

  struct Gaze {
    double x = 0, y = 0;
  } g_;
  std::optional<std::array<double, 2>> cursor_;
  double cursor_t_ = -99;
  bool follow_cursor_ = false;

  std::vector<LensBlink> blinks_;
  double next_blink_ = 2;
  std::array<double, 2> blink_range_{2.2, 5.8};

  std::array<Ripple, 3> ripples_;
  int ripple_i_ = 0;
  std::vector<TearDrop> tears_;

  double flash_v_ = 0;
  double squash_ = 0;
  double fluid_phase_ = 0;
  LensRgb eye_cur_;
  LensRgb tint_cur_;
  LensPaletteColors pal_cur_;
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_MODEL_H_
