// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_ZEPHYRUS_FINGERPRINT_SEED_AGENT_H_
#define CHROME_RENDERER_ZEPHYRUS_FINGERPRINT_SEED_AGENT_H_

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "components/content_settings/renderer/content_settings_agent_impl.h"

#include "chrome/browser/zephyrus/privacy/fingerprint_seed.h"
#include "content/public/renderer/render_frame_observer.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/blink/public/mojom/zephyrus/zephyrus_fingerprint_seed.mojom.h"

namespace content {
class RenderFrame;
}

namespace zephyrus_privacy {

// Renderer-side holder for this document's §6.5 seed.
//
// **Fetch once, cache for the document.** The seed is constant for an
// (origin, session), so re-fetching would be pure cost — and worse, a design
// that re-derived per read is exactly the "never randomize per call" failure
// §6.5 warns about, where a site averages many reads to recover the truth.
//
// **Fetched lazily, but synchronously.** Nothing is fetched for the vast
// majority of documents that never touch an instrumented surface, so the
// blocking hop is paid only by pages that actually probe one. When it is paid,
// it must be synchronous: a page can read a canvas in its first inline script,
// and an async seed would serve that read the true values — leaking the
// fingerprint on precisely the earliest reads a fingerprinting script makes.
class FingerprintSeedAgent : public content::RenderFrameObserver {
 public:
  explicit FingerprintSeedAgent(content::RenderFrame* render_frame);
  FingerprintSeedAgent(const FingerprintSeedAgent&) = delete;
  FingerprintSeedAgent& operator=(const FingerprintSeedAgent&) = delete;

  // The agent for `render_frame`, or null if there is none.
  static FingerprintSeedAgent* Get(content::RenderFrame* render_frame);

  // This document's seed, or nullopt when randomization is off for it.
  // Fetches on first call.
  const std::optional<FingerprintSeed>& Seed();

  // §6.5 per-surface mask; 0 when nothing may perturb.
  uint32_t SurfaceMask();

  // The seed the browser PUSHED for the document about to commit at `url`
  // (fragment removed). Adopted by that commit, so Seed() needs no IPC for it;
  // dropped by any other commit. See DocumentStartPayload.
  void SetPushedSeed(const std::string& url,
                     std::optional<FingerprintSeed> seed,
                     uint32_t surface_mask);

  // content::RenderFrameObserver:
  void DidCommitProvisionalLoad(ui::PageTransition transition) override;
  void OnDestruct() override;

 private:
  ~FingerprintSeedAgent() override;

  // nullopt until fetched; the inner optional is empty when not randomizing.
  std::optional<std::optional<FingerprintSeed>> cached_;
  uint32_t surface_mask_ = 0;

  struct PushedSeed {
    std::string url;
    std::optional<FingerprintSeed> seed;
    uint32_t surface_mask = 0;
  };
  std::optional<PushedSeed> pushed_;
  mojo::Remote<mojom::FingerprintSeedHost> host_;
};

// The frame's content settings client, extended with the §6.5 seed.
//
// Subclassed in chrome rather than adding the seed to ContentSettingsAgentImpl
// itself: that class lives in //components and is shared with other embedders,
// none of which have a Zephyrus seed to give. Chrome already constructs the
// agent, so overriding one virtual here keeps the change inside the embedder
// that owns the feature.
class ZephyrusContentSettingsAgent
    : public content_settings::ContentSettingsAgentImpl {
 public:
  ZephyrusContentSettingsAgent(
      content::RenderFrame* render_frame,
      std::unique_ptr<content_settings::ContentSettingsAgentImpl::Delegate>
          delegate);
  ~ZephyrusContentSettingsAgent() override;

  // blink::WebContentSettingsClient:
  std::optional<std::array<uint8_t, 32>> GetZephyrusFingerprintSeed() override;
  uint32_t GetZephyrusFingerprintSurfaceMask() override;
};

}  // namespace zephyrus_privacy

#endif  // CHROME_RENDERER_ZEPHYRUS_FINGERPRINT_SEED_AGENT_H_
