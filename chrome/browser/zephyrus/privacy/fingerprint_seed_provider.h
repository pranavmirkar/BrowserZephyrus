// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_FINGERPRINT_SEED_PROVIDER_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_FINGERPRINT_SEED_PROVIDER_H_

#include <string>

#include "base/supports_user_data.h"
#include "chrome/browser/zephyrus/privacy/fingerprint_seed.h"

namespace content {
class BrowserContext;
class RenderFrameHost;
}  // namespace content

namespace zephyrus_privacy {

// Owns the §6.5 session secret and turns a frame into that frame's seed.
//
// **Why this hangs off BrowserContext and not the privacy service.**
// PrivacyIntelligenceService is deliberately absent for off-the-record
// profiles, because §5.2 forbids recording incognito browsing. Sourcing the
// seed from it would therefore leave Private Workspace windows unrandomized —
// making the private mode MORE fingerprintable than the normal one, which is
// the opposite of what a user opening it is asking for. Randomization is
// protection, not recording, so it must not inherit recording's exclusions.
//
// **Why each context gets its OWN secret.** A shared secret would give
// example.com the same perturbation in the regular profile and in a Private
// Workspace. A site could then read its canvas in both and match the values,
// linking the two sessions and defeating the isolation the private window
// exists to provide. Separate secrets make that comparison useless.
class FingerprintSeedProvider : public base::SupportsUserData::Data {
 public:
  // Creates on first use. Never null for a live context.
  static FingerprintSeedProvider* GetOrCreate(content::BrowserContext* context);

  FingerprintSeedProvider(const FingerprintSeedProvider&) = delete;
  FingerprintSeedProvider& operator=(const FingerprintSeedProvider&) = delete;
  ~FingerprintSeedProvider() override;

  // The seed for the document currently committed in `rfh`.
  //
  // Attribution is taken from the RenderFrameHost, never from anything the
  // renderer says — same rule as the §9.2.1 reporter. A renderer that could
  // name its own origin here could ask for another site's seed and use it to
  // predict that site's noise.
  FingerprintSeed SeedForFrame(content::RenderFrameHost* rfh) const;

 private:
  explicit FingerprintSeedProvider(FingerprintSessionSecret secret);

  const FingerprintSessionSecret secret_;
};

// The key `rfh`'s document is seeded under. Exposed for tests, which need to
// assert the opaque-origin rule directly.
//
// A tuple origin keys on its serialization, so every document of that origin
// shares one seed and a reload is stable. An OPAQUE origin — a sandboxed
// iframe, a data: URL — cannot: they all serialize to "null", so keying on that
// would hand every sandboxed frame on the machine a single shared seed and make
// them mutually linkable. Those key on the document's own unguessable token
// instead, which means each gets an independent seed and, correctly, a new one
// after reload: a fresh opaque origin IS a different security principal.
std::string OriginKeyForFrame(content::RenderFrameHost* rfh);

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_FINGERPRINT_SEED_PROVIDER_H_
