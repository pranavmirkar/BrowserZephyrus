// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/frame/zephyrus_fingerprint_seed.h"

#include "third_party/blink/public/platform/web_content_settings_client.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/workers/worker_global_scope.h"
#include "third_party/blink/renderer/core/workers/worker_or_worklet_global_scope.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/renderer/platform/supplementable.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_remote.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"

namespace blink {

namespace {

// The content settings client for either kind of context.
//
// A worker has no frame, so the window path returns nothing for it — which is
// why OffscreenCanvas inside a Web Worker went unperturbed and a script could
// evade the whole feature by moving its canvas work off the main thread.
// Workers DO carry a WebContentSettingsClient of their own
// (WorkerOrWorkletGlobalScope::ContentSettingsClient), populated from the
// creating frame, so both contexts can answer once this looks in both places.
WebContentSettingsClient* SettingsClientFor(ExecutionContext* context) {
  if (auto* window = DynamicTo<LocalDOMWindow>(context)) {
    return window->GetFrame() ? window->GetFrame()->GetContentSettingsClient()
                              : nullptr;
  }
  if (auto* scope = DynamicTo<WorkerOrWorkletGlobalScope>(context)) {
    return scope->ContentSettingsClient();
  }
  return nullptr;
}

// The interface broker for either kind of context. There is no
// ExecutionContext::GetBrowserInterfaceBroker(), so this branches — but it
// branches in ONE place, like SettingsClientFor above, rather than at each
// caller.
const BrowserInterfaceBrokerProxy* BrokerFor(ExecutionContext* context) {
  if (auto* window = DynamicTo<LocalDOMWindow>(context)) {
    return window->GetFrame() ? &window->GetFrame()->GetBrowserInterfaceBroker()
                              : nullptr;
  }
  if (auto* scope = DynamicTo<WorkerGlobalScope>(context)) {
    return &scope->GetBrowserInterfaceBroker();
  }
  return nullptr;
}

}  // namespace

std::optional<std::array<uint8_t, 32>> ZephyrusFingerprintSeedFor(
    ExecutionContext* context) {
  WebContentSettingsClient* settings = SettingsClientFor(context);
  return settings ? settings->GetZephyrusFingerprintSeed() : std::nullopt;
}

std::optional<std::array<uint8_t, 32>> ZephyrusSeedForSurface(
    ExecutionContext* context,
    uint32_t surface_bit) {
  WebContentSettingsClient* settings = SettingsClientFor(context);
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
// Keyed on ExecutionContext, not LocalDOMWindow: a worker is an
// ExecutionContext too (and ExecutionContext is Supplementable<ExecutionContext>),
// so one implementation covers documents and workers. Keyed on the window
// alone, worker fingerprinting was randomized but never reported — invisible
// on the dashboard and uncounted by the attempt heuristic, which classifies on
// how many distinct surfaces a context touches.
class ZephyrusReporter final : public GarbageCollected<ZephyrusReporter>,
                               public Supplement<ExecutionContext> {
 public:
  static constexpr char kSupplementName[] = "ZephyrusReporter";

  static ZephyrusReporter& From(ExecutionContext& context) {
    auto* self = Supplement<ExecutionContext>::From<ZephyrusReporter>(context);
    if (!self) {
      self = MakeGarbageCollected<ZephyrusReporter>(context);
      Supplement<ExecutionContext>::ProvideTo(context, self);
    }
    return *self;
  }

  explicit ZephyrusReporter(ExecutionContext& context)
      : Supplement<ExecutionContext>(context), remote_(&context) {}

  void Report(ExecutionContext& context,
              zephyrus_privacy::mojom::blink::FingerprintSurface surface) {
    const uint32_t bit = 1u << static_cast<uint32_t>(surface);
    if (reported_ & bit) {
      return;
    }
    reported_ |= bit;
    if (!remote_.is_bound()) {
      const BrowserInterfaceBrokerProxy* broker = BrokerFor(&context);
      if (!broker) {
        return;
      }
      broker->GetInterface(remote_.BindNewPipeAndPassReceiver(
          context.GetTaskRunner(TaskType::kMiscPlatformAPI)));
    }
    remote_->ReportFingerprintSurface(surface);
  }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(remote_);
    Supplement<ExecutionContext>::Trace(visitor);
  }

 private:
  HeapMojoRemote<zephyrus_privacy::mojom::blink::PrivacyReporter> remote_;
  uint32_t reported_ = 0;
};

}  // namespace

void ZephyrusReportFingerprintSurface(
    ExecutionContext* context,
    zephyrus_privacy::mojom::blink::FingerprintSurface surface) {
  if (!context) {
    return;
  }
  ZephyrusReporter::From(*context).Report(*context, surface);
}

}  // namespace blink
