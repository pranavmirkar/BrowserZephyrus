// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/voice_lock.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <string_view>

namespace zephyrus::agent::voice {

namespace {

constexpr float kMinWakeThreshold = 0.22f;
// Real microphones (noise-cancelling, gated, boosted) make one person's
// recordings of a phrase sit 0.33-0.40 apart, where studio-clean speech sits
// 0.12-0.17. The ceiling has to let such a voice in; the speaker gate and the
// per-voice calibration below keep a clean voice's threshold low.
constexpr float kMaxWakeThreshold = 0.42f;
// How far a new take may sit from the earlier ones before it is another phrase.
constexpr float kSamePhraseDistance = 0.44f;
constexpr float kMinSpeakerThreshold = 3.0f;
constexpr float kMaxSpeakerThreshold = 5.0f;
// How far a command's pitch may sit from the voice's, in octaves.
constexpr float kPitchTolerance = 0.35f;

constexpr size_t kMinPhraseFrames = 25;   // 0.25 s
constexpr size_t kMaxPhraseFrames = 200;  // 2 s
constexpr size_t kMaxFramesStored = 300;

// The spread a voice is allowed before a difference counts, per dimension: a
// person does not sound identical twice, and five recordings taken a minute
// apart underestimate how much they vary. Wide for the cepstra, narrower for
// their spread, and the pitch a few percent.
float SpreadFloor(int dim) {
  if (dim < kCepstra) {
    return 1.0f;
  }
  if (dim < 2 * kCepstra) {
    return 0.6f;
  }
  return 0.10f;
}
float SpeakerWeight(int dim) {
  if (dim < kCepstra) {
    return 1.0f;
  }
  if (dim < 2 * kCepstra) {
    return 0.5f;
  }
  return 3.0f;
}

float SpeakerDistance(const SpeakerVector& v, const VoiceProfile& profile) {
  float sum = 0.0f;
  float weights = 0.0f;
  for (int d = 0; d < kSpeakerDim; ++d) {
    const bool is_pitch = d == 2 * kCepstra;
    if (is_pitch && (v[d] == 0.0f || profile.speaker_mean[d] == 0.0f)) {
      continue;  // no pitch to compare: judge on the rest
    }
    const float sigma = std::max(profile.speaker_spread[d], SpreadFloor(d));
    const float z = (v[d] - profile.speaker_mean[d]) / sigma;
    sum += SpeakerWeight(d) * z * z;
    weights += SpeakerWeight(d);
  }
  return weights > 0.0f ? std::sqrt(sum / weights) : 99.0f;
}

// ---- Serialisation -----------------------------------------------------------

constexpr char kMagic[] = "ZVP1";

void PutU32(std::string* out, uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    out->push_back(static_cast<char>((v >> (8 * i)) & 0xff));
  }
}
void PutI64(std::string* out, int64_t v) {
  const uint64_t u = static_cast<uint64_t>(v);
  for (int i = 0; i < 8; ++i) {
    out->push_back(static_cast<char>((u >> (8 * i)) & 0xff));
  }
}
void PutFloat(std::string* out, float f) {
  PutU32(out, std::bit_cast<uint32_t>(f));
}
void PutString(std::string* out, const std::string& s) {
  PutU32(out, static_cast<uint32_t>(s.size()));
  out->append(s);
}

class Reader {
 public:
  explicit Reader(const std::string& bytes) : bytes_(bytes) {}
  bool ok() const { return ok_; }
  bool done() const { return pos_ == bytes_.size(); }

  uint32_t U32() {
    if (!Have(4)) {
      return 0;
    }
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      v |= static_cast<uint32_t>(static_cast<uint8_t>(bytes_[pos_ + i]))
           << (8 * i);
    }
    pos_ += 4;
    return v;
  }
  int64_t I64() {
    if (!Have(8)) {
      return 0;
    }
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
      v |= static_cast<uint64_t>(static_cast<uint8_t>(bytes_[pos_ + i]))
           << (8 * i);
    }
    pos_ += 8;
    return static_cast<int64_t>(v);
  }
  float Float() {
    const float f = std::bit_cast<float>(U32());
    if (!std::isfinite(f)) {
      ok_ = false;
      return 0.0f;
    }
    return f;
  }
  std::string String(size_t max) {
    const uint32_t n = U32();
    if (!ok_ || n > max || !Have(n)) {
      ok_ = false;
      return std::string();
    }
    std::string s = bytes_.substr(pos_, n);
    pos_ += n;
    return s;
  }

 private:
  bool Have(size_t n) {
    if (!ok_ || bytes_.size() - pos_ < n) {
      ok_ = false;
      return false;
    }
    return true;
  }

  const std::string& bytes_;
  size_t pos_ = 0;
  bool ok_ = true;
};

}  // namespace

float ThresholdScale(Sensitivity sensitivity) {
  switch (sensitivity) {
    case Sensitivity::kStrict:
      return 0.85f;
    case Sensitivity::kBalanced:
      return 1.0f;
    case Sensitivity::kRelaxed:
      return 1.2f;
  }
  return 1.0f;
}

WakeTemplate::WakeTemplate() = default;
WakeTemplate::WakeTemplate(const WakeTemplate&) = default;
WakeTemplate::WakeTemplate(WakeTemplate&&) = default;
WakeTemplate& WakeTemplate::operator=(const WakeTemplate&) = default;
WakeTemplate& WakeTemplate::operator=(WakeTemplate&&) = default;
WakeTemplate::~WakeTemplate() = default;

VoiceProfile::VoiceProfile() = default;
VoiceProfile::VoiceProfile(const VoiceProfile&) = default;
VoiceProfile::VoiceProfile(VoiceProfile&&) = default;
VoiceProfile& VoiceProfile::operator=(const VoiceProfile&) = default;
VoiceProfile& VoiceProfile::operator=(VoiceProfile&&) = default;
VoiceProfile::~VoiceProfile() = default;

SampleCheck::SampleCheck() = default;
SampleCheck::SampleCheck(SampleCheck&&) = default;
SampleCheck& SampleCheck::operator=(SampleCheck&&) = default;
SampleCheck::~SampleCheck() = default;

bool VoiceProfile::Prepare() {
  prepared.clear();
  if (templates.size() < 2) {
    return false;
  }
  for (const WakeTemplate& t : templates) {
    if (t.frames.size() < kMinPhraseFrames) {
      return false;
    }
    prepared.push_back(NormalisedFeatures(t.frames));
  }

  // How far apart are the recordings of the same phrase? The threshold is the
  // spread among them, plus room: five taken one after another underestimate
  // how much the same person varies from one day to the next.
  std::vector<float> pair;
  for (size_t i = 0; i < prepared.size(); ++i) {
    for (size_t j = 0; j < prepared.size(); ++j) {
      if (i != j) {
        pair.push_back(OpenEndDtw(prepared[i], prepared[j]).distance);
      }
    }
  }
  float mean = 0.0f;
  for (float d : pair) {
    mean += d;
  }
  mean /= static_cast<float>(pair.size());
  float var = 0.0f;
  for (float d : pair) {
    var += (d - mean) * (d - mean);
  }
  const float spread = std::sqrt(var / static_cast<float>(pair.size()));
  wake_threshold =
      std::clamp(mean + 3.0f * spread, kMinWakeThreshold, kMaxWakeThreshold);

  // The voice: mean and spread of each speaker feature over the recordings.
  const float count = static_cast<float>(templates.size());
  for (int d = 0; d < kSpeakerDim; ++d) {
    float sum = 0.0f;
    float n = 0.0f;
    for (const WakeTemplate& t : templates) {
      if (d == 2 * kCepstra && t.speaker[d] == 0.0f) {
        continue;  // unvoiced: no pitch from this one
      }
      sum += t.speaker[d];
      n += 1.0f;
    }
    speaker_mean[d] = n > 0.0f ? sum / n : 0.0f;
    float v = 0.0f;
    for (const WakeTemplate& t : templates) {
      if (d == 2 * kCepstra && t.speaker[d] == 0.0f) {
        continue;
      }
      v += (t.speaker[d] - speaker_mean[d]) * (t.speaker[d] - speaker_mean[d]);
    }
    speaker_spread[d] = n > 0.0f ? std::sqrt(v / n) : 0.0f;
  }
  pitch_mean = speaker_mean[2 * kCepstra];
  float worst = 0.0f;
  for (const WakeTemplate& t : templates) {
    worst = std::max(worst, SpeakerDistance(t.speaker, *this));
  }
  speaker_threshold = std::clamp(worst * 1.5f + 1.0f, kMinSpeakerThreshold,
                                 kMaxSpeakerThreshold);
  (void)count;
  return true;
}

std::string DescribeProblem(SampleProblem problem) {
  switch (problem) {
    case SampleProblem::kNone:
      return std::string();
    case SampleProblem::kTooShort:
      return "That was too short. Say \"Hey Zep\" clearly.";
    case SampleProblem::kTooLong:
      return "That ran long, or the room is noisy. Say just \"Hey Zep\", or pick another microphone.";
    case SampleProblem::kTooQuiet:
      return "I could barely hear that. Move closer, or pick another microphone.";
    case SampleProblem::kNotThePhrase:
      return "That did not sound like the earlier ones. Say \"Hey Zep\" the "
             "same way.";
  }
  return std::string();
}

SampleCheck AnalyseEnrollmentSample(base::span<const int16_t> pcm,
                                    base::span<const WakeTemplate> accepted) {
  SampleCheck check;
  const std::vector<float> samples = PcmToFloat(pcm);
  const Frames frames = ExtractFrames(samples);
  if (frames.features.size() < kMinPhraseFrames) {
    check.problem = SampleProblem::kTooShort;
    return check;
  }
  // Loud enough over the room: the peak against the quietest tenth of frames.
  std::vector<float> sorted = frames.energy_db;
  std::sort(sorted.begin(), sorted.end());
  const float floor_db = sorted[sorted.size() / 10];
  const float peak_db = sorted.back();
  if (peak_db < -45.0f || peak_db - floor_db < 9.0f) {
    check.problem = SampleProblem::kTooQuiet;
    return check;
  }
  const auto [first, last] = SpeechSpan(
      frames.energy_db, 25.0f, -60.0f, /*phrase_only=*/true);
  const size_t length = last > first ? last - first : 0;
  if (length < kMinPhraseFrames) {
    check.problem = SampleProblem::kTooShort;
    return check;
  }
  if (length > kMaxPhraseFrames) {
    check.problem = SampleProblem::kTooLong;
    return check;
  }
  check.sample.frames.assign(frames.features.begin() + first,
                             frames.features.begin() + last);
  check.sample.speaker = ComputeSpeakerVector(samples, frames, first, last);

  if (!accepted.empty()) {
    // A word is not a phrase: "Zep" on its own is a good deal shorter than "Hey
    // Zep", and how long the phrase takes is the one thing a person repeats
    // steadily even when a noisy microphone changes how it sounds.
    std::vector<size_t> lengths;
    for (const WakeTemplate& earlier : accepted) {
      lengths.push_back(earlier.frames.size());
    }
    std::sort(lengths.begin(), lengths.end());
    const float typical = static_cast<float>(lengths[lengths.size() / 2]);
    const float mine_length = static_cast<float>(length);
    if (mine_length < 0.70f * typical || mine_length > 1.6f * typical) {
      check.problem = SampleProblem::kNotThePhrase;
      return check;
    }
  }

  if (!accepted.empty()) {
    const std::vector<FeatureFrame> mine =
        NormalisedFeatures(check.sample.frames);
    std::vector<float> distances;
    for (const WakeTemplate& earlier : accepted) {
      distances.push_back(
          OpenEndDtw(NormalisedFeatures(earlier.frames), mine).distance);
    }
    std::sort(distances.begin(), distances.end());
    if (distances[distances.size() / 2] > kSamePhraseDistance) {
      check.problem = SampleProblem::kNotThePhrase;
    }
  }
  return check;
}

MatchResult MatchWakePhrase(base::span<const VoiceProfile> profiles,
                            base::span<const float> samples,
                            const Frames& frames,
                            size_t first,
                            bool lock,
                            Sensitivity sensitivity) {
  MatchResult best;
  const float scale = ThresholdScale(sensitivity);
  const size_t available =
      frames.features.size() > first ? frames.features.size() - first : 0;
  float best_score = 1e9f;
  for (size_t p = 0; p < profiles.size(); ++p) {
    const VoiceProfile& profile = profiles[p];
    if (profile.prepared.empty()) {
      continue;
    }
    // The closest of this voice's recordings.
    float distance = 9.0f;
    size_t end = 0;
    for (const std::vector<FeatureFrame>& tmpl : profile.prepared) {
      const size_t m = tmpl.size();
      const size_t window =
          std::min(available, static_cast<size_t>(2.2f * static_cast<float>(m)));
      if (window < static_cast<size_t>(0.6f * static_cast<float>(m))) {
        continue;  // not enough spoken yet to hold the phrase
      }
      const std::vector<FeatureFrame> query = NormalisedFeatures(
          base::span(frames.features).subspan(first, window));
      const DtwResult r = OpenEndDtw(tmpl, query);
      if (r.distance < distance) {
        distance = r.distance;
        end = r.end;
      }
    }
    const float wake_limit = profile.wake_threshold * scale;
    const bool wake = distance < wake_limit;

    float speaker = 99.0f;
    bool voice_ok = wake && !lock;
    if (wake && lock) {
      const SpeakerVector v =
          ComputeSpeakerVector(samples, frames, first, first + end);
      speaker = SpeakerDistance(v, profile);
      voice_ok = speaker <= profile.speaker_threshold * scale;
    }

    // Best voice: one that passes outright beats one that does not; then the
    // closest by both measures together.
    const float score = (voice_ok ? 0.0f : 100.0f) +
                        distance / wake_limit +
                        (lock && speaker < 99.0f
                             ? speaker / (profile.speaker_threshold * scale)
                             : 0.0f);
    if (score < best_score) {
      best_score = score;
      best.wake = wake;
      best.voice_ok = voice_ok;
      best.profile = static_cast<int>(p);
      best.wake_distance = distance;
      best.speaker_distance = speaker;
      best.end_frame = first + end;
    }
  }
  return best;
}

bool PitchMatches(const VoiceProfile& profile,
                  base::span<const float> samples,
                  const Frames& frames,
                  size_t first,
                  size_t last) {
  if (profile.pitch_mean == 0.0f) {
    return true;  // this voice has no pitch on record: nothing to compare
  }
  const SpeakerVector v = ComputeSpeakerVector(samples, frames, first, last);
  const float pitch = v[2 * kCepstra];
  if (pitch == 0.0f) {
    return false;
  }
  return std::abs(pitch - profile.pitch_mean) <= kPitchTolerance;
}

std::string SerialiseProfiles(base::span<const VoiceProfile> profiles) {
  std::string out(kMagic, 4);
  PutU32(&out, static_cast<uint32_t>(profiles.size()));
  for (const VoiceProfile& p : profiles) {
    PutString(&out, p.id);
    PutString(&out, p.name);
    PutI64(&out, p.created_unix);
    PutU32(&out, static_cast<uint32_t>(p.templates.size()));
    for (const WakeTemplate& t : p.templates) {
      PutU32(&out, static_cast<uint32_t>(t.frames.size()));
      for (const StaticFrame& f : t.frames) {
        for (float x : f) {
          PutFloat(&out, x);
        }
      }
      for (float x : t.speaker) {
        PutFloat(&out, x);
      }
    }
  }
  return out;
}

std::optional<std::vector<VoiceProfile>> ParseProfiles(
    const std::string& bytes) {
  if (bytes.size() < 8 || std::string_view(bytes).substr(0, 4) != "ZVP1") {
    return std::nullopt;
  }
  Reader in(bytes);
  in.U32();  // the magic
  const uint32_t count = in.U32();
  if (!in.ok() || count > kMaxProfiles) {
    return std::nullopt;
  }
  std::vector<VoiceProfile> profiles;
  for (uint32_t i = 0; i < count; ++i) {
    VoiceProfile p;
    p.id = in.String(64);
    p.name = in.String(kMaxNameLength * 4);  // UTF-8
    p.created_unix = in.I64();
    const uint32_t templates = in.U32();
    if (!in.ok() || templates > kMaxTemplates) {
      return std::nullopt;
    }
    for (uint32_t t = 0; t < templates; ++t) {
      WakeTemplate tmpl;
      const uint32_t frames = in.U32();
      if (!in.ok() || frames > kMaxFramesStored) {
        return std::nullopt;
      }
      tmpl.frames.resize(frames);
      for (StaticFrame& f : tmpl.frames) {
        for (float& x : f) {
          x = in.Float();
        }
      }
      for (float& x : tmpl.speaker) {
        x = in.Float();
      }
      if (!in.ok()) {
        return std::nullopt;
      }
      p.templates.push_back(std::move(tmpl));
    }
    if (p.id.empty() || p.name.empty() || !p.Prepare()) {
      return std::nullopt;
    }
    profiles.push_back(std::move(p));
  }
  if (!in.ok() || !in.done()) {
    return std::nullopt;
  }
  return profiles;
}

}  // namespace zephyrus::agent::voice
