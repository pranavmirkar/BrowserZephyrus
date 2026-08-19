// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/zephyrus_fingerprint_seed_host.h"

#include <vector>

#include "chrome/browser/zephyrus/privacy/fingerprint_seed_provider.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "content/public/browser/render_frame_host.h"

namespace zephyrus_privacy {

// static
void ZephyrusFingerprintSeedHost::Create(
    content::RenderFrameHost* render_frame_host,
    mojo::PendingReceiver<mojom::FingerprintSeedHost> receiver) {
  CHECK(render_frame_host);
  // Self-owned; DocumentService deletes it with the document.
  new ZephyrusFingerprintSeedHost(*render_frame_host, std::move(receiver));
}

ZephyrusFingerprintSeedHost::ZephyrusFingerprintSeedHost(
    content::RenderFrameHost& render_frame_host,
    mojo::PendingReceiver<mojom::FingerprintSeedHost> receiver)
    : DocumentService(render_frame_host, std::move(receiver)) {}

ZephyrusFingerprintSeedHost::~ZephyrusFingerprintSeedHost() = default;

void ZephyrusFingerprintSeedHost::GetSeed(GetSeedCallback callback) {
  // Empty means "do not randomize". Returning it when the flag is off is what
  // lets the renderer treat the seed as the single source of truth: it never
  // has to consult a feature flag of its own, so browser and renderer cannot
  // end up disagreeing about whether this document is being perturbed.
  const uint32_t mask = FingerprintSurfaceMask();
  if (mask == 0) {
    std::move(callback).Run({}, 0);
    return;
  }

  auto* provider = FingerprintSeedProvider::GetOrCreate(
      render_frame_host().GetBrowserContext());
  if (!provider) {
    std::move(callback).Run({}, 0);
    return;
  }

  // The origin comes from the RenderFrameHost inside SeedForFrame(). Nothing
  // the renderer sent participates, because the message has no fields.
  const FingerprintSeed seed = provider->SeedForFrame(&render_frame_host());
  std::move(callback).Run(std::vector<uint8_t>(seed.begin(), seed.end()),
                          mask);
}

}  // namespace zephyrus_privacy
