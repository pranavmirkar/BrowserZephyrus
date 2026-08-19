// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_CRYPTO_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_CRYPTO_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"

namespace zephyrus_privacy {

// Encrypts the only genuinely sensitive values in the privacy database: site
// and request domains. Everything else stored is counts and enums.
//
// Spec §5.3 asks for encryption at rest "the same mechanism as the existing
// cookie and password stores". Chromium has no whole-file SQLite encryption —
// its own History database is plaintext on disk — and the cookie store
// encrypts individual VALUES via the platform keystore. So this is
// column-level encryption, which is what that instruction actually means in
// this codebase.
//
// **A database with no working crypto must not open.** §10 is explicit: if the
// keystore is unavailable, run memory-only for the session rather than writing
// plaintext. Making the encryptor a required constructor argument means the
// plaintext path does not exist to be taken by mistake.
class PrivacyCrypto {
 public:
  virtual ~PrivacyCrypto();

  // Returns nullopt if the keystore is unavailable. The caller must treat that
  // as "do not persist", never as "store it as-is".
  virtual std::optional<std::vector<uint8_t>> Encrypt(
      std::string_view plaintext) = 0;

  // Returns nullopt for corrupt or undecryptable data — which happens
  // legitimately when a profile is copied to another machine, so it is an
  // ordinary case to handle, not an error to shout about.
  virtual std::optional<std::string> Decrypt(
      base::span<const uint8_t> ciphertext) = 0;

  // Keyed digest for the lookup/index columns that sit beside the encrypted
  // values. Spec §5.3.
  //
  // **This must be an HMAC, never a bare hash.** Ciphertext cannot be a key or
  // a UNIQUE constraint because keystore output is non-deterministic (DPAPI),
  // which forces a companion lookup column — and a plain hash there defeats
  // the encryption completely. The domain space is public and enumerable, so
  // an unkeyed digest is reversible by dictionary lookup, and an UNKEYED one
  // is worse than per-file brute force: a single precomputed table breaks
  // every install ever created. With a key held in the keystore, recovery
  // requires the key, which is exactly what the threat model assumes is
  // protected.
  //
  // Truncated to 64 bits: collisions are negligible at our row counts and the
  // index stays compact. The old 32-bit hash was additionally a correctness
  // bug — `domain_hash` is part of tracker_daily's primary key, so a collision
  // silently MERGED two domains' counts, violating §2.
  //
  // Same lifetime rule as Encrypt: no key, no database.
  virtual std::optional<int64_t> KeyedHash(std::string_view value) = 0;

  // True when this object generated a NEW lookup key because the stored one
  // could not be unsealed — a profile copied to another machine, or a keystore
  // reset. Every existing row was keyed and encrypted with the old key, so it
  // is unreadable and its HMACs will never match again. The database uses this
  // to discard those rows instead of accumulating a second, parallel set.
  virtual bool key_was_rotated() const = 0;
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_CRYPTO_H_
