// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/fingerprint_seed_provider.h"

#include <utility>

#include "base/memory/ptr_util.h"
#include "base/strings/strcat.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/render_frame_host.h"
#include "url/origin.h"

namespace zephyrus_privacy {

namespace {

constexpr char kUserDataKey[] = "zephyrus_fingerprint_seed_provider";

}  // namespace

FingerprintSeedProvider::FingerprintSeedProvider(
    FingerprintSessionSecret secret)
    : secret_(std::move(secret)) {}

FingerprintSeedProvider::~FingerprintSeedProvider() = default;

// static
FingerprintSeedProvider* FingerprintSeedProvider::GetOrCreate(
    content::BrowserContext* context) {
  if (!context) {
    return nullptr;
  }
  auto* existing = static_cast<FingerprintSeedProvider*>(
      context->GetUserData(kUserDataKey));
  if (existing) {
    return existing;
  }
  // WrapUnique rather than make_unique: the constructor is private so that the
  // secret can only be created here, once per context.
  auto provider = base::WrapUnique(
      new FingerprintSeedProvider(FingerprintSessionSecret::Generate()));
  auto* raw = provider.get();
  context->SetUserData(kUserDataKey, std::move(provider));
  return raw;
}

FingerprintSeed FingerprintSeedProvider::SeedForFrame(
    content::RenderFrameHost* rfh) const {
  return secret_.DeriveForOriginKey(OriginKeyForFrame(rfh));
}

std::optional<FingerprintSeed> FingerprintSeedProvider::SeedForOrigin(
    const url::Origin& origin) const {
  if (origin.opaque()) {
    return std::nullopt;
  }
  return secret_.DeriveForOriginKey(
      OriginKeyForSeed(origin.scheme(), origin.host(), origin.port()));
}

FingerprintSeed FingerprintSeedProvider::SeedForCommit(
    const url::Origin& origin,
    content::RenderFrameHost* rfh) const {
  return secret_.DeriveForOriginKey(OriginKeyForOrigin(origin, rfh));
}

std::string OriginKeyForFrame(content::RenderFrameHost* rfh) {
  if (!rfh) {
    // No frame, no principal. Returning a constant would be worse than it
    // looks — every caller in this state would share one seed — so this is a
    // distinct key that belongs to nothing, and callers with a real frame can
    // never collide with it.
    return "zephyrus/fp/no-frame";
  }
  return OriginKeyForOrigin(rfh->GetLastCommittedOrigin(), rfh);
}

std::string OriginKeyForOrigin(const url::Origin& origin,
                               content::RenderFrameHost* rfh) {
  if (!rfh) {
    return "zephyrus/fp/no-frame";
  }
  if (origin.opaque()) {
    // §6.5's per-origin rule, applied to a principal whose whole point is that
    // it is not equal to any other. Every opaque origin serializes to "null",
    // so the DOCUMENT's unguessable token is the only thing that distinguishes
    // two sandboxed frames from each other. Keying on the serialization would
    // give them all one seed and let a page correlate its own sandboxed frames
    // with everyone else's.
    //
    // Keyed on the FRAME token, because content/public exposes no per-document
    // token. The difference is worth being precise about: two opaque documents
    // loaded one after another into the SAME frame share a seed, where ideally
    // they would not. That is a within-page correlation only — a page can
    // compare noise between two data: URLs it loaded into its own iframe, which
    // tells it nothing it did not already know — and every distinct frame still
    // gets a distinct, unguessable seed, which is the property that stops
    // sandboxed frames being mutually linkable across sites.
    return base::StrCat({"zephyrus/fp/opaque/",
                         rfh->GetGlobalFrameToken().frame_token.ToString()});
  }
  return OriginKeyForSeed(origin.scheme(), origin.host(), origin.port());
}

}  // namespace zephyrus_privacy
