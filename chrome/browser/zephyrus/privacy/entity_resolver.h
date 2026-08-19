// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_ENTITY_RESOLVER_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_ENTITY_RESOLVER_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/files/memory_mapped_file.h"
#include "base/time/time.h"
#include "chrome/browser/zephyrus/privacy/entity_artifact.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"

namespace base {
class FilePath;
}

namespace zephyrus_privacy {

// Age of a dataset in whole days, or -1 when it cannot be established.
//
// Free function because two callers need it on different sequences: the
// resolver (privacy sequence) and the service's stats snapshot (UI thread).
// Having each compute it inline is how they drift apart.
//
// A publication date in the FUTURE returns -1, not 0. Zero would mean "brand
// new" and read as fresh, which is precisely backwards: a future date is a
// broken build or a skewed clock, and §4.4.1 exists to stop an artifact
// claiming a currency nobody verified. Unknown age is treated as stale, and a
// nonsense date is unknown age.
int DatasetAgeDaysFrom(uint64_t published_unix_seconds, base::Time now);

struct EntityInfo {
  uint16_t entity_id = kNoEntity;
  Category category = Category::kUnknown;

  bool resolved() const { return entity_id != kNoEntity; }
};

// Maps a domain hash to the company that owns it.
//
// An INTERFACE, deliberately: the dataset behind it is unsettled and may have
// to be swapped or removed entirely for licensing reasons (see
// entity_artifact.h). Nothing above this class may name a dataset.
//
// **A null resolver is a first-class implementation, not an error path.** Spec
// §4 as amended requires the whole feature to work with no dataset at all —
// bare domains, every other capability intact. Making "no data" a resolver
// that always answers kUnknown means every caller handles it by construction,
// rather than every caller needing a null check that one of them will forget.
//
// Resolution happens on the PRIVACY SEQUENCE, not the request path. §8.3 puts
// the lookup on the hot path, but the emission point here turned out to be the
// UI thread (see the spec memory), which is more latency-sensitive than the
// network thread. So emission writes kNoEntity and the consumer resolves while
// draining. The lookup is a binary search over an mmap'd array either way; the
// difference is which thread pays for the page fault.
class EntityResolver {
 public:
  virtual ~EntityResolver();

  // Thread-safe and const: the backing file is read-only and immutable for the
  // resolver's lifetime. Swapping in a component update replaces the whole
  // resolver rather than mutating this one (§8.4).
  virtual EntityInfo Lookup(uint32_t domain_hash) const = 0;

  // Empty when the id is unknown or no dataset is loaded. The returned view
  // points into the mapped file and is valid for the resolver's lifetime.
  virtual std::string_view GetEntityName(uint16_t entity_id) const = 0;

  // False for the null resolver. Callers use it to decide what to SAY, never
  // to decide whether to work: with no dataset the UI shows bare domains and
  // must not claim a site is clean merely because nothing could be attributed.
  virtual bool has_dataset() const = 0;

  // Provenance for internals and for the attribution the licence requires.
  virtual DatasetId dataset_id() const = 0;
  virtual uint64_t built_unix_seconds() const = 0;

  // When the SOURCE dataset was published, which is what §4.4.1 measures
  // staleness against. Zero when unknown, which Freshness() treats as stale:
  // an unknown age must never read as a fresh one.
  virtual uint64_t dataset_published_unix_seconds() const = 0;

  // Attribution, read out of the artifact rather than hardcoded, so the credit
  // the licence requires cannot drift from what actually shipped (§4.2).
  virtual std::string_view dataset_version() const = 0;
  virtual std::string_view dataset_source() const = 0;
  virtual std::string_view dataset_licence() const = 0;
  virtual size_t entry_count() const = 0;

  // Always succeeds. The feature runs on this when no artifact is installed.
  static std::unique_ptr<EntityResolver> CreateNull();

  // §4.4.1. The single place age becomes a policy decision; every surface asks
  // this rather than comparing days itself.
  DatasetFreshness Freshness(base::Time now) const;
  int DatasetAgeDays(base::Time now) const;

  // Returns the null resolver if the file is missing, unreadable, or fails
  // validation. **Never fails open into "no attribution" silently** — the
  // caller is expected to surface `has_dataset() == false` in internals so a
  // rejected artifact is visible rather than looking like a clean web (§10).
  static std::unique_ptr<EntityResolver> CreateFromFile(const base::FilePath& path);

  // Validates a candidate artifact in memory. Exposed for tests and for the
  // converter's self-check; CreateFromFile runs it before mapping anything.
  // Returns nullptr on any inconsistency.
  // Structure only — does NOT verify the signature. Use CreateFromFile with
  // SetPublicKeySpkiForTesting to exercise verification.
  static std::unique_ptr<EntityResolver> CreateFromBytesForTesting(
      base::span<const uint8_t> bytes);

  // Replaces the compiled-in signing key for the rest of the process. Tests
  // only: the real key's private half lives outside the repo.
  static void SetPublicKeySpkiForTesting(base::span<const uint8_t> spki);
};

// Exact lookup by `domain_hash`, falling back to the registrable domain.
//
// **Why the fallback matters.** Entity datasets are keyed by REGISTRABLE
// DOMAIN, but requests go to subdomains: the artifact holds "doubleclick.net"
// while the request went to "securepubads.g.doubleclick.net". Without the
// fallback an exact match is the only match, and in practice almost nothing
// attributes — which the UI renders as a clean page rather than as a missing
// lookup. A subdomain is owned by whoever owns the registrable domain, so
// folding up one level is attribution, not guessing (§4.2).
//
// §9.7: only names that CAN have an owner are folded. An IP literal,
// "localhost", or a single-label intranet name has no registrable domain, and
// folding one would attribute the request to whatever its last two labels
// happen to look like.
//
// `domain` may be empty when the string was dropped at the string table's cap;
// the exact hash lookup still runs, only the fallback is skipped.
//
// Returns an unresolved EntityInfo when `resolver` is null, which is the normal
// no-dataset state (§4.1) rather than an error. Cold path only — callers must
// not put this on the per-request path.
EntityInfo LookupEntityWithFallback(EntityResolver* resolver,
                                    uint32_t domain_hash,
                                    std::string_view domain);

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_ENTITY_RESOLVER_H_
