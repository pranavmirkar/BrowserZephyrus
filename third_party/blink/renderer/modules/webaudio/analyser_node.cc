/*
 * Copyright (C) 2010, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include "third_party/blink/renderer/modules/webaudio/analyser_node.h"
#include "third_party/blink/renderer/core/frame/zephyrus_fingerprint_seed.h"
#include <optional>
#include <array>
#include <algorithm>

#include "third_party/blink/renderer/bindings/modules/v8/v8_analyser_options.h"
#include "third_party/blink/renderer/modules/webaudio/analyser_handler.h"
#include "third_party/blink/renderer/modules/webaudio/audio_graph_tracer.h"
#include "third_party/blink/renderer/modules/webaudio/base_audio_context.h"

namespace blink {

AnalyserNode::AnalyserNode(BaseAudioContext& context) : AudioNode(context) {
  SetHandler(AnalyserHandler::Create(*this, context.sampleRate()));
}

AnalyserNode* AnalyserNode::Create(BaseAudioContext& context,
                                   ExceptionState& exception_state) {
  DCHECK(IsMainThread());

  return MakeGarbageCollected<AnalyserNode>(context);
}

AnalyserNode* AnalyserNode::Create(BaseAudioContext* context,
                                   const AnalyserOptions* options,
                                   ExceptionState& exception_state) {
  DCHECK(IsMainThread());

  AnalyserNode* node = Create(*context, exception_state);

  if (!node) {
    return nullptr;
  }

  node->HandleChannelOptions(options, exception_state);

  node->setFftSize(options->fftSize(), exception_state);
  node->setSmoothingTimeConstant(options->smoothingTimeConstant(),
                                 exception_state);

  // minDecibels and maxDecibels have default values.  Set both of the values
  // at once.
  node->SetMinMaxDecibels(options->minDecibels(), options->maxDecibels(),
                          exception_state);

  return node;
}

AnalyserHandler& AnalyserNode::GetAnalyserHandler() const {
  return static_cast<AnalyserHandler&>(Handler());
}

unsigned AnalyserNode::fftSize() const {
  return GetAnalyserHandler().FftSize();
}

void AnalyserNode::setFftSize(unsigned size, ExceptionState& exception_state) {
  return GetAnalyserHandler().SetFftSize(size, exception_state);
}

unsigned AnalyserNode::frequencyBinCount() const {
  return GetAnalyserHandler().FrequencyBinCount();
}

void AnalyserNode::setMinDecibels(double min, ExceptionState& exception_state) {
  GetAnalyserHandler().SetMinDecibels(min, exception_state);
}

double AnalyserNode::minDecibels() const {
  return GetAnalyserHandler().MinDecibels();
}

void AnalyserNode::setMaxDecibels(double max, ExceptionState& exception_state) {
  GetAnalyserHandler().SetMaxDecibels(max, exception_state);
}

void AnalyserNode::SetMinMaxDecibels(double min,
                                     double max,
                                     ExceptionState& exception_state) {
  GetAnalyserHandler().SetMinMaxDecibels(min, max, exception_state);
}

double AnalyserNode::maxDecibels() const {
  return GetAnalyserHandler().MaxDecibels();
}

void AnalyserNode::setSmoothingTimeConstant(double smoothing_time,
                                            ExceptionState& exception_state) {
  GetAnalyserHandler().SetSmoothingTimeConstant(smoothing_time,
                                                exception_state);
}

double AnalyserNode::smoothingTimeConstant() const {
  return GetAnalyserHandler().SmoothingTimeConstant();
}

namespace {

// Zephyrus §6.5, audio surface.
//
// AudioContext fingerprinting works by rendering a fixed oscillator/compressor
// graph and hashing the samples: tiny per-device differences in the DSP make
// the result stable per machine and near-unique. Perturbing the READ, exactly
// as canvas does, breaks that hash while leaving the audio itself unchanged —
// nothing here is audible, and nothing here alters what is played, only what a
// script is told when it inspects the analysis buffers.
//
// Deterministic in (seed, index, value) for the same reason canvas is: a page
// that reads twice must get the same answer, or it averages the noise away.
template <typename T>
void PerturbAudio(ExecutionContext* context, base::span<T> samples, bool byte) {
  ZephyrusReportFingerprintSurface(context, zephyrus_privacy::mojom::blink::FingerprintSurface::kAudioBuffer);
  std::optional<std::array<uint8_t, 32>> seed =
      ZephyrusSeedForSurface(context, kZephyrusFpAudio);
  if (!seed) {
    return;
  }
  const uint64_t key = ZephyrusSurfaceValue(*seed, kZephyrusSurfaceAudio);
  for (size_t i = 0; i < samples.size(); ++i) {
    // The sample participates, so noise learned on a known-silent buffer does
    // not transfer to a real one.
    const uint64_t bits = static_cast<uint64_t>(samples[i] * 1000.0);
    uint64_t h = key ^ (static_cast<uint64_t>(i) * 0x100000001b3ULL + bits);
    h += 0x9e3779b97f4a7c15ULL;
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
    h ^= h >> 31;
    if (h % 8 != 0) {
      continue;
    }
    if (byte) {
      // Byte buffers are 0-255 like canvas: one step, clamped not wrapped.
      const int v = static_cast<int>(samples[i]) + ((h >> 8) & 1 ? 1 : -1);
      samples[i] = static_cast<T>(std::clamp(v, 0, 255));
    } else {
      // Float buffers are decibels or -1..1 amplitudes. A perturbation far
      // below the quantisation of any real audio path, but enough to move a
      // hash of the buffer.
      samples[i] = static_cast<T>(samples[i] +
                                  ((h >> 8) & 1 ? 1.0f : -1.0f) * 0.0001f);
    }
  }
}

}  // namespace

void AnalyserNode::getFloatFrequencyData(NotShared<DOMFloat32Array> array) {
  GetAnalyserHandler().GetFloatFrequencyData(array.Get(),
                                             context()->currentTime());
  PerturbAudio(GetExecutionContext(), array->AsSpan(), /*byte=*/false);
}

void AnalyserNode::getByteFrequencyData(NotShared<DOMUint8Array> array) {
  GetAnalyserHandler().GetByteFrequencyData(array.Get(),
                                            context()->currentTime());
  PerturbAudio(GetExecutionContext(), array->AsSpan(), /*byte=*/true);
}

void AnalyserNode::getFloatTimeDomainData(NotShared<DOMFloat32Array> array) {
  GetAnalyserHandler().GetFloatTimeDomainData(array.Get());
  PerturbAudio(GetExecutionContext(), array->AsSpan(), /*byte=*/false);
}

void AnalyserNode::getByteTimeDomainData(NotShared<DOMUint8Array> array) {
  GetAnalyserHandler().GetByteTimeDomainData(array.Get());
  PerturbAudio(GetExecutionContext(), array->AsSpan(), /*byte=*/true);
}

void AnalyserNode::ReportDidCreate() {
  GraphTracer().DidCreateAudioNode(this);
}

void AnalyserNode::ReportWillBeDestroyed() {
  GraphTracer().WillDestroyAudioNode(this);
}

}  // namespace blink
