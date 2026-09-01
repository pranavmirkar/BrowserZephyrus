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
#include "third_party/blink/public/mojom/zephyrus/zephyrus_fingerprint_seed.mojom-blink.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"
#include "mojo/public/cpp/bindings/remote.h"

#include <algorithm>

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

// The interface broker to fetch PrivacyReporter from, or nullptr when this
// context has no way to reach it.
//
// FRAMES ONLY, and this is a hard constraint rather than a simplification.
// PrivacyReporter is registered in PopulateChromeFrameBinders and its browser
// side is a content::DocumentService, which needs a RenderFrameHost. No worker
// scope of any kind can bind it:
//
//   - dedicated and shared workers: content exposes NO embedder binder hook
//     for them at all, so there is nowhere to register it even if we wanted to;
//   - service workers: the hook exists
//     (ContentBrowserClient::RegisterBrowserInterfaceBindersForServiceWorker)
//     but Chrome does not register PrivacyReporter in it.
//
// Asking anyway is not a dropped message — it is FATAL. An unbound interface
// request from a worker scope is treated as a bad Mojo message and the browser
// kills the whole renderer process:
//
//   "Received bad user message: No binder found for interface
//    zephyrus_privacy.mojom.PrivacyReporter for the dedicated worker scope"
//
// which takes the page down with it. Reached simply by a worker drawing to an
// OffscreenCanvas and reading it back, with the randomization flag on.
//
// So worker fingerprinting is RANDOMIZED but not REPORTED — recorded as a
// known gap rather than paid for with a tab crash. Randomization and reporting
// are separate wirings; this is the seam between them.
const BrowserInterfaceBrokerProxy* BrokerFor(ExecutionContext* context) {
  auto* window = DynamicTo<LocalDOMWindow>(context);
  if (!window || !window->GetFrame()) {
    return nullptr;
  }
  return &window->GetFrame()->GetBrowserInterfaceBroker();
}

}  // namespace

namespace {

// A SERVICE WORKER's seed, fetched through its own interface broker and cached
// for the life of the worker.
//
// Service workers are the one instrumented context that cannot be handed a seed
// the way every other one is. A document reads it from its frame's content
// settings client; a dedicated worker gets a copy of its creating frame's. A
// service worker has no frame to copy from — it outlives every document it
// serves and is created by none of them. It does have a
// WebContentSettingsClient — a ServiceWorkerContentSettingsProxy — but that
// proxy is built from a registration and carries no seed, which is why the
// accessors below must consult this path before consulting it. Measured before
// it was built: a canvas drawn inside a service worker read back the exact
// unperturbed bytes, so a script could evade the whole feature by moving its
// canvas work into one.
//
// It is still the SAME seed its documents hold, because the seed is keyed on
// origin alone and a service worker is origin-scoped. The browser derives it
// from the worker's own registration, so the renderer still never names an
// origin — GetSeed() takes no arguments here exactly as it does for a frame.
//
// Synchronous for the same reason the frame path is: a service worker can read
// a canvas in its very first turn, and an async seed would serve that read the
// true values. It blocks only the worker's own thread.
class ZephyrusWorkerSeed final : public GarbageCollected<ZephyrusWorkerSeed>,
                                 public Supplement<ExecutionContext> {
 public:
  static constexpr char kSupplementName[] = "ZephyrusWorkerSeed";

  static ZephyrusWorkerSeed& From(ExecutionContext& context) {
    auto* self = Supplement<ExecutionContext>::From<ZephyrusWorkerSeed>(context);
    if (!self) {
      self = MakeGarbageCollected<ZephyrusWorkerSeed>(context);
      Supplement<ExecutionContext>::ProvideTo(context, self);
    }
    return *self;
  }

  explicit ZephyrusWorkerSeed(ExecutionContext& context)
      : Supplement<ExecutionContext>(context) {}

  const std::optional<std::array<uint8_t, 32>>& Seed(ExecutionContext& context) {
    EnsureFetched(context);
    return seed_;
  }

  uint32_t SurfaceMask(ExecutionContext& context) {
    EnsureFetched(context);
    return surface_mask_;
  }

  void Trace(Visitor* visitor) const override {
    Supplement<ExecutionContext>::Trace(visitor);
  }

 private:
  void EnsureFetched(ExecutionContext& context) {
    if (fetched_) {
      return;
    }
    // Set BEFORE the call, not after: a failed fetch must not be retried on
    // every canvas read, which would put a blocking IPC on a hot path.
    fetched_ = true;

    auto* scope = DynamicTo<WorkerGlobalScope>(context);
    if (!scope) {
      return;
    }
    mojo::Remote<zephyrus_privacy::mojom::blink::FingerprintSeedHost> host;
    scope->GetBrowserInterfaceBroker().GetInterface(
        host.BindNewPipeAndPassReceiver());

    Vector<uint8_t> bytes;
    uint32_t mask = 0;
    if (!host->GetSeed(&bytes, &mask)) {
      return;
    }
    // An empty seed is the browser saying "do not randomize" — the flag is off,
    // or this principal has no stable origin to key on. Not an error.
    if (bytes.size() != 32u) {
      return;
    }
    std::array<uint8_t, 32> seed;
    std::copy(bytes.begin(), bytes.end(), seed.begin());
    seed_ = seed;
    surface_mask_ = mask;
  }

  std::optional<std::array<uint8_t, 32>> seed_;
  uint32_t surface_mask_ = 0;
  bool fetched_ = false;
};

// Whether `context` must fetch its own seed instead of reading one from a
// content settings client.
//
// STRICTLY service workers, and the strictness is a safety property rather than
// tidiness. FingerprintSeedHost is registered for frames and for service
// workers only. A dedicated or shared worker that asked for it would be making
// an unbindable interface request, which the browser treats as a bad Mojo
// message and answers by killing the renderer process. Widening this predicate
// to "any worker" would turn a missing seed into a crashed tab.
bool FetchesOwnSeed(ExecutionContext* context) {
  return context && context->IsServiceWorkerGlobalScope();
}

}  // namespace

// A service worker is checked FIRST, ahead of the content settings client.
//
// It has one — a ServiceWorkerContentSettingsProxy — but that proxy carries no
// seed and never will: it is built in //content from a registration, with no
// frame anywhere in the picture to take a seed from. Consulting it first is not
// merely redundant, it is wrong: it answers nullopt authoritatively and the
// self-fetch below is never reached, which is exactly how the first version of
// this left service workers reading true pixels with the browser side fully
// wired and waiting.
std::optional<std::array<uint8_t, 32>> ZephyrusFingerprintSeedFor(
    ExecutionContext* context) {
  if (FetchesOwnSeed(context)) {
    return ZephyrusWorkerSeed::From(*context).Seed(*context);
  }
  WebContentSettingsClient* settings = SettingsClientFor(context);
  return settings ? settings->GetZephyrusFingerprintSeed() : std::nullopt;
}

std::optional<std::array<uint8_t, 32>> ZephyrusSeedForSurface(
    ExecutionContext* context,
    uint32_t surface_bit) {
  if (FetchesOwnSeed(context)) {
    ZephyrusWorkerSeed& worker = ZephyrusWorkerSeed::From(*context);
    if (!(worker.SurfaceMask(*context) & surface_bit)) {
      return std::nullopt;
    }
    return worker.Seed(*context);
  }
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
