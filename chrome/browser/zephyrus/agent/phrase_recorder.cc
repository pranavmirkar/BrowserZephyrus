// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/phrase_recorder.h"

#include <algorithm>
#include <cmath>

#include "chrome/browser/zephyrus/agent/voice_dsp.h"

namespace zephyrus::agent::voice {

namespace {

constexpr size_t kHop = kFrameHop;
constexpr int kPrerollHops = 20;
constexpr int kOnsetRun = 3;
constexpr int kEndQuietHops = 50;   // half a second
constexpr int kMaxHops = 400;       // 4 s: this is one short phrase
constexpr int kMinHops = 25;        // under a quarter second is a click
constexpr float kOnsetMarginDb = 10.0f;
constexpr float kAbsoluteMinDb = -55.0f;

}  // namespace

PhraseRecorder::PhraseRecorder() = default;
PhraseRecorder::~PhraseRecorder() = default;

void PhraseRecorder::Reset() {
  carry_.clear();
  preroll_.clear();
  utterance_.clear();
  ready_.reset();
  loud_run_ = 0;
  quiet_run_ = 0;
  hops_ = 0;
  in_speech_ = false;
  level_ = 0.0f;
}

std::optional<std::vector<int16_t>> PhraseRecorder::Feed(
    base::span<const int16_t> pcm) {
  carry_.insert(carry_.end(), pcm.begin(), pcm.end());
  size_t used = 0;
  while (carry_.size() - used >= static_cast<size_t>(kHop) && !ready_) {
    ProcessHop(base::span(carry_).subspan(used, kHop));
    used += kHop;
  }
  carry_.erase(carry_.begin(), carry_.begin() + used);
  std::optional<std::vector<int16_t>> out = std::move(ready_);
  ready_.reset();
  return out;
}

void PhraseRecorder::ProcessHop(base::span<const int16_t> hop) {
  float sum = 0.0f;
  for (int16_t s : hop) {
    const float x = static_cast<float>(s) / 32768.0f;
    sum += x * x;
  }
  const float energy_db =
      10.0f * std::log10(sum / static_cast<float>(kHop) + 1e-10f);
  level_ = std::clamp((energy_db + 60.0f) / 50.0f, 0.0f, 1.0f);

  if (warmup_ > 0) {
    const float k = static_cast<float>(30 - warmup_);
    floor_db_ = (floor_db_ * k + energy_db) / (k + 1.0f);
    --warmup_;
  } else if (energy_db < floor_db_) {
    floor_db_ += 0.2f * (energy_db - floor_db_);
  } else if (!in_speech_ && energy_db < floor_db_ + 15.0f) {
    floor_db_ += 0.004f * (energy_db - floor_db_);
  }
  const bool loud =
      warmup_ == 0 &&
      energy_db > std::max(floor_db_ + kOnsetMarginDb, kAbsoluteMinDb);

  if (!in_speech_) {
    preroll_.insert(preroll_.end(), hop.begin(), hop.end());
    while (preroll_.size() > static_cast<size_t>(kPrerollHops * kHop)) {
      preroll_.erase(preroll_.begin(), preroll_.begin() + kHop);
    }
    loud_run_ = loud ? loud_run_ + 1 : 0;
    if (loud_run_ >= kOnsetRun) {
      in_speech_ = true;
      utterance_.assign(preroll_.begin(), preroll_.end());
      preroll_.clear();
      hops_ = 0;
      quiet_run_ = 0;
    }
    return;
  }

  utterance_.insert(utterance_.end(), hop.begin(), hop.end());
  ++hops_;
  quiet_run_ = loud ? 0 : quiet_run_ + 1;
  if (quiet_run_ >= kEndQuietHops || hops_ > kMaxHops) {
    const bool long_enough = hops_ - quiet_run_ >= kMinHops;
    in_speech_ = false;
    loud_run_ = 0;
    quiet_run_ = 0;
    if (long_enough) {
      ready_ = std::move(utterance_);
    }
    utterance_.clear();
  }
}

}  // namespace zephyrus::agent::voice
