// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_DSP_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_DSP_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "base/containers/span.h"

// Small, dependency-free audio maths for the hands-free voice features: the
// on-device "Hey Zep" detector and Voice Lock. Nothing here touches a device, a
// thread or the network, so all of it is unit-tested against recorded speech.
//
// WHAT THIS IS AND IS NOT. It measures how a short phrase sounds (its spectral
// envelope over time, and the pitch of the voice) and compares it with what the
// user recorded. That is enough to tell people apart in ordinary use and to keep
// a TV or a colleague from waking the agent. It is NOT a biometric: a recording
// of the owner, or a very close impersonator, can pass. Nothing that matters
// (payments, sign-in) relies on it.
namespace zephyrus::agent::voice {

inline constexpr int kSampleRate = 16000;
inline constexpr int kFrameLength = 400;  // 25 ms
inline constexpr int kFrameHop = 160;     // 10 ms
inline constexpr int kFftSize = 512;
inline constexpr int kMelBands = 26;
inline constexpr int kCepstra = 12;                 // c1..c12
inline constexpr int kStaticDim = kCepstra + 1;     // and the log energy
inline constexpr int kFeatureDim = 2 * kStaticDim;  // and their deltas
// Mean and spread of c1..c12 over the strong frames, and the pitch.
inline constexpr int kSpeakerDim = 2 * kCepstra + 1;

using StaticFrame = std::array<float, kStaticDim>;
using FeatureFrame = std::array<float, kFeatureDim>;
using SpeakerVector = std::array<float, kSpeakerDim>;

struct Frames {
  Frames();
  Frames(Frames&&);
  Frames& operator=(Frames&&);
  ~Frames();

  std::vector<StaticFrame> features;
  std::vector<float> energy_db;  // per frame, 10*log10(mean square)
};

// 16-bit PCM to [-1, 1).
std::vector<float> PcmToFloat(base::span<const int16_t> pcm);

// 25 ms frames every 10 ms: mel cepstra c1..c12 and the log energy. A signal
// shorter than a frame gives one zero-padded frame.
Frames ExtractFrames(base::span<const float> samples);

// [first, last) of the frames that are speech: within `margin_db` of the
// loudest frame and above `floor_db`. Empty (0, 0) when there are no frames.
//
// The line is also kept above the room: a few dB over the quiet fifth of the
// recording. Against a quiet room that changes nothing; against a loud one it is
// the difference between finding the speech and calling everything speech.
//
// `phrase_only` returns just the stretch around the loudest frame, for a
// recording of one short phrase; without it, from the first frame over the line
// to the last, which is what "the start of an utterance" needs when a command
// follows the phrase.
std::pair<size_t, size_t> SpeechSpan(base::span<const float> energy_db,
                                     float margin_db = 25.0f,
                                     float floor_db = -60.0f,
                                     bool phrase_only = false);

// Is this hop speech? One noise-floor follower shared by the enrolment recorder
// and the always-listening engine, so they cannot disagree about what a person
// sounds like against their room.
//
// It smooths over four hops and needs a smaller margin over the floor when the
// floor is high. MEASURED on a laptop's built-in array microphone: the room and
// the microphone's own gain put the noise at -25 dB, speech peaked at -12 dB,
// and the noise jittered by +-4 dB from one 10 ms hop to the next. A fixed 10 dB
// margin on raw hops saw almost none of the speech, and the recorder never found
// the end of a phrase because the "quiet" between words kept crossing the line.
class HopVad {
 public:
  HopVad();
  ~HopVad();
  HopVad(const HopVad&);
  HopVad& operator=(const HopVad&);

  // `energy_db` is the raw 10*log10(mean square) of one hop. `freeze_floor`
  // stops the floor rising, for a caller that already knows speech is going on.
  bool Update(float energy_db, bool freeze_floor);
  void Reset();
  float floor_db() const { return floor_db_; }
  // Smoothed level of the latest hop, for a meter.
  float level_db() const { return smooth_db_; }
  // How many times the floor has been moved to the room after being found far
  // below it. Audio heard around one of those is not to be trusted.
  int reanchors() const { return reanchors_; }

 private:
  std::array<float, 4> power_ = {};
  int filled_ = 0;
  int next_ = 0;
  float floor_db_ = -70.0f;
  float smooth_db_ = -100.0f;
  int warmup_ = 30;
  // The last five seconds of smoothed levels: a floor learned from the
  // digital silence a device gives before it starts (or from a room that got
  // louder) is far below the room, and a follower that only creeps up 15 dB
  // never reaches it.
  std::vector<float> window_;
  int since_check_ = 0;
  int reanchors_ = 0;
  int loud_hops_ = 0;
};

// The features a template or a query is compared on: the frames normalised for
// the mean and spread of THEIR OWN span (so the level of the microphone and of
// the voice mostly drop out), then with deltas appended.
std::vector<FeatureFrame> NormalisedFeatures(
    base::span<const StaticFrame> frames);

struct DtwResult {
  // Mean per-frame distance along the best path, per unit of path length.
  // ~0.1-0.2 for the same phrase by the same voice, ~0.4 and up otherwise.
  float distance = 9.0f;
  // How many frames of the query the template covered: where the phrase ends.
  size_t end = 0;
};

// Dynamic time warping of `tmpl` against the START of `query`: both begin
// together, and the template may finish anywhere in the query between half and
// 1.8 times its own length. This is what finds "Hey Zep" at the head of "Hey
// Zep, open the news". Distance 9 when the query is too short to hold it.
DtwResult OpenEndDtw(base::span<const FeatureFrame> tmpl,
                     base::span<const FeatureFrame> query,
                     float band = 0.8f);

// The pitch of the voice around `center` (a sample index), in Hz, or 0 when it
// is not voiced there.
float PitchHz(base::span<const float> samples, size_t center);

// How the voice sounds over the strong frames in [first, last): the mean and
// spread of c1..c12 (NOT normalised, so it keeps the voice's own colour) and
// log2 of the median pitch.
SpeakerVector ComputeSpeakerVector(base::span<const float> samples,
                                   const Frames& frames,
                                   size_t first,
                                   size_t last);

}  // namespace zephyrus::agent::voice

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_DSP_H_
