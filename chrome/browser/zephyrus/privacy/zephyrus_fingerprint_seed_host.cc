// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/zephyrus_fingerprint_seed_host.h"

#include <optional>
#include <vector>

#include "chrome/browser/zephyrus/privacy/fingerprint_seed_provider.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "url/origin.h"

namespace zephyrus_privacy {

NavigationSeed SeedForNavigation(content::NavigationHandle* navigation) {
  NavigationSeed result;
  content::RenderFrameHost* rfh = navigation->GetRenderFrameHost();
  const std::optional<url::Origin> origin = navigation->GetOriginToCommit();
  if (!rfh || !origin) {
    return result;  // Unknown: the renderer falls back to asking.
  }
  result.known = true;
  // The same "off" answer GetSeed gives, so a pushed document and a fetched
  // one can never disagree about whether they are perturbed.
  const uint32_t mask = FingerprintSurfaceMask();
  if (mask == 0) {
    return result;
  }
  auto* provider = FingerprintSeedProvider::GetOrCreate(rfh->GetBrowserContext());
  if (!provider) {
    return result;
  }
  const FingerprintSeed seed = provider->SeedForCommit(*origin, rfh);
  result.seed.assign(seed.begin(), seed.end());
  result.surfaces = mask;
  return result;
}

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

// static
void ZephyrusFingerprintSeedServiceWorkerHost::Create(
    const content::ServiceWorkerVersionBaseInfo& service_worker_info,
    mojo::PendingReceiver<mojom::FingerprintSeedHost> receiver) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // Registered UNCONDITIONALLY for every service worker, including when the
  // feature is off. That is deliberate and not merely tidy: an interface
  // request a worker scope cannot bind is treated as a bad Mojo message and
  // the browser kills the renderer. A binder that exists and answers "empty
  // seed, no surfaces" is the only safe way to say "not randomizing".
  std::vector<uint8_t> seed;
  uint32_t mask = FingerprintSurfaceMask();

  if (mask != 0) {
    content::RenderProcessHost* process =
        content::RenderProcessHost::FromID(service_worker_info.process_id);
    auto* provider =
        process ? FingerprintSeedProvider::GetOrCreate(
                      process->GetBrowserContext())
                : nullptr;
    // The origin comes from the worker's own registration, which the browser
    // recorded. Nothing the renderer sent participates — GetSeed() still takes
    // no arguments.
    std::optional<FingerprintSeed> derived =
        provider ? provider->SeedForOrigin(
                       service_worker_info.storage_key.origin())
                 : std::nullopt;
    if (derived) {
      seed.assign(derived->begin(), derived->end());
    } else {
      // No seed means no randomization, and the mask must say so. A non-zero
      // mask beside an empty seed would tell the renderer to perturb with
      // nothing to perturb from.
      mask = 0;
    }
  }

  mojo::MakeSelfOwnedReceiver(
      std::make_unique<ZephyrusFingerprintSeedServiceWorkerHost>(
          std::move(seed), mask),
      std::move(receiver));
}

ZephyrusFingerprintSeedServiceWorkerHost::
    ZephyrusFingerprintSeedServiceWorkerHost(std::vector<uint8_t> seed,
                                             uint32_t enabled_surfaces)
    : seed_(std::move(seed)), enabled_surfaces_(enabled_surfaces) {}

ZephyrusFingerprintSeedServiceWorkerHost::
    ~ZephyrusFingerprintSeedServiceWorkerHost() = default;

void ZephyrusFingerprintSeedServiceWorkerHost::GetSeed(
    GetSeedCallback callback) {
  std::move(callback).Run(seed_, enabled_surfaces_);
}

}  // namespace zephyrus_privacy
