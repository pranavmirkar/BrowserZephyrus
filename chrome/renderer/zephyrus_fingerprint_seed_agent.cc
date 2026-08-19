// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/zephyrus_fingerprint_seed_agent.h"

#include <map>
#include <vector>

#include "base/no_destructor.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"

namespace zephyrus_privacy {

namespace {

// One agent per frame. A map rather than RenderFrameObserverTracker so the
// lookup stays available to Blink-facing call sites that only hold a
// RenderFrame.
std::map<content::RenderFrame*, FingerprintSeedAgent*>& Agents() {
  static base::NoDestructor<std::map<content::RenderFrame*,
                                     FingerprintSeedAgent*>>
      agents;
  return *agents;
}

}  // namespace

FingerprintSeedAgent::FingerprintSeedAgent(content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {
  Agents()[render_frame] = this;
}

FingerprintSeedAgent::~FingerprintSeedAgent() {
  Agents().erase(render_frame());
}

// static
FingerprintSeedAgent* FingerprintSeedAgent::Get(
    content::RenderFrame* render_frame) {
  auto it = Agents().find(render_frame);
  return it == Agents().end() ? nullptr : it->second;
}

const std::optional<FingerprintSeed>& FingerprintSeedAgent::Seed() {
  if (cached_.has_value()) {
    return *cached_;
  }
  cached_.emplace();  // Cached as "no seed" unless the fetch says otherwise.

  content::RenderFrame* rf = render_frame();
  if (!rf) {
    return *cached_;
  }
  if (!host_.is_bound()) {
    rf->GetBrowserInterfaceBroker().GetInterface(
        host_.BindNewPipeAndPassReceiver());
  }

  std::vector<uint8_t> bytes;
  uint32_t mask = 0;
  if (!host_->GetSeed(&bytes, &mask)) {
    // The browser went away mid-call. Not randomizing is the safe outcome: it
    // returns true values, which is what an unprotected browser does anyway,
    // whereas inventing a local seed here would produce perturbation the
    // browser has no record of and cannot report (§2).
    return *cached_;
  }
  // An empty reply is the browser saying "randomization is off for this
  // document" — the single source of truth, so the renderer never consults a
  // feature flag of its own and the two cannot disagree.
  surface_mask_ = mask;
  if (bytes.size() == std::tuple_size_v<FingerprintSeed>) {
    FingerprintSeed seed;
    base::span(seed).copy_from(base::span(bytes));
    cached_->emplace(seed);
  }
  return *cached_;
}

void FingerprintSeedAgent::DidCommitProvisionalLoad(
    ui::PageTransition transition) {
  // A new document is a new principal and may be a new origin. Keeping the old
  // seed would perturb the new page with the previous page's noise, which both
  // breaks the per-origin rule and would let one site learn another's seed by
  // navigating to it.
  cached_.reset();
  surface_mask_ = 0;
  host_.reset();
}

void FingerprintSeedAgent::OnDestruct() {
  delete this;
}

uint32_t FingerprintSeedAgent::SurfaceMask() {
  Seed();  // Ensures the fetch has happened; the mask arrives with it.
  return surface_mask_;
}

uint32_t ZephyrusContentSettingsAgent::GetZephyrusFingerprintSurfaceMask() {
  auto* agent = FingerprintSeedAgent::Get(render_frame());
  return agent ? agent->SurfaceMask() : 0;
}

ZephyrusContentSettingsAgent::ZephyrusContentSettingsAgent(
    content::RenderFrame* render_frame,
    std::unique_ptr<content_settings::ContentSettingsAgentImpl::Delegate>
        delegate)
    : content_settings::ContentSettingsAgentImpl(render_frame,
                                                 std::move(delegate)) {}

ZephyrusContentSettingsAgent::~ZephyrusContentSettingsAgent() = default;

std::optional<FingerprintSeed>
ZephyrusContentSettingsAgent::GetZephyrusFingerprintSeed() {
  // Blink asks at the moment of a canvas read, which is what makes the fetch
  // lazy: a frame that never reads one never causes an IPC.
  auto* agent = FingerprintSeedAgent::Get(render_frame());
  return agent ? agent->Seed() : std::nullopt;
}

}  // namespace zephyrus_privacy
