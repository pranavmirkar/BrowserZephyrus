// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_FINGERPRINT_SEED_HOST_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_FINGERPRINT_SEED_HOST_H_

#include "content/public/browser/document_service.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "third_party/blink/public/mojom/zephyrus/zephyrus_fingerprint_seed.mojom.h"

namespace content {
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

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_FINGERPRINT_SEED_HOST_H_
