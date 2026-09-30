// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_LOCK_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_LOCK_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "chrome/browser/zephyrus/agent/voice_dsp.h"

namespace zephyrus::agent::voice {

// Voice Lock: hands-free "Hey Zep" answers only voices the user has recorded.
//
// A voice is a name and a handful of recordings of the phrase "Hey Zep". What is
// kept is what the phrase SOUNDS like (mel cepstra over time, and the pitch),
// never the audio. See voice_dsp.h for what this can and cannot do: it is a
// lock for the hands-free path, not a security boundary, and payments and
// sign-in do not depend on it.

// How readily the phrase is accepted. Strict for a noisy room full of people,
// relaxed for a quiet room and a voice that will not match.
enum class Sensitivity { kStrict, kBalanced, kRelaxed };
float ThresholdScale(Sensitivity sensitivity);

inline constexpr size_t kMaxProfiles = 6;
inline constexpr size_t kEnrollmentSamples = 3;
inline constexpr size_t kMaxTemplates = 12;
inline constexpr size_t kMaxNameLength = 32;

// One recording of the phrase, reduced to what is compared.
struct WakeTemplate {
  WakeTemplate();
  WakeTemplate(const WakeTemplate&);
  WakeTemplate(WakeTemplate&&);
  WakeTemplate& operator=(const WakeTemplate&);
  WakeTemplate& operator=(WakeTemplate&&);
  ~WakeTemplate();

  std::vector<StaticFrame> frames;  // the phrase only, silence trimmed
  SpeakerVector speaker = {};
};

struct VoiceProfile {
  VoiceProfile();
  VoiceProfile(const VoiceProfile&);
  VoiceProfile(VoiceProfile&&);
  VoiceProfile& operator=(const VoiceProfile&);
  VoiceProfile& operator=(VoiceProfile&&);
  ~VoiceProfile();

  std::string id;
  std::string name;
  int64_t created_unix = 0;
  std::vector<WakeTemplate> templates;

  // Derived from the templates by Prepare(); never stored.
  std::vector<std::vector<FeatureFrame>> prepared;
  SpeakerVector speaker_mean = {};
  SpeakerVector speaker_spread = {};
  float wake_threshold = 0.25f;
  float speaker_threshold = 4.0f;
  float pitch_mean = 0.0f;  // log2 Hz

  // Computes everything derived. False when there are too few templates.
  bool Prepare();
};

enum class SampleProblem {
  kNone,
  kTooShort,      // under a quarter of a second of speech
  kTooLong,       // over two seconds: not just "Hey Zep"
  kTooQuiet,      // barely above the room
  kNotThePhrase,  // does not resemble the recordings already accepted
};

struct SampleCheck {
  SampleCheck();
  SampleCheck(SampleCheck&&);
  SampleCheck& operator=(SampleCheck&&);
  ~SampleCheck();

  SampleProblem problem = SampleProblem::kNone;
  WakeTemplate sample;
};

// Turns one recording of the phrase into a template, or says what is wrong with
// it. `accepted` are the ones already taken for this voice, so a recording that
// is clearly something else is refused instead of poisoning the voice.
SampleCheck AnalyseEnrollmentSample(base::span<const int16_t> pcm,
                                    base::span<const WakeTemplate> accepted);

// A user-facing sentence for a problem.
std::string DescribeProblem(SampleProblem problem);

struct MatchResult {
  bool wake = false;         // the phrase, close enough
  bool voice_ok = false;     // and, with the lock on, in an enrolled voice
  int profile = -1;          // index of the best voice, -1 for none
  float wake_distance = 9.0f;
  float speaker_distance = 99.0f;
  size_t end_frame = 0;      // where the phrase ended in the utterance
};

// Does this utterance BEGIN with the phrase, and in whose voice?
//
// `samples` is the utterance from a little before the speech starts, `frames`
// its features, and [first, ...) where speech begins. With `lock` off any voice
// close enough counts; on, only one whose own features also match.
MatchResult MatchWakePhrase(base::span<const VoiceProfile> profiles,
                            base::span<const float> samples,
                            const Frames& frames,
                            size_t first,
                            bool lock,
                            Sensitivity sensitivity);

// Whether a longer stretch of speech (the command after the phrase) has the
// pitch of this voice. Text-independent and loose: the sound of a sentence
// differs from the sound of "Hey Zep", but the pitch of the speaker does not.
bool PitchMatches(const VoiceProfile& profile,
                  base::span<const float> samples,
                  const Frames& frames,
                  size_t first,
                  size_t last);

// Storage. One blob for all the voices; the caller encrypts it. Untrusted on
// the way in: every length is bounded and a bad blob gives nullopt.
std::string SerialiseProfiles(base::span<const VoiceProfile> profiles);
std::optional<std::vector<VoiceProfile>> ParseProfiles(
    const std::string& bytes);

}  // namespace zephyrus::agent::voice

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_LOCK_H_
