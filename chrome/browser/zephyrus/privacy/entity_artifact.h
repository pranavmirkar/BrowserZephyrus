// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_ENTITY_ARTIFACT_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_ENTITY_ARTIFACT_H_

#include <stdint.h>

#include <type_traits>

namespace zephyrus_privacy {

// The on-disk entity-attribution artifact: domain hash -> owning entity +
// category, plus the entity display names.
//
// THIS FORMAT IS THE CONTRACT, NOT THE DATASET. Spec §4 as amended
// 2026-08-11: every mainstream entity dataset (DuckDuckGo Tracker Radar,
// Disconnect, Ghostery TrackerDB) is CC BY-NC-SA 4.0, so the choice of source
// may change and a commercial release may have to ship with no dataset at all.
// Nothing above EntityResolver may know which dataset produced this file; one
// converter per source writes this format, and that is the only thing that
// changes.
//
// **Never linked into the binary.** It is a standalone file, mmap'd read-only:
// shared across profiles and processes, page-cache backed, ~0 private memory
// (§8.4). The licensing reason is ShareAlike — a build-time table is an
// adaptation of the source data, and compiling an adaptation into the
// executable blurs a boundary that wants to stay obvious. The engineering
// reason is that a separate file costs nothing and can be updated without
// shipping a browser.
//
// Layout, all little-endian, each section 8-byte aligned:
//
//   [ArtifactHeader]       160 bytes
//   [Entry × entry_count]    8 bytes each, SORTED ASCENDING by domain_hash
//   [EntityRecord × entity_count]  8 bytes each, indexed by entity_id
//   [string blob]           string_bytes of UTF-8, not NUL-terminated
//
// Treat a loaded file as UNTRUSTED. It arrives from a component update, and
// signature verification is a separate layer that may fail open to a stale or
// attacker-supplied file if it is ever misconfigured. EntityArtifactReader
// validates every offset against the real file size before any dereference.

inline constexpr char kEntityArtifactMagic[8] = {'Z', 'E', 'P', 'H',
                                                 'E', 'N', 'T', '2'};
inline constexpr uint32_t kEntityArtifactVersion = 2;

// Ed25519. The signature covers the ENTIRE FILE with the signature field
// itself zeroed, which is the simplest rule that leaves no unsigned bytes: any
// change to the header, the table, or the strings invalidates it.
inline constexpr size_t kEntityArtifactSignatureBytes = 64;

// §4.4.1 staleness thresholds. Constants in ONE place, deliberately: the spec
// requires the UI's wording to degrade with dataset age, and thresholds copied
// into each surface would drift until two screens disagreed about whether the
// data is old.
//
// The risk being managed is not reduced coverage. Trackers rotate domains, so
// an old dataset reports "no trackers detected" on a page that IS tracked —
// a coverage gap silently becomes a false §2 claim.
inline constexpr int kDatasetFreshDays = 60;
inline constexpr int kDatasetStaleDays = 180;

enum class DatasetFreshness {
  // No dataset at all. Bare domains; the UI must not imply a clean page.
  kAbsent,
  // Under 60 days. Normal wording.
  kFresh,
  // 60-180 days. Show the dataset date; drop unqualified coverage language.
  kAging,
  // Over 180 days. "no trackers detected" must become "no KNOWN trackers
  // detected - tracker data last updated {date}".
  kStale,
};

// Which converter produced the file. Provenance only — the runtime must not
// branch on it. It exists so chrome://privacy-internals and the About screen
// can name the dataset for the attribution the licence requires.
enum class DatasetId : uint32_t {
  kUnknown = 0,
  kTrackerRadar = 1,
  kDisconnect = 2,
  kGhosteryTrackerDb = 3,
};

struct ArtifactHeader {
  char magic[8];
  uint32_t format_version;
  uint32_t flags;  // Reserved, must be 0.

  uint32_t entry_count;
  uint32_t entity_count;
  uint32_t string_bytes;
  uint32_t dataset_id;  // DatasetId

  // Seconds since the Unix epoch, when the converter ran. Provenance for
  // reproducing a build; NOT what staleness is measured against.
  uint64_t built_unix_seconds;

  // When the SOURCE DATASET was published upstream. This is the number the
  // §4.4.1 staleness guard uses, and it is deliberately distinct from
  // built_unix_seconds: re-running the converter today over a two-year-old
  // snapshot produces a fresh build of stale data, and measuring the build
  // date would call that current.
  uint64_t dataset_published_unix_seconds;

  // Domains dropped by the converter because two of them shared a 32-bit hash.
  // See the note on Entry below. Surfaced by internals; a sudden jump means
  // the hash or the dataset changed shape.
  uint32_t dropped_collisions;

  // Attribution metadata, as (offset, length) into the string blob. §4.2
  // requires the credit shown in About and the dashboard to come from the
  // artifact itself rather than a hardcoded string, so it cannot drift from
  // whatever actually shipped.
  uint32_t dataset_version_offset;
  uint32_t dataset_version_length;
  uint32_t source_offset;
  uint32_t source_length;
  uint32_t licence_offset;
  uint32_t licence_length;

  // Ed25519 over the whole file with these 64 bytes zeroed. Verified on EVERY
  // load, including a bundled artifact (§4.4.2) — the point is that the
  // verification path is exercised from day one rather than first exercised
  // when remote delivery arrives and it matters.
  uint8_t signature[kEntityArtifactSignatureBytes];

  uint32_t reserved[5];
};
static_assert(sizeof(ArtifactHeader) == 160,
              "header size is part of the format");

// One domain. `domain_hash` is base::PersistentHash of the lowercased host,
// the same function the emission path uses — the two MUST stay in step or
// every lookup misses.
//
// COLLISIONS ARE RESOLVED AT BUILD TIME, NOT HERE. A 32-bit hash over ~10k
// domains has roughly a 1% chance of at least one collision, and a collision
// would attribute a request to the wrong company — a §2 accuracy violation,
// which is worse than not attributing it at all. The runtime cannot tell two
// colliding domains apart, so the converter detects them, drops every domain
// involved, and records the count in the header. Anything left in this table
// is unambiguous.
struct Entry {
  uint32_t domain_hash;
  uint16_t entity_id;
  uint8_t category;  // Category
  uint8_t reserved;
};
static_assert(sizeof(Entry) == 8, "entry size is part of the format");

// Display name for one entity, as an offset into the string blob.
struct EntityRecord {
  uint32_t name_offset;
  uint32_t name_length;
};
static_assert(sizeof(EntityRecord) == 8, "record size is part of the format");

static_assert(std::is_trivially_copyable_v<ArtifactHeader>);
static_assert(std::is_trivially_copyable_v<Entry>);
static_assert(std::is_trivially_copyable_v<EntityRecord>);

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_ENTITY_ARTIFACT_H_
