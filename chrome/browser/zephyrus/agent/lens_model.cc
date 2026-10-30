// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/lens_model.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "chrome/browser/zephyrus/agent/lens_states.h"

namespace zephyrus::agent {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct StateInfo {
  const char* key;
  const char* label;
};

constexpr std::array<StateInfo, kLensStateCount> kStateInfo = {{
    {"idle", "Idle"},
    {"working", "Working"},
    {"searching", "Searching"},
    {"clicking", "Clicking"},
    {"done", "Done"},
    {"error", "Error"},
    {"joy", "Joy"},
    {"sadness", "Sadness"},
    {"anger", "Anger"},
    {"fear", "Fear"},
    {"surprise", "Surprise"},
    {"disgust", "Disgust"},
    {"contempt", "Contempt"},
    {"love", "Love"},
    {"excited", "Excited"},
    {"laughing", "Laughing"},
    {"pleading", "Pleading"},
    {"shy", "Shy"},
    {"curious", "Curious"},
    {"confused", "Confused"},
    {"thinking", "Thinking"},
    {"suspicious", "Suspicious"},
    {"determined", "Determined"},
    {"smug", "Smug"},
    {"mischievous", "Mischievous"},
    {"wink", "Wink"},
    {"bored", "Bored"},
    {"sleepy", "Sleepy"},
    {"crying", "Crying"},
    {"anxious", "Anxious"},
    {"dizzy", "Dizzy"},
    {"typing", "Typing"},
    {"reading", "Reading"},
    {"scrolling", "Scrolling"},
    {"listening", "Listening"},
    {"speaking", "Speaking"},
    {"asking", "Asking"},
    {"handoff", "Handing over"},
    {"downloading", "Downloading"},
    {"loading", "Loading"},
    {"sleeping", "Sleeping"},
    {"greeting", "Greeting"},
    {"held", "Held"},
    {"relieved", "Relieved"},
    {"switching", "Switching tabs"},
}};

LensRgb Hex(unsigned v) {
  return {static_cast<double>((v >> 16) & 0xFF),
          static_cast<double>((v >> 8) & 0xFF), static_cast<double>(v & 0xFF)};
}

// The prototype's PALS: deep blue, purple, pink, bright blue, orange, gold,
// white core, in that role order, per mood.
const std::array<LensPaletteColors, 10> kPalettes = {{
    {Hex(0x2c3fbf), Hex(0x8d68f5), Hex(0xf06ea6), Hex(0x4f78f2), Hex(0xf79b5c),
     Hex(0xffe3b4), Hex(0xfff7ec)},  // neutral
    {Hex(0xff7a3c), Hex(0xffb02e), Hex(0xff5c8a), Hex(0x19c8ff), Hex(0xffd23c),
     Hex(0xfff3b8), Hex(0xffffff)},  // warm
    {Hex(0x7a0f26), Hex(0xc4161c), Hex(0xff3b30), Hex(0xff6a1a), Hex(0xff9a3c),
     Hex(0xffd0a0), Hex(0xffe8d6)},  // red
    {Hex(0x12306b), Hex(0x1d3f86), Hex(0x2a4a8a), Hex(0x2e86c1), Hex(0x4f78b8),
     Hex(0x9fb7d9), Hex(0xd6e4f5)},  // blue
    {Hex(0x3a2a8a), Hex(0x7a5cd6), Hex(0xb05cff), Hex(0x5d89f0), Hex(0x9e78ff),
     Hex(0xd8ccff), Hex(0xf2eeff)},  // violet
    {Hex(0x2b4a14), Hex(0x5c8a1c), Hex(0x9bbf2a), Hex(0x2fa87a), Hex(0xc7d43a),
     Hex(0xecf2a8), Hex(0xf8fbd8)},  // green
    {Hex(0xa1246b), Hex(0xe0457b), Hex(0xff5fa8), Hex(0xff8fb8), Hex(0xff9f7a),
     Hex(0xffd2d9), Hex(0xfff0f3)},  // pink
    {Hex(0x0d4f6b), Hex(0x1fb5a8), Hex(0x4fd1c5), Hex(0x0EBFF4), Hex(0x7fe0b0),
     Hex(0xd4fbf0), Hex(0xf3fffd)},  // teal
    {Hex(0xff5fb0), Hex(0xff9a5c), Hex(0xffd65c), Hex(0x52f0c2), Hex(0x46c4ff),
     Hex(0x7a6bff), Hex(0xe24dff)},  // opal
    {Hex(0x3b1a78), Hex(0x7a4fd1), Hex(0xc040ff), Hex(0xff5cc8), Hex(0xff8f5c),
     Hex(0xf0c8ff), Hex(0xfff0ff)},  // purple
}};

constexpr LensRgb kNeutralEye{15, 92, 171};

// The fluid fill's seven colour fields. r is the radius of the soft radial
// field, rx/ry the orbit radii, w the orbit speed, p the phase and st how
// stretched the streak is.
constexpr std::array<LensBlobSpec, 7> kBlobs = {{
    {118, 86, 56, 0.34, 0.0, 1.35, false, false},
    {92, 78, 52, -0.46, 2.1, 1.55, false, false},
    {74, 62, 44, 0.60, 4.0, 1.60, false, false},
    {78, 84, 54, -0.27, 1.0, 1.50, false, false},
    {66, 56, 38, 0.78, 5.4, 1.55, false, false},
    {50, 30, 20, -0.88, 0.3, 1.30, false, true},
    {40, 10, 8, 0.55, 0.8, 1.10, true, true},
}};

// Per-pose spring stiffness and damping (the prototype's SPRING_CFG) and the
// neutral value each pose starts from.
struct PoseSpec {
  double w, z, neutral;
};
constexpr std::array<PoseSpec, kLensPoseCount> kPoseSpec = {{
    /* s      */ {7, 0.7, 0},
    /* k      */ {8, 1, 1},
    /* open   */ {30, 1, 1},
    /* openR  */ {30, 1, 1},
    /* tilt   */ {14, 1, 0},
    /* tiltR  */ {14, 1, 0},
    /* cheek  */ {16, 1, 0},
    /* cheekR */ {16, 1, 0},
    /* sc     */ {16, 0.7, 1},
    /* scR    */ {16, 0.7, 1},
    /* esx    */ {18, 0.7, 1},
    /* esy    */ {18, 0.7, 1},
    /* rot    */ {14, 1, 0},
    /* eyeY   */ {12, 1, 0},
    /* gap    */ {10, 1, 0},
    /* hsx    */ {16, 0.8, 1},
    /* hsy    */ {16, 0.8, 1},
    /* hs     */ {16, 0.8, 1},
    /* sx     */ {26, 0.5, 1},
    /* sy     */ {26, 0.5, 1},
    /* bodyY  */ {10, 0.8, 0},
    /* scale  */ {14, 0.7, 1},
    /* rotB   */ {10, 0.7, 0},
    /* lean   */ {10, 0.8, 0},
    /* tremor */ {10, 1, 0},
    /* energy */ {3.5, 1, 0.3},
    /* tintA  */ {5, 1, 0},
    /* sat    */ {4, 1, 1},
    /* bri    */ {4, 1, 1},
    /* beamA  */ {6, 1, 0},
}};

double Dist(double a, double b) { return std::sqrt(a * a + b * b); }

}  // namespace

const std::array<LensBlobSpec, 7>& LensBlobSpecs() {
  return kBlobs;
}

const char* LensStateKey(LensState state) {
  return kStateInfo[static_cast<int>(state)].key;
}

const char* LensStateLabel(LensState state) {
  return kStateInfo[static_cast<int>(state)].label;
}

std::optional<LensState> LensStateFromKey(std::string_view key) {
  for (int i = 0; i < kLensStateCount; ++i) {
    if (key == kStateInfo[i].key) {
      return static_cast<LensState>(i);
    }
  }
  return std::nullopt;
}

bool LensStateIsEmotion(LensState state) {
  return state >= LensState::kJoy && state < LensState::kTyping;
}

const LensPaletteColors& LensPaletteFor(LensPalette palette) {
  return kPalettes[static_cast<int>(palette)];
}

// ---- construction -----------------------------------------------------------------

LensModel::LensModel(LensRandom* random) : rng_(random) {
  Reset();
  SetState(LensState::kIdle, /*force=*/true);
}

LensModel::~LensModel() = default;

void LensModel::Reset() {
  gx_ = LensSpring(0, 46, 1);
  gy_ = LensSpring(0, 46, 1);
  wf_ = LensSpring(0, 5, 1);
  wo_ = LensSpring(0, 5, 1);
  wp_ = LensSpring(0, 5, 1);
  beam_x_ = LensSpring(kLensCenterX, 14, 1);
  for (int i = 0; i < kLensPoseCount; ++i) {
    pose_[i] = LensSpring(kPoseSpec[i].neutral, kPoseSpec[i].w, kPoseSpec[i].z);
  }
  for (int i = 0; i < kLensHighlightCount; ++i) {
    hw_[i] = LensSpring(i == 0 ? 1 : 0, 18, 0.6);
  }
  targets_ = LensTargets{};
  mem_ = LensMemory{};
  t_ = 0;
  rate_ = 1;
  g_ = {};
  cursor_.reset();
  cursor_t_ = -99;
  blinks_.clear();
  next_blink_ = 2;
  blink_range_ = {2.2, 5.8};
  ripples_ = {};
  ripple_i_ = 0;
  tears_.clear();
  flash_v_ = 0;
  squash_ = 0;
  fluid_phase_ = 0;
  eye_cur_ = kNeutralEye;
  tint_cur_ = {255, 70, 70};
  pal_cur_ = kPalettes[0];
  state_ = LensState::kIdle;
}

// ---- state and controls -------------------------------------------------------------

void LensModel::SetState(LensState state, bool force) {
  if (state == state_ && !force) {
    return;
  }
  state_ = state;
  mem_ = LensMemory{};
  mem_.t0 = t_;
  rate_ = 1;
  LensStates::Init(*this, state);
  next_blink_ = t_ + rng_->Range(blink_range_[0], blink_range_[1]);
  if (rng_->Chance(0.6)) {
    BlinkNow();
  }
}

void LensModel::SetIntensity(double intensity) {
  intensity_ = LensClamp(intensity, 0.3, 1.0);
}

void LensModel::SetFill(LensFill fill) {
  fill_ = fill;
  wf_.t = fill == LensFill::kFluid ? 1 : 0;
  wo_.t = fill == LensFill::kOpal ? 1 : 0;
  wp_.t = fill == LensFill::kSpace ? 1 : 0;
}

void LensModel::Look(double x, double y) {
  // A big jump of the gaze often comes with a blink, if one is not under way.
  if (Dist(x - g_.x, y - g_.y) > 1 && rng_->Chance(0.35) && blinks_.empty()) {
    BlinkNow();
  }
  g_ = {x, y};
  gx_.t = x;
  gy_.t = y;
}

void LensModel::SetCursor(double nx, double ny) {
  cursor_ = std::array<double, 2>{nx, ny};
  cursor_t_ = t_;
}

void LensModel::ClearCursor() {
  cursor_.reset();
  cursor_t_ = t_;
}

void LensModel::BlinkNow(double slow, bool double_blink) {
  blinks_.push_back({t_, slow});
  if (double_blink) {
    blinks_.push_back({t_ + 0.28 * slow, slow});
  }
}

void LensModel::AddRipple(double x, double y) {
  ripples_[ripple_i_++ % 3] = {t_, x, y};
}

void LensModel::Press(double x, double y) {
  BlinkNow(0.8);
  AddRipple(x, y);
}

void LensModel::Tear(int side) {
  if (tears_.size() >= 12) {
    return;
  }
  tears_.push_back({t_, side, rng_->Range(-0.6, 0.6)});
}

// ---- the frame ------------------------------------------------------------------------

void LensModel::Update(double dt) {
  dt = std::min(0.05, dt);
  if (!(dt > 0)) {
    return;
  }
  t_ += dt;
  const double t = t_;

  // A state writes targets, starting from neutral.
  targets_ = LensTargets{};
  LensTargets& T = targets_;
  LensStates::Update(*this, state_, t);
  if (squash_ > 0.001) {
    T.sy -= 0.14 * squash_;
    T.sx += 0.08 * squash_;
  }
  squash_ *= std::exp(-dt * 10);

  if (t >= next_blink_) {
    if (blinks_.empty()) {
      BlinkNow(1, rng_->Chance(0.16));
    }
    next_blink_ = t + rng_->Range(blink_range_[0], blink_range_[1]);
  }
  blinks_.erase(std::remove_if(blinks_.begin(), blinks_.end(),
                               [t](const LensBlink& b) {
                                 return !((t - b.t0) / b.d < 0.3);
                               }),
                blinks_.end());
  flash_v_ *= std::exp(-dt * 3.2);

  const double open_r = T.open_r.value_or(T.open);
  const double tilt_r = T.tilt_r.value_or(T.tilt);
  const double cheek_r = T.cheek_r.value_or(T.cheek);
  const double sc_r = T.sc_r.value_or(T.sc);

  const auto set = [this](LensPose p, double v) {
    pose_[static_cast<int>(p)].t = v;
  };
  set(LensPose::kS, T.s);
  set(LensPose::kK, T.k);
  set(LensPose::kOpen, T.open);
  set(LensPose::kOpenR, open_r);
  set(LensPose::kTilt, T.tilt);
  set(LensPose::kTiltR, tilt_r);
  set(LensPose::kCheek, T.cheek);
  set(LensPose::kCheekR, cheek_r);
  set(LensPose::kSc, T.sc);
  set(LensPose::kScR, sc_r);
  set(LensPose::kEsx, T.esx);
  set(LensPose::kEsy, T.esy);
  set(LensPose::kRot, T.rot);
  set(LensPose::kEyeY, T.eye_y);
  set(LensPose::kGap, T.gap);
  set(LensPose::kHsx, T.hsx);
  set(LensPose::kHsy, T.hsy);
  set(LensPose::kHs, T.hs);
  set(LensPose::kSx, T.sx);
  set(LensPose::kSy, T.sy);
  set(LensPose::kBodyY, T.body_y);
  set(LensPose::kScale, T.scale);
  set(LensPose::kRotB, T.rot_b);
  set(LensPose::kLean, T.lean);
  set(LensPose::kTremor, T.tremor);
  set(LensPose::kEnergy, T.energy);
  set(LensPose::kTintA, T.tint_alpha);
  set(LensPose::kSat, T.sat);
  set(LensPose::kBri, T.bri);
  set(LensPose::kBeamA, T.beam_alpha);

  // Highlight shape weights: one shape, plus a star glint that borrows weight
  // from the bowl.
  std::array<double, kLensHighlightCount> W = {0, 0, 0, 0, 0};
  W[static_cast<int>(T.hl)] = 1;
  if (T.glint > 0) {
    W[static_cast<int>(LensHighlight::kStar)] =
        std::max(W[static_cast<int>(LensHighlight::kStar)], T.glint);
    W[static_cast<int>(LensHighlight::kBowl)] =
        std::min(W[static_cast<int>(LensHighlight::kBowl)], 1 - 0.75 * T.glint);
  }
  for (int i = 0; i < kLensHighlightCount; ++i) {
    hw_[i].t = W[i];
  }

  const double rdt = dt * rate_;
  gx_.Step(dt);
  gy_.Step(dt);
  wf_.Step(dt);
  wo_.Step(dt);
  wp_.Step(dt);
  beam_x_.Step(dt);
  for (LensSpring& spring : pose_) {
    spring.Step(rdt);
  }
  for (LensSpring& spring : hw_) {
    spring.Step(rdt);
  }

  // Colours ease toward their targets.
  const double ek = 1 - std::exp(-dt * 3.2);
  const auto ease = [ek](double cur, double target) {
    return cur + (target - cur) * ek;
  };
  eye_cur_.r = ease(eye_cur_.r, LensLerp(kNeutralEye.r, T.eye_color.r, T.eye_mix));
  eye_cur_.g = ease(eye_cur_.g, LensLerp(kNeutralEye.g, T.eye_color.g, T.eye_mix));
  eye_cur_.b = ease(eye_cur_.b, LensLerp(kNeutralEye.b, T.eye_color.b, T.eye_mix));
  tint_cur_.r = ease(tint_cur_.r, T.tint_color.r);
  tint_cur_.g = ease(tint_cur_.g, T.tint_color.g);
  tint_cur_.b = ease(tint_cur_.b, T.tint_color.b);
  const LensPaletteColors& base = fill_ == LensFill::kOpal
                                      ? LensPaletteFor(LensPalette::kOpal)
                                      : LensPaletteFor(LensPalette::kNeutral);
  const LensPaletteColors& mood =
      T.pal == LensPalette::kNeutral ? base : LensPaletteFor(T.pal);
  for (int j = 0; j < 7; ++j) {
    pal_cur_[j].r = ease(pal_cur_[j].r, LensLerp(base[j].r, mood[j].r, T.pal_mix));
    pal_cur_[j].g = ease(pal_cur_[j].g, LensLerp(base[j].g, mood[j].g, T.pal_mix));
    pal_cur_[j].b = ease(pal_cur_[j].b, LensLerp(base[j].b, mood[j].b, T.pal_mix));
  }
  fluid_phase_ += dt * (0.2 + 1.3 * PoseValue(LensPose::kEnergy)) *
                  (amp_ < 1 ? 0.4 : 1);

  // render() prunes finished tears; done here so ComputeFrame stays const.
  tears_.erase(std::remove_if(tears_.begin(), tears_.end(),
                              [t](const TearDrop& z) { return !(t - z.t0 < 1.5); }),
               tears_.end());
}

LensFrame LensModel::ComputeFrame() const {
  LensFrame f;
  const double t = t_;
  const double amp = amp_;
  const auto P = [this](LensPose p) { return pose_[static_cast<int>(p)].x; };

  f.s = LensClamp(manual_s_ ? *manual_s_ : P(LensPose::kS) * amp, -1.1, 1.1);
  f.u = (f.s + 1) / 2;
  f.k = 1 + (P(LensPose::kK) - 1) * amp;
  f.layers = ComputeLayerOpacities(wf_.x, wo_.x, wp_.x);
  f.sat = P(LensPose::kSat);
  f.bri = P(LensPose::kBri);

  // Body: lean, tilt, squash, tremor.
  const double tr = P(LensPose::kTremor) * amp;
  const double hz = targets_.hz;
  const double jx = tr * (std::sin(t * hz * kLensTau) * 0.6 +
                          std::sin(t * hz * 1.73 * kLensTau + 1.3) * 0.4);
  const double jy = tr * 0.6 * std::sin(t * hz * 1.31 * kLensTau + 0.7);
  const double by = P(LensPose::kBodyY) * amp;
  const double sg = 1 + (P(LensPose::kScale) - 1) * amp;
  f.body_sx = (1 + (P(LensPose::kSx) - 1) * amp) * sg;
  f.body_sy = (1 + (P(LensPose::kSy) - 1) * amp) * sg;
  f.body_tx = kLensCenterX + P(LensPose::kLean) * amp + jx;
  f.body_ty = 330 + by + jy;
  f.body_rot_deg = P(LensPose::kRotB) * amp;
  f.face_rot_deg = -f.s * 5;

  // Eyes.
  const double gx = gx_.x + 0.03 * (std::sin(t * 1.3) + std::sin(t * 2.1 + 1));
  const double gy = gy_.x + 0.03 * (std::sin(t * 1.7 + 2) + std::sin(t * 2.6));
  const double ex = gx * 5.8;
  const double ey = gy * 3.4;
  const std::array<double, 2> blink = {LensBlinksValue(blinks_, t),
                                       LensBlinksValue(blinks_, t - 0.012)};
  for (int i = 0; i < 2; ++i) {
    const bool left = i == 0;
    LensEyeFrame& eye = f.eyes[i];
    const double open = left ? P(LensPose::kOpen) : P(LensPose::kOpenR);
    const double tilt = left ? P(LensPose::kTilt) : P(LensPose::kTiltR);
    const double cheek = left ? P(LensPose::kCheek) : P(LensPose::kCheekR);
    const double sc = left ? P(LensPose::kSc) : P(LensPose::kScR);
    eye.lid = ComputeLid(open, tilt, cheek, blink[i], left);
    const double cx = left ? 95.5 - P(LensPose::kGap) : 154.5 + P(LensPose::kGap);
    eye.tx = cx + ex;
    eye.ty = kLensEyeBaseY + ey + P(LensPose::kEyeY);
    eye.rot_deg = (left ? -1 : 1) * P(LensPose::kRot) * amp;
    eye.sx = sc * P(LensPose::kEsx);
    eye.sy = sc * P(LensPose::kEsy);
    eye.hl_tx = gx * 1.8;
    eye.hl_ty = gy * 1.1;
    const double hs = P(LensPose::kHs);
    for (int h = 0; h < kLensHighlightCount; ++h) {
      const double w = LensClamp(hw_[h].x, 0, 1.3);
      LensEyeFrame::Shape& shape = eye.shapes[h];
      if (w < 0.02) {
        shape.show = false;
        continue;
      }
      shape.show = true;
      switch (static_cast<LensHighlight>(h)) {
        case LensHighlight::kBowl:
          shape.sx = P(LensPose::kHsx) * w * hs;
          shape.sy = P(LensPose::kHsy) * w * hs;
          break;
        case LensHighlight::kStar:
          shape.rot_deg = std::sin(t * 3) * 8;
          shape.sx = shape.sy = w * hs * (1 + 0.1 * std::sin(t * 8));
          break;
        case LensHighlight::kSpiral:
          shape.rot_deg = t * 420;
          shape.sx = shape.sy = w * hs;
          break;
        default:
          shape.sx = shape.sy = w * hs;
          break;
      }
    }
  }
  f.eye_bottom = eye_cur_;
  f.eye_middle = {eye_cur_.r * 0.33, eye_cur_.g * 0.33, eye_cur_.b * 0.33 + 3};

  // Tears (the pool holds at most 12).
  for (const TearDrop& z : tears_) {
    const double age = t - z.t0;
    const bool left = z.side == 0;
    const double cx = (left ? 95.5 - P(LensPose::kGap) : 154.5 + P(LensPose::kGap)) + ex;
    const double form = LensClamp(age / 0.35, 0, 1);
    const double fall = std::max(0.0, age - 0.35);
    LensTearFrame tear;
    tear.y = kLensEyeBaseY + ey + P(LensPose::kEyeY) + 5 + fall * fall * 150 + fall * 8;
    tear.x = cx + (left ? -2 : 2) + z.dx * fall * 6;
    const double sc = LensLerp(0.25, 1, LensEaseOut(form)) *
                      (1 - 0.25 * LensClamp(fall / 1, 0, 1));
    tear.sx = sc;
    tear.sy = sc * (1 + std::min(fall * 1.2, 0.5));
    tear.opacity = age < 1.1 ? 0.95 : std::max(0.0, 0.95 * (1 - (age - 1.1) / 0.4));
    f.tears.push_back(tear);
  }

  // Effects shared by every fill.
  f.tint_opacity = LensClamp(P(LensPose::kTintA) * (1 - 0.4 * LensClamp(wp_.x, 0, 1)), 0, 1);
  f.tint_color = tint_cur_;
  f.beam_opacity = LensClamp(P(LensPose::kBeamA), 0, 1);
  f.beam_x = beam_x_.x;
  f.flash_opacity = LensClamp(flash_v_, 0, 1);
  for (int i = 0; i < 3; ++i) {
    const Ripple& rp = ripples_[i];
    const double age = t - rp.t0;
    LensRippleFrame& out = f.ripples[i];
    if (age < 0 || age > 0.95) {
      continue;
    }
    const double p = age / 0.95;
    out.active = true;
    out.cx = rp.x;
    out.cy = rp.y;
    out.r = LensLerp(6, 150, LensEaseOut(p));
    out.stroke = LensLerp(5, 0.8, p);
    out.opacity = 0.85 * std::pow(1 - p, 1.5);
  }

  // The fluid fill's geometry, only while that layer is visible.
  if (f.layers.show_fluid) {
    LensFluidFrame& fl = f.fluid;
    fl.computed = true;
    fl.palette = pal_cur_;
    const double ph = fluid_phase_;
    const double lvl = LensClamp(P(LensPose::kEnergy), 0, 1);
    for (int i = 0; i < 7; ++i) {
      const LensBlobSpec& b = kBlobs[i];
      const double a = ph * b.w * kLensTau * 0.32 + b.p;
      const double wob = 1 + 0.12 * std::sin(ph * 0.9 + b.p * 2);
      const double pulse = 1 + (b.core ? 0.14 : 0.05) * std::sin(ph * 2.4 + b.p);
      fl.blobs[i] = {kLensCenterX + b.rx * wob * std::cos(a),
                     262 + b.ry * wob * std::sin(a), a * 57.3 + 90,
                     b.st * pulse, pulse / b.st};
    }
    const double flut = 0.78 + 0.22 * std::sin(t * 6.7) * std::sin(t * 2.9 + 1);
    const std::array<std::array<double, 4>, 3> rib = {{{226, 20, 1.3, 1.0}, {298, 24, 1.0, -0.8}, {281, 12, 2.1, 1.7}}};
    for (int i = 0; i < 3; ++i) {
      const double y0 = rib[i][0], A = rib[i][1], kk = rib[i][2], sp = rib[i][3];
      const double ramp = A * (0.2 + 0.8 * lvl * flut) * (amp < 1 ? 0.5 : 1);
      const double phs = ph * sp * 2.2 + i * 1.7;
      for (int n = 0; n <= 30; ++n) {
        const double x = -10 + n * 9.03;
        const double u = (x + 10) / 271;
        const double env = std::sin(kPi * u);
        const double y = y0 + ramp * env *
                                  (std::sin(u * kk * kLensTau + phs) +
                                   0.35 * std::sin(u * kk * 2.3 * kLensTau - phs * 1.4));
        fl.ribbons[i][n] = {x, y};
      }
    }
    fl.rim_rotation_deg = ph * 46;
    fl.rim_glow_opacity = 0.4 + 0.4 * lvl;
    const LensBodyControls c = ComputeBodyControls(f.u, f.k);
    const LensPoint p1 = CubicAt(c.b, 0.5);
    const LensPoint p0 = CubicAt(c.b, 0.46);
    const LensPoint p2 = CubicAt(c.b, 0.54);
    fl.gloss_tx = p1.x;
    fl.gloss_ty = p1.y + 14;
    fl.gloss_rot_deg = std::atan2(p2.y - p0.y, p2.x - p0.x) * 57.3;
    fl.blobs_scale = 1 + 0.035 * std::sin(t * 2.1 * (0.6 + lvl));
  }
  return f;
}

}  // namespace zephyrus::agent
