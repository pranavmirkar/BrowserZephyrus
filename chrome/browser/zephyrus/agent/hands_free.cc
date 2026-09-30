// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/hands_free.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace zephyrus::agent::voice {

namespace {

// When, in hops since speech began, the phrase is looked for: soon after it
// could have been said, and again a little later in case it was said slowly. A
// natural pause, or the end of the speech, is checked as well.
constexpr std::array<int, 3> kChecks = {85, 120, 160};

constexpr size_t kHopSamples = kFrameHop;

}  // namespace

HandsFreeEngine::HandsFreeEngine(Delegate* delegate, Options options)
    : delegate_(delegate), options_(options) {}

HandsFreeEngine::~HandsFreeEngine() = default;

void HandsFreeEngine::SetProfiles(std::vector<VoiceProfile> profiles) {
  profiles_ = std::move(profiles);
  for (VoiceProfile& p : profiles_) {
    if (p.prepared.empty()) {
      p.Prepare();
    }
  }
  Reset();
}

void HandsFreeEngine::Reset() {
  carry_.clear();
  ToIdle();
}

void HandsFreeEngine::ToIdle() {
  state_ = State::kIdle;
  utterance_.clear();
  preroll_.clear();
  loud_run_ = 0;
  quiet_run_ = 0;
  hops_in_utterance_ = 0;
  await_hops_ = 0;
  next_check_ = 0;
  paused_checked_ = false;
  give_up_ = false;
  follow_up_ = false;
  command_start_ = 0;
}

void HandsFreeEngine::Feed(base::span<const int16_t> pcm) {
  if (profiles_.empty()) {
    return;  // nothing to listen for: nothing is examined or kept
  }
  carry_.insert(carry_.end(), pcm.begin(), pcm.end());
  size_t used = 0;
  while (carry_.size() - used >= kHopSamples) {
    ProcessHop(base::span(carry_).subspan(used, kHopSamples));
    used += kHopSamples;
  }
  carry_.erase(carry_.begin(), carry_.begin() + used);
}

void HandsFreeEngine::ProcessHop(base::span<const int16_t> hop) {
  float sum = 0.0f;
  for (int16_t s : hop) {
    const float x = static_cast<float>(s) / 32768.0f;
    sum += x * x;
  }
  const float energy_db =
      10.0f * std::log10(sum / static_cast<float>(kHopSamples) + 1e-10f);

  const bool loud = vad_.Update(energy_db, /*freeze_floor=*/false);

  switch (state_) {
    case State::kIdle:
    case State::kAwaiting: {
      preroll_.insert(preroll_.end(), hop.begin(), hop.end());
      while (preroll_.size() > static_cast<size_t>(kPrerollHops * kHopSamples)) {
        preroll_.erase(preroll_.begin(), preroll_.begin() + kHopSamples);
      }
      loud_run_ = loud ? loud_run_ + 1 : 0;
      if (state_ == State::kAwaiting) {
        ++await_hops_;
      }
      if (loud_run_ >= kOnsetRun) {
        BeginUtterance();
      } else if (state_ == State::kAwaiting && await_hops_ >= kAwaitHops) {
        delegate_->OnTimedOut();
        ToIdle();
      }
      return;
    }
    case State::kCollecting:
    case State::kCapturing:
      break;
  }

  ++hops_in_utterance_;
  quiet_run_ = loud ? 0 : quiet_run_ + 1;
  if (!give_up_) {
    utterance_.insert(utterance_.end(), hop.begin(), hop.end());
  }

  if (state_ == State::kCollecting && !give_up_) {
    const bool pause = quiet_run_ == kPauseHops && !paused_checked_;
    const bool checkpoint = next_check_ < static_cast<int>(kChecks.size()) &&
                            hops_in_utterance_ >= kChecks[next_check_];
    if (pause || checkpoint) {
      paused_checked_ |= pause;
      while (next_check_ < static_cast<int>(kChecks.size()) &&
             hops_in_utterance_ >= kChecks[next_check_]) {
        ++next_check_;
      }
      TryWake();
    }
    // Past the last look, an utterance that has not matched never will: the
    // phrase is at the start. Stop keeping it.
    if (state_ == State::kCollecting &&
        hops_in_utterance_ > kChecks.back() + 5) {
      give_up_ = true;
      utterance_.clear();
      utterance_.shrink_to_fit();
    }
  }

  if (quiet_run_ >= kEndQuietHops || hops_in_utterance_ > kMaxUtteranceHops) {
    FinishUtterance();
  }
}

void HandsFreeEngine::BeginUtterance() {
  utterance_.assign(preroll_.begin(), preroll_.end());
  preroll_.clear();
  hops_in_utterance_ = 0;
  quiet_run_ = 0;
  loud_run_ = 0;
  next_check_ = 0;
  paused_checked_ = false;
  give_up_ = false;
  if (state_ == State::kAwaiting) {
    // The command after a phrase that was said alone.
    state_ = State::kCapturing;
    follow_up_ = true;
    command_start_ = 0;
  } else {
    state_ = State::kCollecting;
    follow_up_ = false;
  }
}

bool HandsFreeEngine::TryWake() {
  const std::vector<float> samples = PcmToFloat(utterance_);
  const Frames frames = ExtractFrames(samples);
  const auto [first, last] = SpeechSpan(frames.energy_db);
  (void)last;
  if (frames.features.empty() || first >= frames.features.size()) {
    return false;
  }
  const MatchResult r =
      MatchWakePhrase(profiles_, samples, frames, first, options_.lock,
                      options_.sensitivity);
  if (!(r.wake && r.voice_ok) || r.profile < 0) {
    last_reject_ = r;
    return false;
  }
  profile_ = r.profile;
  wake_.profile = r.profile;
  wake_.wake_distance = r.wake_distance;
  wake_.speaker_distance = r.speaker_distance;
  // The command starts where the phrase ended.
  command_start_ = std::min(utterance_.size(),
                            r.end_frame * static_cast<size_t>(kHopSamples));
  state_ = State::kCapturing;
  delegate_->OnWake(wake_);
  return true;
}

void HandsFreeEngine::FinishUtterance() {
  if (state_ == State::kCollecting) {
    if (!give_up_ && !utterance_.empty() && !TryWake()) {
      const MatchResult r = last_reject_;
      delegate_->OnIgnored(r.wake && !r.voice_ok ? Ignored::kNotYourVoice
                                                 : Ignored::kNotThePhrase,
                           r);
      ToIdle();
      return;
    }
    if (state_ == State::kCollecting) {
      // Long speech that never matched: dropped without a word.
      delegate_->OnIgnored(Ignored::kNotThePhrase, last_reject_);
      ToIdle();
      return;
    }
  }
  if (state_ == State::kCapturing) {
    DeliverCommand(command_start_, follow_up_);
    return;
  }
  ToIdle();
}

void HandsFreeEngine::DeliverCommand(size_t start, bool follow_up) {
  size_t end = utterance_.size();
  if (quiet_run_ > 10) {
    const size_t trim = static_cast<size_t>(quiet_run_ - 10) * kHopSamples;
    end = end > trim ? end - trim : 0;
  }
  end = std::max(end, start);
  const int hops = static_cast<int>((end - start) / kHopSamples);

  if (!follow_up && hops < kMinCommandHops) {
    // Only the phrase: the command comes next.
    state_ = State::kAwaiting;
    utterance_.clear();
    preroll_.clear();
    loud_run_ = 0;
    quiet_run_ = 0;
    hops_in_utterance_ = 0;
    await_hops_ = 0;
    follow_up_ = false;
    return;
  }
  if (hops < kMinCommandHops) {
    ToIdle();  // a click or a cough after the phrase
    return;
  }
  std::vector<int16_t> pcm(utterance_.begin() + start, utterance_.begin() + end);

  // A command spoken AFTER a pause is only known to follow the phrase, not to be
  // said by whoever said it. With the lock on, its pitch must be the voice's:
  // the sound of a sentence differs from the sound of "Hey Zep", but a person's
  // pitch does not, and a different speaker's usually does.
  if (follow_up && options_.lock && profile_ >= 0 &&
      static_cast<size_t>(profile_) < profiles_.size()) {
    const std::vector<float> samples = PcmToFloat(pcm);
    const Frames frames = ExtractFrames(samples);
    const auto [first, last] = SpeechSpan(frames.energy_db);
    if (!PitchMatches(profiles_[profile_], samples, frames, first, last)) {
      delegate_->OnIgnored(Ignored::kWrongVoiceFollowUp, MatchResult());
      ToIdle();
      return;
    }
  }
  const WakeInfo info = wake_;
  ToIdle();
  delegate_->OnCommand(std::move(pcm), info);
}

MatchResult TestPhrase(base::span<const VoiceProfile> profiles,
                       base::span<const int16_t> pcm,
                       bool lock,
                       Sensitivity sensitivity) {
  const std::vector<float> samples = PcmToFloat(pcm);
  const Frames frames = ExtractFrames(samples);
  const auto [first, last] = SpeechSpan(frames.energy_db);
  (void)last;
  if (frames.features.empty()) {
    return MatchResult();
  }
  return MatchWakePhrase(profiles, samples, frames, first, lock, sensitivity);
}

}  // namespace zephyrus::agent::voice
