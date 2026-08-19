// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/frame/zephyrus_fingerprint_seed.h"

#include "third_party/blink/public/platform/web_content_settings_client.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/renderer/platform/supplementable.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_remote.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"

namespace blink {

std::optional<std::array<uint8_t, 32>> ZephyrusFingerprintSeedFor(
    ExecutionContext* context) {
  // Nullopt for anything that is not a window. A worker, or an OffscreenCanvas
  // on a worker thread, has no frame and therefore no WebContentSettingsClient
  // to ask.
  //
  // That is a real coverage gap, recorded here rather than papered over: a
  // fingerprinting script that moves its canvas work into a worker is not
  // perturbed. The alternative — inventing a worker-side seed — is worse, since
  // it would produce perturbation the browser has no record of and therefore
  // cannot report, which §2 forbids. Closing it properly means giving workers
  // their own seed channel.
  auto* window = DynamicTo<LocalDOMWindow>(context);
  if (!window || !window->GetFrame()) {
    return std::nullopt;
  }
  WebContentSettingsClient* settings =
      window->GetFrame()->GetContentSettingsClient();
  return settings ? settings->GetZephyrusFingerprintSeed() : std::nullopt;
}

std::optional<std::array<uint8_t, 32>> ZephyrusSeedForSurface(
    ExecutionContext* context,
    uint32_t surface_bit) {
  auto* window = DynamicTo<LocalDOMWindow>(context);
  if (!window || !window->GetFrame()) {
    return std::nullopt;
  }
  WebContentSettingsClient* settings =
      window->GetFrame()->GetContentSettingsClient();
  if (!settings ||
      !(settings->GetZephyrusFingerprintSurfaceMask() & surface_bit)) {
    return std::nullopt;
  }
  return settings->GetZephyrusFingerprintSeed();
}

namespace {

// SplitMix64 — see zephyrus_canvas_noise.cc for why a non-cryptographic mix is
// sufficient below a secret seed.
constexpr uint64_t Mix(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

}  // namespace

uint64_t ZephyrusSurfaceValue(const std::array<uint8_t, 32>& seed,
                              uint64_t tag) {
  // Folds all 32 bytes: using only the first eight would discard three quarters
  // of the entropy the browser derived.
  uint64_t v = tag;
  for (size_t i = 0; i < seed.size(); i += 8) {
    uint64_t chunk = 0;
    for (size_t j = 0; j < 8; ++j) {
      chunk = (chunk << 8) | seed[i + j];
    }
    v = Mix(v ^ chunk);
  }
  return v;
}

namespace {

// Holds the reporter pipe and the set of surfaces already reported, for the
// lifetime of one window. A Supplement rather than a static map so it dies with
// the document and cannot leak a pipe across navigations.
class ZephyrusReporter final : public GarbageCollected<ZephyrusReporter>,
                               public Supplement<LocalDOMWindow> {
 public:
  static constexpr char kSupplementName[] = "ZephyrusReporter";

  static ZephyrusReporter& From(LocalDOMWindow& window) {
    auto* self = Supplement<LocalDOMWindow>::From<ZephyrusReporter>(window);
    if (!self) {
      self = MakeGarbageCollected<ZephyrusReporter>(window);
      Supplement<LocalDOMWindow>::ProvideTo(window, self);
    }
    return *self;
  }

  explicit ZephyrusReporter(LocalDOMWindow& window)
      : Supplement<LocalDOMWindow>(window), remote_(&window) {}

  void Report(LocalDOMWindow& window,
              zephyrus_privacy::mojom::blink::FingerprintSurface surface) {
    const uint32_t bit = 1u << static_cast<uint32_t>(surface);
    if (reported_ & bit) {
      return;
    }
    reported_ |= bit;
    if (!remote_.is_bound()) {
      window.GetFrame()->GetBrowserInterfaceBroker().GetInterface(
          remote_.BindNewPipeAndPassReceiver(
              window.GetTaskRunner(TaskType::kMiscPlatformAPI)));
    }
    remote_->ReportFingerprintSurface(surface);
  }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(remote_);
    Supplement<LocalDOMWindow>::Trace(visitor);
  }

 private:
  HeapMojoRemote<zephyrus_privacy::mojom::blink::PrivacyReporter> remote_;
  uint32_t reported_ = 0;
};

}  // namespace

void ZephyrusReportFingerprintSurface(
    ExecutionContext* context,
    zephyrus_privacy::mojom::blink::FingerprintSurface surface) {
  auto* window = DynamicTo<LocalDOMWindow>(context);
  if (!window || !window->GetFrame()) {
    return;  // Workers have no frame; see ZephyrusFingerprintSeedFor.
  }
  ZephyrusReporter::From(*window).Report(*window, surface);
}

}  // namespace blink
