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
std::pair<size_t, size_t> SpeechSpan(base::span<const float> energy_db,
                                     float margin_db = 25.0f,
                                     float floor_db = -60.0f);

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
