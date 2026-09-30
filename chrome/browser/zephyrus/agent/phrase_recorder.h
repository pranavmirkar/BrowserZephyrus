// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_PHRASE_RECORDER_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_PHRASE_RECORDER_H_

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "base/containers/span.h"

namespace zephyrus::agent::voice {

// Catches ONE short utterance from a stream of audio: from where speech begins
// to a half-second of quiet after it. It is what enrolment and "test my voice"
// listen with: they want the phrase the user just said, whole, and nothing else.
//
// Same audio, same answer, like the wake engine: time is the audio fed in.
class PhraseRecorder {
 public:
  PhraseRecorder();
  ~PhraseRecorder();

  // Any amount of 16 kHz mono 16-bit audio. Returns an utterance when one has
  // just ended (with a little room before it and after it). Utterances under a
  // quarter of a second are clicks and are dropped without a word.
  std::optional<std::vector<int16_t>> Feed(base::span<const int16_t> pcm);

  // How loud it is now, 0 to 1, for a level meter.
  float level() const { return level_; }
  // Speech is being heard right now.
  bool hearing_speech() const { return in_speech_; }

  void Reset();

 private:
  void ProcessHop(base::span<const int16_t> hop);

  std::vector<int16_t> carry_;
  std::deque<int16_t> preroll_;
  std::vector<int16_t> utterance_;
  std::optional<std::vector<int16_t>> ready_;
  float floor_db_ = -70.0f;
  float level_ = 0.0f;
  int warmup_ = 30;
  int loud_run_ = 0;
  int quiet_run_ = 0;
  int hops_ = 0;
  bool in_speech_ = false;
};

}  // namespace zephyrus::agent::voice

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_PHRASE_RECORDER_H_
