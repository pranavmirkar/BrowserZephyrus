// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_HANDS_FREE_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_HANDS_FREE_H_

#include <cstdint>
#include <deque>
#include <vector>

#include "base/containers/span.h"
#include "chrome/browser/zephyrus/agent/voice_lock.h"

namespace zephyrus::agent::voice {

// "Hey Zep": the streaming half of hands-free.
//
// Fed microphone audio in any chunk size, it finds where speech begins and ends,
// decides ON THIS COMPUTER whether an utterance begins with the wake phrase and
// (with the lock on) in an enrolled voice, and only then hands the command that
// follows to its delegate. Speech that is not for Zep is dropped where it is
// heard: it is never sent anywhere and is not kept.
//
// It knows nothing of devices, threads or clocks. Time is the amount of audio
// fed to it, so the same audio always gives the same decisions, which is how it
// is tested.
class HandsFreeEngine {
 public:
  struct WakeInfo {
    int profile = -1;  // index into the profiles it was given
    float wake_distance = 9.0f;
    float speaker_distance = 99.0f;
  };

  enum class Ignored {
    kNotThePhrase,  // speech, but not "Hey Zep"
    kNotYourVoice,  // "Hey Zep" in a voice that is not enrolled
    kWrongVoiceFollowUp,  // a follow-up command in another voice
  };

  class Delegate {
   public:
    virtual ~Delegate() = default;
    // The phrase was accepted; the command is being listened for.
    virtual void OnWake(const WakeInfo& info) = 0;
    // A whole command, 16 kHz mono, its end heard.
    virtual void OnCommand(std::vector<int16_t> pcm, const WakeInfo& info) = 0;
    // The phrase was heard but no command followed in time.
    virtual void OnTimedOut() = 0;
    // Speech that was not for Zep, or not from an enrolled voice.
    virtual void OnIgnored(Ignored why, const MatchResult& match) = 0;
  };

  enum class State {
    kIdle,        // waiting for speech
    kCollecting,  // speech in progress, phrase not yet decided
    kCapturing,   // phrase accepted; the command is being spoken
    kAwaiting,    // phrase accepted alone; waiting for the command
  };

  struct Options {
    bool lock = true;
    Sensitivity sensitivity = Sensitivity::kBalanced;
  };

  HandsFreeEngine(Delegate* delegate, Options options);
  ~HandsFreeEngine();

  HandsFreeEngine(const HandsFreeEngine&) = delete;
  HandsFreeEngine& operator=(const HandsFreeEngine&) = delete;

  void SetProfiles(std::vector<VoiceProfile> profiles);
  void SetOptions(Options options) { options_ = options; }

  // Any amount of 16 kHz mono 16-bit audio.
  void Feed(base::span<const int16_t> pcm);

  // Back to waiting, dropping whatever was being heard.
  void Reset();

  State state() const { return state_; }
  float noise_floor_db() const { return vad_.floor_db(); }
  bool has_profiles() const { return !profiles_.empty(); }

  // Audio time each step waits or allows, in 10 ms hops.
  static constexpr int kPrerollHops = 30;
  static constexpr int kOnsetRun = 3;
  static constexpr int kEndQuietHops = 60;
  static constexpr int kPauseHops = 25;
  static constexpr int kMaxUtteranceHops = 1500;
  static constexpr int kAwaitHops = 600;
  static constexpr int kMinCommandHops = 35;

 private:
  void ProcessHop(base::span<const int16_t> hop);
  void BeginUtterance();
  bool TryWake();
  void FinishUtterance();
  void DeliverCommand(size_t start, bool follow_up);
  void ToIdle();

  Delegate* const delegate_;
  Options options_;
  std::vector<VoiceProfile> profiles_;

  State state_ = State::kIdle;
  std::vector<int16_t> carry_;
  std::deque<int16_t> preroll_;
  std::vector<int16_t> utterance_;
  HopVad vad_;
  int loud_run_ = 0;
  int quiet_run_ = 0;
  int hops_in_utterance_ = 0;
  int await_hops_ = 0;
  int next_check_ = 0;
  bool paused_checked_ = false;
  // After the last attempt, a long utterance that never matched is only waited
  // out: nothing more to decide, and nothing worth keeping.
  bool give_up_ = false;
  bool follow_up_ = false;
  size_t command_start_ = 0;
  WakeInfo wake_;
  int profile_ = -1;
  MatchResult last_reject_;
};

// The numbers for one recording of the phrase, for a "test my voice" screen and
// for tuning: how it scored against the enrolled voices.
MatchResult TestPhrase(base::span<const VoiceProfile> profiles,
                       base::span<const int16_t> pcm,
                       bool lock,
                       Sensitivity sensitivity);

}  // namespace zephyrus::agent::voice

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_HANDS_FREE_H_
