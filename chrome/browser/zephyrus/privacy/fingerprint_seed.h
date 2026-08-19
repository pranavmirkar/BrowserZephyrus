// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_FINGERPRINT_SEED_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_FINGERPRINT_SEED_H_

#include <stdint.h>

#include <array>
#include <string_view>

#include "base/containers/span.h"

namespace zephyrus_privacy {

// §6.5 / §11.5. The root of fingerprinting randomization.
//
// Every perturbed value the browser ever returns traces back to here, so the
// properties below are correctness requirements, not implementation detail.
// §6.5 states them as such: "these are correctness requirements, not style."
//
//  STABLE      Same origin, same session -> identical seed, therefore identical
//              perturbation. A site that reads the same canvas twice and gets
//              two different answers has both detected us AND been handed the
//              tool to remove us: averaging many reads recovers the true value.
//              This is the property that makes randomization worth doing at
//              all, and it is why nothing here consults a clock or a counter.
//
//  UNLINKABLE  Two origins must get unrelated seeds. If evil-a.com could
//              predict the seed evil-b.com will see, the two could confirm they
//              are looking at the same device — which is exactly the linkage
//              fingerprinting protection exists to prevent, reintroduced by the
//              protection itself.
//
//  UNGUESSABLE §11.5: "the seed derivation must not be predictable from
//              anything a page can observe." The only secret input is 32 random
//              bytes from the OS CSPRNG. Nothing derived from the origin, the
//              time, the profile path, or any counter participates, because a
//              page knows or can bound all of those.
//
//  EPHEMERAL   The secret never touches disk and is regenerated per browser
//              session. A persisted secret would be a stable cross-session
//              device identifier that we created — strictly worse than the
//              fingerprint we are perturbing, because it would survive clearing
//              browsing data.

// 256 bits of derived key material for one (origin, session).
//
// Not a bare integer: this is key material for further derivation, and a
// 64-bit value would leave too little room to derive many independent
// per-surface values from it without collisions.
using FingerprintSeed = std::array<uint8_t, 32>;

// The per-session secret. RAM only, for the lifetime of the browser process.
class FingerprintSessionSecret {
 public:
  // Draws from the OS CSPRNG. Cheap enough to call once at startup and never
  // again.
  static FingerprintSessionSecret Generate();

  // For tests that need a known secret. Never call this in production: a fixed
  // secret makes every seed reproducible, which is exactly the cross-session
  // identifier EPHEMERAL exists to prevent.
  static FingerprintSessionSecret CreateForTesting(
      base::span<const uint8_t, 32> bytes);

  FingerprintSessionSecret(const FingerprintSessionSecret&) = delete;
  FingerprintSessionSecret& operator=(const FingerprintSessionSecret&) = delete;
  FingerprintSessionSecret(FingerprintSessionSecret&&);
  FingerprintSessionSecret& operator=(FingerprintSessionSecret&&);
  ~FingerprintSessionSecret();

  // Derives the seed for `origin_key`.
  //
  // **What `origin_key` must be, and why the caller decides.** For an ordinary
  // origin it is the serialization ("https://example.com:443"). For an OPAQUE
  // origin it must be an unguessable per-document token instead, because every
  // opaque origin serializes to the literal "null" — keying on that would hand
  // every sandboxed iframe on the machine one shared seed, making them mutually
  // linkable, which is the precise opposite of the point. This function cannot
  // tell the two cases apart from a string, so the browser layer that holds the
  // RenderFrameHost makes the choice and OriginKeyForSeed() below names the
  // rule.
  FingerprintSeed DeriveForOriginKey(std::string_view origin_key) const;

 private:
  FingerprintSessionSecret();

  std::array<uint8_t, 32> key_;
};

// Derives an independent 64-bit value from a seed, one per named surface.
//
// Surfaces must not share a value. If the canvas noise and the WebGL vendor
// choice were the same number, a site could read one to learn the other, and a
// site that only ever touches canvas would leak what our WebGL answer will be.
// `label` is a compile-time constant naming the surface, never page-controlled
// input.
uint64_t DeriveSurfaceValue(const FingerprintSeed& seed, std::string_view label);

// The string to pass to DeriveForOriginKey() for a non-opaque origin. Exists so
// the format is written once; two callers spelling it differently would give
// the same origin two seeds and break STABLE across, say, a reload.
std::string OriginKeyForSeed(std::string_view scheme,
                             std::string_view host,
                             uint16_t port);

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_FINGERPRINT_SEED_H_
