// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_ENTITY_SIGNING_KEY_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_ENTITY_SIGNING_KEY_H_

#include <stdint.h>

#include <array>

namespace zephyrus_privacy {

// The PUBLIC half of the entity-artifact signing key, X.509
// SubjectPublicKeyInfo DER for Ed25519. Public keys are meant to be published;
// this one is compiled in so the browser can verify an artifact without
// trusting anything on disk beside it.
//
// The private half is NOT in this repository and must never be. Release and CI
// builds sign with a key held in the build system's secret storage; see
// chrome/browser/zephyrus/THIRD_PARTY_DATA.md.
//
// Rotating the key means shipping a browser that carries both the old and new
// public keys for one release, then dropping the old one — otherwise every
// already-installed artifact fails verification at once and every user silently
// loses attribution.
inline constexpr std::array<uint8_t, 44> kEntityArtifactPublicKeySpki = {
    0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00,
    0x4d, 0xd4, 0x57, 0xf8, 0x34, 0x87, 0x83, 0xec, 0x84, 0x90, 0x2f, 0x5e,
    0xc2, 0x98, 0x56, 0x82, 0x7d, 0x6c, 0x3e, 0x07, 0x6a, 0x54, 0x61, 0xff,
    0x73, 0x34, 0x07, 0x66, 0xf3, 0xc2, 0x05, 0x7c};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_ENTITY_SIGNING_KEY_H_
