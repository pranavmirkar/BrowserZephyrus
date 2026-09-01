// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_FINGERPRINT_SEED_HOST_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_FINGERPRINT_SEED_HOST_H_

#include <cstdint>
#include <vector>

#include "content/public/browser/document_service.h"
#include "content/public/browser/service_worker_version_base_info.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "third_party/blink/public/mojom/zephyrus/zephyrus_fingerprint_seed.mojom.h"

namespace content {
class BrowserContext;
class RenderFrameHost;
}

namespace zephyrus_privacy {

// Browser side of §6.5 seed delivery, scoped to a document.
//
// **The trust boundary, restated because this one hands something OUT.** The
// reporter host next door receives renderer claims and refuses to believe them.
// This interface is the other direction, which makes the question "what may a
// compromised renderer obtain?" The answer is: the seed for the document it is
// already running, and nothing else.
//
//  - The origin is read from render_frame_host(), never from the message. The
//    interface takes no arguments at all, so there is no field through which a
//    renderer could request another site's seed.
//  - DocumentService ends the receiver at navigation, so a seed cannot be
//    fetched for a document that has been replaced.
//  - The session secret never crosses. Only the derived per-origin seed does,
//    and a derived seed reveals nothing about the secret or about any other
//    origin's seed.
class ZephyrusFingerprintSeedHost
    : public content::DocumentService<mojom::FingerprintSeedHost> {
 public:
  static void Create(content::RenderFrameHost* render_frame_host,
                     mojo::PendingReceiver<mojom::FingerprintSeedHost> receiver);

  ZephyrusFingerprintSeedHost(const ZephyrusFingerprintSeedHost&) = delete;
  ZephyrusFingerprintSeedHost& operator=(const ZephyrusFingerprintSeedHost&) =
      delete;

  // mojom::FingerprintSeedHost:
  void GetSeed(GetSeedCallback callback) override;

 private:
  ZephyrusFingerprintSeedHost(
      content::RenderFrameHost& render_frame_host,
      mojo::PendingReceiver<mojom::FingerprintSeedHost> receiver);
  ~ZephyrusFingerprintSeedHost() override;
};

// The same interface for a SERVICE WORKER, which has no frame at all.
//
// **Why a separate class.** The one above is a content::DocumentService, which
// is exactly right for a document — it dies at navigation — and impossible for
// a service worker, which outlives every document it serves and is owned by
// none of them.
//
// **Why the seed is captured at bind time rather than looked up per call.**
// Two reasons, and the second is the load-bearing one:
//
//  1. The seed is constant for (origin, session), so a later lookup could not
//     return anything different.
//  2. It means this object holds no BrowserContext pointer. A service worker's
//     lifetime is not tied to any profile-owned object we could observe, so a
//     retained BrowserContext* would be a use-after-free waiting for a profile
//     teardown to happen in the wrong order. Thirty-two bytes and a mask have
//     no such problem.
//
// The origin still comes from the browser — from the worker's own
// ServiceWorkerVersionBaseInfo — never from the renderer, so the "a renderer
// cannot name another site's seed" property is unchanged: the message still has
// no fields.
class ZephyrusFingerprintSeedServiceWorkerHost
    : public mojom::FingerprintSeedHost {
 public:
  static void Create(
      const content::ServiceWorkerVersionBaseInfo& service_worker_info,
      mojo::PendingReceiver<mojom::FingerprintSeedHost> receiver);

  ZephyrusFingerprintSeedServiceWorkerHost(std::vector<uint8_t> seed,
                                           uint32_t enabled_surfaces);
  ~ZephyrusFingerprintSeedServiceWorkerHost() override;

  ZephyrusFingerprintSeedServiceWorkerHost(
      const ZephyrusFingerprintSeedServiceWorkerHost&) = delete;
  ZephyrusFingerprintSeedServiceWorkerHost& operator=(
      const ZephyrusFingerprintSeedServiceWorkerHost&) = delete;

  // mojom::FingerprintSeedHost:
  void GetSeed(GetSeedCallback callback) override;

 private:
  const std::vector<uint8_t> seed_;
  const uint32_t enabled_surfaces_;
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_FINGERPRINT_SEED_HOST_H_
