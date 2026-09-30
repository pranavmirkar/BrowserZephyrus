// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/voice_dsp.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <complex>
#include <limits>

namespace zephyrus::agent::voice {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kBins = kFftSize / 2 + 1;

float HzToMel(float hz) {
  return 2595.0f * std::log10(1.0f + hz / 700.0f);
}
float MelToHz(float mel) {
  return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

struct Tables {
  Tables() {
    // Triangular mel filters between 100 Hz and 7 kHz.
    const float lo = HzToMel(100.0f);
    const float hi = HzToMel(7000.0f);
    std::array<int, kMelBands + 2> edges = {};
    for (int i = 0; i < kMelBands + 2; ++i) {
      const float hz = MelToHz(lo + (hi - lo) * static_cast<float>(i) /
                                        static_cast<float>(kMelBands + 1));
      edges[i] = static_cast<int>(
          std::floor(static_cast<float>(kFftSize + 1) * hz / kSampleRate));
    }
    for (auto& row : mel) {
      row.fill(0.0f);
    }
    for (int m = 1; m <= kMelBands; ++m) {
      const int l = edges[m - 1];
      const int c = edges[m];
      const int r = edges[m + 1];
      for (int k = l; k < c && k < kBins; ++k) {
        mel[m - 1][k] = static_cast<float>(k - l) /
                        static_cast<float>(std::max(c - l, 1));
      }
      for (int k = c; k < r && k < kBins; ++k) {
        mel[m - 1][k] = static_cast<float>(r - k) /
                        static_cast<float>(std::max(r - c, 1));
      }
    }
    for (int i = 0; i < kCepstra; ++i) {
      for (int m = 0; m < kMelBands; ++m) {
        dct[i][m] = std::cos(kPi * static_cast<float>(i + 1) *
                             (static_cast<float>(m) + 0.5f) / kMelBands);
      }
    }
    for (int i = 0; i < kFrameLength; ++i) {
      window[i] =
          0.54f - 0.46f * std::cos(2.0f * kPi * static_cast<float>(i) /
                                   static_cast<float>(kFrameLength - 1));
    }
    for (int i = 0; i < kFftSize / 2; ++i) {
      const float a = -2.0f * kPi * static_cast<float>(i) / kFftSize;
      twiddle[i] = std::complex<float>(std::cos(a), std::sin(a));
    }
    int bits = 0;
    while ((1 << bits) < kFftSize) {
      ++bits;
    }
    for (int i = 0; i < kFftSize; ++i) {
      int r = 0;
      for (int b = 0; b < bits; ++b) {
        if (i & (1 << b)) {
          r |= 1 << (bits - 1 - b);
        }
      }
      bit_reverse[i] = static_cast<uint16_t>(r);
    }
  }

  std::array<std::array<float, kBins>, kMelBands> mel;
  std::array<std::array<float, kMelBands>, kCepstra> dct;
  std::array<float, kFrameLength> window;
  std::array<std::complex<float>, kFftSize / 2> twiddle;
  std::array<uint16_t, kFftSize> bit_reverse;
};

const Tables& GetTables() {
  // Trivially destructible, so a plain function-local static is the right form.
  static const Tables tables;
  return tables;
}

// In-place radix-2 FFT.
void Fft(std::array<std::complex<float>, kFftSize>& a, const Tables& t) {
  for (int i = 0; i < kFftSize; ++i) {
    const int j = t.bit_reverse[i];
    if (j > i) {
      std::swap(a[i], a[j]);
    }
  }
  for (int len = 2; len <= kFftSize; len <<= 1) {
    const int half = len / 2;
    const int step = kFftSize / len;
    for (int start = 0; start < kFftSize; start += len) {
      for (int k = 0; k < half; ++k) {
        const std::complex<float> w = t.twiddle[k * step];
        const std::complex<float> u = a[start + k];
        const std::complex<float> v = a[start + k + half] * w;
        a[start + k] = u + v;
        a[start + k + half] = u - v;
      }
    }
  }
}

}  // namespace

Frames::Frames() = default;
Frames::Frames(Frames&&) = default;
Frames& Frames::operator=(Frames&&) = default;
Frames::~Frames() = default;

std::vector<float> PcmToFloat(base::span<const int16_t> pcm) {
  std::vector<float> out;
  out.reserve(pcm.size());
  for (int16_t s : pcm) {
    out.push_back(static_cast<float>(s) / 32768.0f);
  }
  return out;
}

Frames ExtractFrames(base::span<const float> samples) {
  Frames out;
  if (samples.empty()) {
    return out;
  }
  const Tables& t = GetTables();
  const size_t count =
      1 + (samples.size() > static_cast<size_t>(kFrameLength)
               ? (samples.size() - kFrameLength) / kFrameHop
               : 0);
  std::vector<float> pre(samples.size());
  pre[0] = samples[0];
  for (size_t i = 1; i < samples.size(); ++i) {
    pre[i] = samples[i] - 0.97f * samples[i - 1];
  }
  out.features.reserve(count);
  out.energy_db.reserve(count);

  std::array<std::complex<float>, kFftSize> buffer;
  std::array<float, kMelBands> logmel;
  for (size_t f = 0; f < count; ++f) {
    buffer.fill(std::complex<float>(0.0f, 0.0f));
    float sumsq = 0.0f;
    for (int k = 0; k < kFrameLength; ++k) {
      const size_t idx = f * kFrameHop + static_cast<size_t>(k);
      if (idx >= samples.size()) {
        break;
      }
      sumsq += samples[idx] * samples[idx];
      buffer[k] = std::complex<float>(pre[idx] * t.window[k], 0.0f);
    }
    Fft(buffer, t);
    for (int m = 0; m < kMelBands; ++m) {
      float e = 0.0f;
      for (int k = 0; k < kBins; ++k) {
        e += t.mel[m][k] * std::norm(buffer[k]);
      }
      logmel[m] = std::log(std::max(e, 1e-10f));
    }
    StaticFrame frame;
    for (int i = 0; i < kCepstra; ++i) {
      float c = 0.0f;
      for (int m = 0; m < kMelBands; ++m) {
        c += t.dct[i][m] * logmel[m];
      }
      frame[i] = c;
    }
    frame[kCepstra] = std::log(sumsq + 1e-10f);
    out.features.push_back(frame);
    out.energy_db.push_back(
        10.0f * std::log10(sumsq / static_cast<float>(kFrameLength) + 1e-10f));
  }
  return out;
}

std::pair<size_t, size_t> SpeechSpan(base::span<const float> energy_db,
                                     float margin_db,
                                     float floor_db,
                                     bool phrase_only) {
  if (energy_db.empty()) {
    return {0, 0};
  }
  const size_t n = energy_db.size();
  const size_t peak_at = static_cast<size_t>(
      std::max_element(energy_db.begin(), energy_db.end()) - energy_db.begin());
  const float peak = energy_db[peak_at];

  // The room, as the quiet fifth of the recording smoothed over five frames.
  // Against a quiet room this is far below the threshold and changes nothing;
  // against a loud one (a laptop's own microphone: noise at -25 dB, speech at
  // -12) the old "within 25 dB of the peak" line sat BELOW the noise, so the
  // whole recording counted as speech.
  std::vector<float> smooth(n);
  for (size_t i = 0; i < n; ++i) {
    float sum = 0.0f;
    int count = 0;
    for (size_t j = i > 2 ? i - 2 : 0; j <= std::min(n - 1, i + 2); ++j) {
      sum += std::pow(10.0f, energy_db[j] / 10.0f);
      ++count;
    }
    smooth[i] = 10.0f * std::log10(sum / static_cast<float>(count) + 1e-10f);
  }
  std::vector<float> sorted = smooth;
  std::sort(sorted.begin(), sorted.end());
  const float room = sorted[n / 5];
  const float threshold =
      std::max({peak - margin_db, floor_db, room + 4.0f});

  if (!phrase_only) {
    size_t first = n;
    size_t last = 0;
    for (size_t i = 0; i < n; ++i) {
      if (smooth[i] > threshold) {
        first = std::min(first, i);
        last = i + 1;
      }
    }
    if (first >= last) {
      return {0, 0};
    }
    return {first, last};
  }

  // The stretch of speech around the loudest frame, not the first and last
  // frame anywhere above the line: a click or a noise burst far from the
  // phrase would otherwise stretch it across the silence between.
  constexpr size_t kBridge = 8;  // a gap this short is inside the phrase
  size_t first = peak_at;
  size_t last = peak_at;
  size_t gap = 0;
  for (size_t i = peak_at; i-- > 0;) {
    if (smooth[i] > threshold) {
      first = i;
      gap = 0;
    } else if (++gap > kBridge) {
      break;
    }
  }
  gap = 0;
  for (size_t i = peak_at + 1; i < n; ++i) {
    if (smooth[i] > threshold) {
      last = i;
      gap = 0;
    } else if (++gap > kBridge) {
      break;
    }
  }
  return {first, last + 1};
}

HopVad::HopVad() = default;
HopVad::~HopVad() = default;
HopVad::HopVad(const HopVad&) = default;
HopVad& HopVad::operator=(const HopVad&) = default;

bool HopVad::Update(float energy_db, bool freeze_floor) {
  power_[next_] = std::pow(10.0f, energy_db / 10.0f);
  next_ = (next_ + 1) % static_cast<int>(power_.size());
  filled_ = std::min<int>(filled_ + 1, static_cast<int>(power_.size()));
  float mean = 0.0f;
  for (int i = 0; i < filled_; ++i) {
    mean += power_[i];
  }
  smooth_db_ = 10.0f * std::log10(mean / static_cast<float>(filled_) + 1e-10f);

  // The floor: learned over the first 300 ms, then followed. It falls quickly
  // to a quieter room and rises slowly, and not at all for a burst of speech,
  // which is well above it.
  if (warmup_ > 0) {
    const float k = static_cast<float>(30 - warmup_);
    floor_db_ = (floor_db_ * k + smooth_db_) / (k + 1.0f);
    --warmup_;
  } else if (smooth_db_ < floor_db_) {
    floor_db_ += 0.2f * (smooth_db_ - floor_db_);
  } else if (!freeze_floor && smooth_db_ < floor_db_ + 15.0f) {
    floor_db_ += 0.004f * (smooth_db_ - floor_db_);
  }
  // Re-anchor on the room when the floor is far below the last few seconds'
  // quiet fifth: once every half second, and only with two seconds of history.
  window_.push_back(smooth_db_);
  if (window_.size() > 500) {
    window_.erase(window_.begin());
  }
  if (++since_check_ >= 25 && window_.size() >= 100 && warmup_ == 0) {
    since_check_ = 0;
    std::vector<float> sorted = window_;
    std::sort(sorted.begin(), sorted.end());
    const float room = sorted[sorted.size() / 5];
    const float busy = sorted[sorted.size() * 4 / 5];
    // Only after 1.5 s of unbroken "speech" -- no phrase lasts that long, a
    // floor that is wrong makes the room look like one -- and only when the
    // window is STEADY: a room, however loud, holds its level to
    // within a few dB, and speech does not. Without this a phrase inside the
    // window would be taken for the room and the floor lifted to its level.
    if (loud_hops_ >= 150 && room > floor_db_ + 10.0f && busy - room < 8.0f) {
      floor_db_ = room;
      ++reanchors_;
    }
  }
  // A high floor is a noisy microphone, where speech is only ~10 dB above it.
  const float margin = floor_db_ > -45.0f ? 6.0f : 10.0f;
  const bool loud =
      warmup_ == 0 && smooth_db_ > std::max(floor_db_ + margin, -55.0f);
  loud_hops_ = loud ? loud_hops_ + 1 : 0;
  return loud;
}

void HopVad::Reset() {
  *this = HopVad();
}

std::vector<FeatureFrame> NormalisedFeatures(
    base::span<const StaticFrame> frames) {
  const size_t n = frames.size();
  std::vector<FeatureFrame> out(n);
  if (n == 0) {
    return out;
  }
  std::array<float, kStaticDim> mean = {};
  std::array<float, kStaticDim> spread = {};
  for (const StaticFrame& f : frames) {
    for (int d = 0; d < kStaticDim; ++d) {
      mean[d] += f[d];
    }
  }
  for (int d = 0; d < kStaticDim; ++d) {
    mean[d] /= static_cast<float>(n);
  }
  for (const StaticFrame& f : frames) {
    for (int d = 0; d < kStaticDim; ++d) {
      const float x = f[d] - mean[d];
      spread[d] += x * x;
    }
  }
  for (int d = 0; d < kStaticDim; ++d) {
    spread[d] = std::sqrt(spread[d] / static_cast<float>(n)) + 1e-3f;
  }
  std::vector<StaticFrame> norm(n);
  for (size_t t = 0; t < n; ++t) {
    for (int d = 0; d < kStaticDim; ++d) {
      norm[t][d] = (frames[t][d] - mean[d]) / spread[d];
    }
  }
  const auto at = [&](long i) -> const StaticFrame& {
    return norm[static_cast<size_t>(std::clamp<long>(i, 0, static_cast<long>(n) - 1))];
  };
  for (size_t t = 0; t < n; ++t) {
    const long i = static_cast<long>(t);
    for (int d = 0; d < kStaticDim; ++d) {
      out[t][d] = norm[t][d];
      out[t][kStaticDim + d] =
          (at(i + 2)[d] - at(i - 2)[d] + 2.0f * (at(i + 1)[d] - at(i - 1)[d])) /
          10.0f;
    }
  }
  return out;
}

DtwResult OpenEndDtw(base::span<const FeatureFrame> tmpl,
                     base::span<const FeatureFrame> query,
                     float band) {
  DtwResult result;
  const size_t m = tmpl.size();
  const size_t n = query.size();
  if (m == 0 || n == 0) {
    result.end = n;
    return result;
  }
  const size_t lo_end = static_cast<size_t>(0.5f * static_cast<float>(m));
  if (n < 2 || n - 1 < lo_end) {
    result.end = n;
    return result;
  }
  constexpr float kInf = 1e9f;
  const size_t stride = n + 1;
  std::vector<float> d((m + 1) * stride, kInf);
  d[0] = 0.0f;
  const float scale = 1.0f / std::sqrt(static_cast<float>(kFeatureDim));
  for (size_t i = 1; i <= m; ++i) {
    const long lo = std::max<long>(
        1, static_cast<long>(static_cast<float>(i) * (1.0f - band)) - 3);
    const long hi = std::min<long>(
        static_cast<long>(n),
        static_cast<long>(static_cast<float>(i) * (1.0f + band * 2.0f)) + 3);
    for (long j = lo; j <= hi; ++j) {
      float sum = 0.0f;
      for (int k = 0; k < kFeatureDim; ++k) {
        const float diff = tmpl[i - 1][k] - query[static_cast<size_t>(j - 1)][k];
        sum += diff * diff;
      }
      const float cost = std::sqrt(sum) * scale;
      const size_t at = i * stride + static_cast<size_t>(j);
      d[at] = cost + std::min({d[at - stride], d[at - 1], d[at - stride - 1]});
    }
  }
  const size_t hi_end = std::min(n - 1, static_cast<size_t>(1.8f * static_cast<float>(m)));
  if (hi_end < lo_end) {
    result.end = n;
    return result;
  }
  float best = std::numeric_limits<float>::max();
  size_t best_j = lo_end;
  for (size_t j = lo_end; j <= hi_end; ++j) {
    // Column j+1 in 1-based terms, so the path length is m + (j + 1).
    const float norm =
        d[m * stride + j + 1] / static_cast<float>(m + j + 1);
    if (norm < best) {
      best = norm;
      best_j = j;
    }
  }
  result.distance = best;
  result.end = best_j + 1;
  return result;
}

float PitchHz(base::span<const float> samples, size_t center) {
  constexpr size_t kWindow = 640;
  constexpr int kMinLag = kSampleRate / 400;
  constexpr int kMaxLag = kSampleRate / 70;
  const size_t start = center > kWindow / 2 ? center - kWindow / 2 : 0;
  if (start + kWindow > samples.size()) {
    return 0.0f;
  }
  std::array<float, kWindow> seg;
  float mean = 0.0f;
  for (size_t i = 0; i < kWindow; ++i) {
    seg[i] = samples[start + i];
    mean += seg[i];
  }
  mean /= static_cast<float>(kWindow);
  float energy = 0.0f;
  for (float& v : seg) {
    v -= mean;
    energy += v * v;
  }
  if (energy < 1e-6f) {
    return 0.0f;
  }
  float best = -1.0f;
  int best_lag = 0;
  for (int lag = kMinLag; lag < kMaxLag; ++lag) {
    float ac = 0.0f;
    for (size_t i = 0; i + lag < kWindow; ++i) {
      ac += seg[i] * seg[i + lag];
    }
    ac /= energy;
    if (ac > best) {
      best = ac;
      best_lag = lag;
    }
  }
  if (best < 0.45f) {
    return 0.0f;
  }
  return static_cast<float>(kSampleRate) / static_cast<float>(best_lag);
}

SpeakerVector ComputeSpeakerVector(base::span<const float> samples,
                                   const Frames& frames,
                                   size_t first,
                                   size_t last) {
  SpeakerVector v;
  v.fill(0.0f);
  last = std::min(last, frames.features.size());
  if (first >= last) {
    return v;
  }
  float peak = -1e9f;
  for (size_t t = first; t < last; ++t) {
    peak = std::max(peak, frames.energy_db[t]);
  }
  std::vector<size_t> strong;
  for (size_t t = first; t < last; ++t) {
    if (frames.energy_db[t] > peak - 20.0f) {
      strong.push_back(t);
    }
  }
  if (strong.empty()) {
    return v;
  }
  const float count = static_cast<float>(strong.size());
  for (int d = 0; d < kCepstra; ++d) {
    float mean = 0.0f;
    for (size_t t : strong) {
      mean += frames.features[t][d];
    }
    mean /= count;
    float var = 0.0f;
    for (size_t t : strong) {
      const float x = frames.features[t][d] - mean;
      var += x * x;
    }
    v[d] = mean;
    v[kCepstra + d] = std::sqrt(var / count);
  }
  std::vector<float> pitches;
  for (size_t t : strong) {
    const float p = PitchHz(samples, t * kFrameHop + kFrameLength / 2);
    if (p > 0.0f) {
      pitches.push_back(p);
    }
  }
  if (!pitches.empty()) {
    std::sort(pitches.begin(), pitches.end());
    const size_t mid = pitches.size() / 2;
    const float median = pitches.size() % 2 == 1
                             ? pitches[mid]
                             : 0.5f * (pitches[mid - 1] + pitches[mid]);
    v[2 * kCepstra] = std::log2(median);
  }
  return v;
}

}  // namespace zephyrus::agent::voice
