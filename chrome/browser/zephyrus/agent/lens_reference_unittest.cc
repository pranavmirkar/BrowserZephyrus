// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Milestone 2 parity: the C++ lens model against the PROTOTYPE'S OWN code.
//
// testdata/lens_reference.json was produced by loading the Mascot class out of
// agent-mascot.html under node, with a seeded Math.random, driving update() and
// render() by hand at 60 fps for 6 seconds per case, and recording its springs,
// gaze, blinks, tears, ripples and the numbers render() wrote (see
// docs/mascot-lens-plan.md for how to regenerate). Every prototype state is
// covered in Static, eight of them in Fluid, and six emotions at intensity 0.5.
//
// The model has to reproduce those numbers from the same seed, which means the
// same random draws in the same order. A failure here is a behavioural
// difference from the prototype, and the message names the state, the frame and
// the field.

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/zephyrus/agent/lens_model.h"
#include "chrome/browser/zephyrus/agent/lens_random.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

base::DictValue LoadReference() {
  base::FilePath root;
  EXPECT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  const base::FilePath path = root.AppendASCII("chrome")
                                  .AppendASCII("browser")
                                  .AppendASCII("zephyrus")
                                  .AppendASCII("agent")
                                  .AppendASCII("testdata")
                                  .AppendASCII("lens_reference.json");
  std::string text;
  EXPECT_TRUE(base::ReadFileToString(path, &text)) << path;
  std::optional<base::Value> parsed = base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  EXPECT_TRUE(parsed && parsed->is_dict());
  return parsed && parsed->is_dict() ? std::move(parsed->GetDict())
                                     : base::DictValue();
}

// JSON numbers may arrive as int or double.
double Num(const base::Value& v) {
  return v.GetDouble();
}
double NumAt(const base::ListValue& l, size_t i) {
  return i < l.size() ? Num(l[i]) : std::nan("");
}

// The prototype keeps seven digits of state it prints with ten significant
// figures; allow for that and for the accumulated difference of two double
// implementations over 360 steps.
bool Near(double got, double want, double tol) {
  return std::abs(got - want) <= tol + 1e-7 * std::abs(want);
}

struct Fail {
  std::string where;
};

class Comparer {
 public:
  Comparer(const std::string& label) : label_(label) {}

  void Check(const char* field, double got, double want, double tol) {
    if (!Near(got, want, tol)) {
      Record(field, got, want);
    }
  }
  void Record(const char* field, double got, double want) {
    if (failures_ < 12) {
      ADD_FAILURE() << label_ << " " << field << ": got " << got << ", want "
                    << want;
    }
    ++failures_;
  }
  void Set(const std::string& label) { label_ = label; }
  int failures() const { return failures_; }

 private:
  std::string label_;
  int failures_ = 0;
};

LensFill FillFor(const std::string& mode) {
  return mode == "fluid" ? LensFill::kFluid : LensFill::kStatic;
}

TEST(LensReferenceTest, EveryPrototypeStateMatchesThePrototypeFrameByFrame) {
  const base::DictValue ref = LoadReference();
  const base::ListValue* cases = ref.FindList("cases");
  ASSERT_TRUE(cases);
  ASSERT_GE(cases->size(), 40u);
  const double fps = ref.FindDouble("fps").value_or(60);
  const double dt = 1.0 / fps;
  const int total = static_cast<int>(std::lround(ref.FindDouble("seconds").value_or(6) * fps));

  static constexpr struct {
    const char* key;
    LensPose pose;
  } kPoses[] = {
      {"s", LensPose::kS},           {"k", LensPose::kK},
      {"open", LensPose::kOpen},     {"openR", LensPose::kOpenR},
      {"tilt", LensPose::kTilt},     {"tiltR", LensPose::kTiltR},
      {"cheek", LensPose::kCheek},   {"cheekR", LensPose::kCheekR},
      {"sc", LensPose::kSc},         {"scR", LensPose::kScR},
      {"esx", LensPose::kEsx},       {"esy", LensPose::kEsy},
      {"rot", LensPose::kRot},       {"eyeY", LensPose::kEyeY},
      {"gap", LensPose::kGap},       {"hsx", LensPose::kHsx},
      {"hsy", LensPose::kHsy},       {"hs", LensPose::kHs},
      {"sx", LensPose::kSx},         {"sy", LensPose::kSy},
      {"bodyY", LensPose::kBodyY},   {"scale", LensPose::kScale},
      {"rotB", LensPose::kRotB},     {"lean", LensPose::kLean},
      {"tremor", LensPose::kTremor}, {"energy", LensPose::kEnergy},
      {"tintA", LensPose::kTintA},   {"sat", LensPose::kSat},
      {"bri", LensPose::kBri},       {"beamA", LensPose::kBeamA},
  };

  int compared_frames = 0;
  for (const base::Value& entry : *cases) {
    const base::DictValue& c = entry.GetDict();
    const std::string state_key = *c.FindString("state");
    const std::string mode = *c.FindString("mode");
    const double intensity = *c.FindDouble("intensity");
    const uint32_t seed = static_cast<uint32_t>(*c.FindInt("seed"));
    const std::optional<LensState> state = LensStateFromKey(state_key);
    ASSERT_TRUE(state.has_value()) << state_key;
    ASSERT_LT(static_cast<int>(*state), kLensPrototypeStateCount) << state_key;

    // Start from a neutral pose with a fresh seeded generator, as the harness
    // does, so the construction-time draws do not shift the sequence.
    SeededLensRandom throwaway(0);
    LensModel model(&throwaway);
    SeededLensRandom rng(seed);
    model.SetRandom(&rng);
    model.Reset();
    model.SetFill(FillFor(mode));
    model.SetIntensity(intensity);
    model.SetState(*state, /*force=*/true);

    const base::ListValue& frames = *c.FindList("frames");
    // The sampled frame indices are recorded by their time; walk the 360 steps
    // and compare when the recorded time is reached.
    size_t next_frame = 0;
    const std::string label = state_key + "/" + mode + "/" +
                              base::NumberToString(intensity) + " seed " +
                              base::NumberToString(seed);
    Comparer cmp(label);
    for (int i = 0; i < total && next_frame < frames.size(); ++i) {
      model.Update(dt);
      const base::DictValue& fd = frames[next_frame].GetDict();
      if (!Near(model.time(), *fd.FindDouble("t"), 1e-9)) {
        continue;
      }
      ++next_frame;
      ++compared_frames;
      cmp.Set(label + " @t=" + base::NumberToString(model.time()));

      // ---- internal state ----
      for (const auto& p : kPoses) {
        cmp.Check(p.key, model.PoseValue(p.pose), *fd.FindDouble(p.key), 1e-6);
      }
      cmp.Check("gx", model.GazeX(), *fd.FindDouble("gx"), 1e-6);
      cmp.Check("gy", model.GazeY(), *fd.FindDouble("gy"), 1e-6);
      cmp.Check("wF", model.LayerWeightFluid(), *fd.FindDouble("wF"), 1e-6);
      const base::ListValue& hw = *fd.FindList("hw");
      for (int h = 0; h < kLensHighlightCount; ++h) {
        cmp.Check("hw", model.HighlightWeight(static_cast<LensHighlight>(h)),
                  Num(hw[h]), 1e-6);
      }
      const base::ListValue& G = *fd.FindList("G");
      cmp.Check("G.x", model.GazeTargetX(), Num(G[0]), 1e-9);
      cmp.Check("G.y", model.GazeTargetY(), Num(G[1]), 1e-9);
      const base::ListValue& eye = *fd.FindList("eye");
      cmp.Check("eye.r", model.eye_color().r, Num(eye[0]), 1e-6);
      cmp.Check("eye.g", model.eye_color().g, Num(eye[1]), 1e-6);
      cmp.Check("eye.b", model.eye_color().b, Num(eye[2]), 1e-6);
      const base::ListValue& tint = *fd.FindList("tint");
      cmp.Check("tint.r", model.tint_color().r, Num(tint[0]), 1e-6);
      cmp.Check("tint.g", model.tint_color().g, Num(tint[1]), 1e-6);
      cmp.Check("tint.b", model.tint_color().b, Num(tint[2]), 1e-6);
      cmp.Check("flash", model.flash(), *fd.FindDouble("flash"), 1e-6);
      cmp.Check("fluidPhase", model.fluid_phase(), *fd.FindDouble("fluidPhase"), 1e-6);
      cmp.Check("nextBlink", model.next_blink_time(), *fd.FindDouble("nextBlink"), 1e-9);
      cmp.Check("rate", model.Rate(), *fd.FindDouble("rate"), 1e-12);

      const base::ListValue& blinks = *fd.FindList("blinks");
      if (blinks.size() != model.blinks().size()) {
        cmp.Record("blink count", model.blinks().size(), blinks.size());
      } else {
        for (size_t b = 0; b < blinks.size(); ++b) {
          const base::ListValue& jb = blinks[b].GetList();
          cmp.Check("blink.t0", model.blinks()[b].t0, Num(jb[0]), 1e-9);
          cmp.Check("blink.d", model.blinks()[b].d, Num(jb[1]), 1e-9);
        }
      }
      const base::ListValue& tears = *fd.FindList("tears");
      if (tears.size() != model.tears().size()) {
        cmp.Record("tear count", model.tears().size(), tears.size());
      } else {
        for (size_t z = 0; z < tears.size(); ++z) {
          const base::ListValue& jz = tears[z].GetList();
          cmp.Check("tear.t0", model.tears()[z].t0, Num(jz[0]), 1e-9);
          cmp.Check("tear.side", model.tears()[z].side, Num(jz[1]), 0);
          cmp.Check("tear.dx", model.tears()[z].dx, Num(jz[2]), 1e-9);
        }
      }
      const base::ListValue& ripples = *fd.FindList("ripples");
      for (size_t r = 0; r < 3; ++r) {
        const base::ListValue& jr = ripples[r].GetList();
        cmp.Check("ripple.t0", model.ripples()[r].t0, Num(jr[0]), 1e-9);
      }
      if (mode == "fluid") {
        const base::ListValue& pal = *fd.FindList("pal");
        for (int j = 0; j < 7; ++j) {
          const base::ListValue& jc = pal[j].GetList();
          cmp.Check("pal.r", model.palette()[j].r, Num(jc[0]), 1e-5);
          cmp.Check("pal.g", model.palette()[j].g, Num(jc[1]), 1e-5);
          cmp.Check("pal.b", model.palette()[j].b, Num(jc[2]), 1e-5);
        }
      }

      // ---- what the painter is given, against what the prototype wrote ----
      // The prototype rounds to two decimals (three for scales), so these
      // allow for that rounding and nothing more.
      const base::DictValue& rd = *fd.FindDict("render");
      const LensFrame f = model.ComputeFrame();
      constexpr double kR = 0.011;
      const base::ListValue& body = *rd.FindList("body");
      cmp.Check("body.tx", f.body_tx, NumAt(body, 0), kR);
      cmp.Check("body.ty", f.body_ty, NumAt(body, 1), kR);
      cmp.Check("body.rot", f.body_rot_deg, NumAt(body, 2), kR);
      cmp.Check("body.sx", f.body_sx, NumAt(body, 3), 0.0011);
      cmp.Check("body.sy", f.body_sy, NumAt(body, 4), 0.0011);
      cmp.Check("face.rot", f.face_rot_deg, NumAt(*rd.FindList("face"), 0), kR);
      const std::array<const char*, 2> eye_keys = {"eyeL", "eyeR"};
      const std::array<const char*, 2> lid_keys = {"lidL", "lidR"};
      for (int e = 0; e < 2; ++e) {
        const base::ListValue& ev = *rd.FindList(eye_keys[e]);
        cmp.Check("eye.tx", f.eyes[e].tx, NumAt(ev, 0), kR);
        cmp.Check("eye.ty", f.eyes[e].ty, NumAt(ev, 1), kR);
        cmp.Check("eye.rot", f.eyes[e].rot_deg, NumAt(ev, 2), kR);
        cmp.Check("eye.sx", f.eyes[e].sx, NumAt(ev, 5), kR);
        cmp.Check("eye.sy", f.eyes[e].sy, NumAt(ev, 6), kR);
        const base::ListValue& lv = *rd.FindList(lid_keys[e]);
        cmp.Check("lid.a", f.eyes[e].lid.a, NumAt(lv, 1), kR);
        cmp.Check("lid.b", f.eyes[e].lid.b, NumAt(lv, 3), kR);
        cmp.Check("lid.yb", f.eyes[e].lid.yb, NumAt(lv, 5), kR);
        cmp.Check("lid.yc", f.eyes[e].lid.yc, NumAt(lv, 7), kR);
        const base::DictValue& hl = (*rd.FindList("hl"))[e].GetDict();
        const base::ListValue& root = *hl.FindList("root");
        cmp.Check("hl.tx", f.eyes[e].hl_tx, NumAt(root, 0), kR);
        cmp.Check("hl.ty", f.eyes[e].hl_ty, NumAt(root, 1), kR);
        const base::ListValue& shapes = *hl.FindList("shapes");
        for (int h = 0; h < kLensHighlightCount; ++h) {
          const base::DictValue& js = shapes[h].GetDict();
          const bool show = *js.FindBool("show");
          if (show != f.eyes[e].shapes[h].show) {
            cmp.Record("hl.show", f.eyes[e].shapes[h].show, show);
            continue;
          }
          if (!show) {
            continue;
          }
          const base::ListValue& tf = *js.FindList("tf");
          switch (static_cast<LensHighlight>(h)) {
            case LensHighlight::kBowl:
              cmp.Check("hl.bowl.sx", f.eyes[e].shapes[h].sx, NumAt(tf, 0), kR);
              cmp.Check("hl.bowl.sy", f.eyes[e].shapes[h].sy, NumAt(tf, 1), kR);
              break;
            case LensHighlight::kStar:
            case LensHighlight::kSpiral:
              cmp.Check("hl.rot", f.eyes[e].shapes[h].rot_deg, NumAt(tf, 0), kR);
              cmp.Check("hl.scale", f.eyes[e].shapes[h].sx, NumAt(tf, 1), kR);
              break;
            default:
              cmp.Check("hl.scale", f.eyes[e].shapes[h].sx, NumAt(tf, 0), kR);
              break;
          }
        }
      }
      const base::ListValue& bot = *rd.FindList("eyeBot");
      const base::ListValue& mid = *rd.FindList("eyeMid");
      cmp.Check("eyeBot.r", f.eye_bottom.r, NumAt(bot, 0), 0.51);
      cmp.Check("eyeBot.g", f.eye_bottom.g, NumAt(bot, 1), 0.51);
      cmp.Check("eyeBot.b", f.eye_bottom.b, NumAt(bot, 2), 0.51);
      cmp.Check("eyeMid.r", f.eye_middle.r, NumAt(mid, 0), 0.51);
      cmp.Check("eyeMid.g", f.eye_middle.g, NumAt(mid, 1), 0.51);
      cmp.Check("eyeMid.b", f.eye_middle.b, NumAt(mid, 2), 0.51);

      const base::ListValue& jtears = *rd.FindList("tears");
      if (jtears.size() != f.tears.size()) {
        cmp.Record("render tear count", f.tears.size(), jtears.size());
      } else {
        for (size_t z = 0; z < jtears.size(); ++z) {
          const base::DictValue& jt = jtears[z].GetDict();
          const base::ListValue& tf = *jt.FindList("tf");
          cmp.Check("tear.x", f.tears[z].x, NumAt(tf, 0), kR);
          cmp.Check("tear.y", f.tears[z].y, NumAt(tf, 1), kR);
          cmp.Check("tear.sx", f.tears[z].sx, NumAt(tf, 2), kR);
          cmp.Check("tear.sy", f.tears[z].sy, NumAt(tf, 3), kR);
          cmp.Check("tear.op", f.tears[z].opacity, *jt.FindDouble("op"), 0.011);
        }
      }
      cmp.Check("tint.op", f.tint_opacity, *rd.FindDouble("tintOp"), 0.0011);
      cmp.Check("beam.op", f.beam_opacity, *rd.FindDouble("beamOp"), 0.0011);
      cmp.Check("beam.x", f.beam_x, NumAt(*rd.FindList("beamTf"), 0), kR);
      cmp.Check("flash.op", f.flash_opacity, *rd.FindDouble("flash"), 0.0011);
      const base::ListValue& rips = *rd.FindList("rips");
      for (size_t r = 0; r < 3; ++r) {
        const base::DictValue& jr = rips[r].GetDict();
        const double op = *jr.FindDouble("op");
        if ((op > 0) != f.ripples[r].active) {
          // The prototype writes opacity 0 for an inactive one.
          if (!(op == 0 && !f.ripples[r].active)) {
            cmp.Record("ripple active", f.ripples[r].active, op > 0);
          }
          continue;
        }
        if (f.ripples[r].active) {
          cmp.Check("rip.cx", f.ripples[r].cx, *jr.FindDouble("cx"), kR);
          cmp.Check("rip.cy", f.ripples[r].cy, *jr.FindDouble("cy"), kR);
          cmp.Check("rip.r", f.ripples[r].r, *jr.FindDouble("r"), kR);
          cmp.Check("rip.sw", f.ripples[r].stroke, *jr.FindDouble("sw"), kR);
          cmp.Check("rip.op", f.ripples[r].opacity, op, 0.0011);
        }
      }
      const base::DictValue& layers = *rd.FindDict("layers");
      EXPECT_EQ(f.layers.show_static, *layers.FindString("s") == "inline")
          << cmp.failures();
      EXPECT_EQ(f.layers.show_fluid, *layers.FindString("f") == "inline");
      if (f.layers.show_fluid) {
        cmp.Check("fluid.opacity", f.layers.fluid, *layers.FindDouble("fo"), 0.0011);
      }
      if (f.layers.show_fluid) {
        ASSERT_TRUE(f.fluid.computed);
        const base::ListValue& blobs = *rd.FindList("blobs");
        for (size_t b = 0; b < 7; ++b) {
          const base::ListValue& tf = blobs[b].GetList();
          cmp.Check("blob.tx", f.fluid.blobs[b].tx, NumAt(tf, 0), kR);
          cmp.Check("blob.ty", f.fluid.blobs[b].ty, NumAt(tf, 1), kR);
          cmp.Check("blob.rot", f.fluid.blobs[b].rot_deg, NumAt(tf, 2), kR);
          cmp.Check("blob.sx", f.fluid.blobs[b].sx, NumAt(tf, 3), kR);
          cmp.Check("blob.sy", f.fluid.blobs[b].sy, NumAt(tf, 4), kR);
        }
        const base::ListValue& ribs = *rd.FindList("ribs");
        for (size_t r = 0; r < 3; ++r) {
          const base::ListValue& pts = ribs[r].GetList();
          ASSERT_EQ(pts.size(), 62u);
          for (size_t n = 0; n < 31; ++n) {
            cmp.Check("rib.x", f.fluid.ribbons[r][n].x, Num(pts[2 * n]), kR);
            cmp.Check("rib.y", f.fluid.ribbons[r][n].y, Num(pts[2 * n + 1]), kR);
          }
        }
        cmp.Check("rim.rot", f.fluid.rim_rotation_deg,
                  NumAt(*rd.FindList("rimRot"), 0), kR);
        cmp.Check("rim.op", f.fluid.rim_glow_opacity, *rd.FindDouble("rimOp"), 0.011);
        const base::ListValue& gloss = *rd.FindList("gloss");
        cmp.Check("gloss.tx", f.fluid.gloss_tx, NumAt(gloss, 0), kR);
        cmp.Check("gloss.ty", f.fluid.gloss_ty, NumAt(gloss, 1), kR);
        cmp.Check("gloss.rot", f.fluid.gloss_rot_deg, NumAt(gloss, 2), kR);
        cmp.Check("wrap.scale", f.fluid.blobs_scale, NumAt(*rd.FindList("wrap"), 2), kR);
      }
    }
    EXPECT_EQ(next_frame, frames.size())
        << label << ": the recorded sample times were not all reached";
    EXPECT_EQ(cmp.failures(), 0) << label;
  }
  EXPECT_GE(compared_frames, 400);
}

}  // namespace
}  // namespace zephyrus::agent
