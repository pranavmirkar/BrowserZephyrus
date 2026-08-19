// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/entity_resolver.h"
#include "chrome/browser/zephyrus/privacy/entity_signing_key.h"
#include "chrome/browser/zephyrus/privacy/privacy_host_canon.h"
#include "base/hash/hash.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "crypto/keypair.h"
#include "crypto/sign.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/memory/raw_span.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/check_is_test.h"
#include "base/no_destructor.h"
#include "base/numerics/checked_math.h"

namespace zephyrus_privacy {

namespace {

// A ceiling on anything the file claims, so a corrupt or hostile header cannot
// make us compute enormous sizes before the bounds checks run. The real
// datasets are ~10k domains and a few thousand entities; these are orders of
// magnitude above that and still trivially small.
// Comfortably above a legitimate artifact (~4M entries + 64 MB of strings is
// ~100 MB) and far below anything that threatens the browser's address space.
constexpr size_t kMaxArtifactBytes = 128u * 1024 * 1024;
constexpr uint32_t kMaxEntries = 4'000'000;
constexpr uint32_t kMaxEntities = 200'000;
constexpr uint32_t kMaxStringBytes = 64u * 1024 * 1024;

// The resolver used whenever there is no usable dataset. Answers "unknown" to
// everything, which is the honest answer and keeps every caller on one path.
class NullEntityResolver : public EntityResolver {
 public:
  EntityInfo Lookup(uint32_t domain_hash) const override { return EntityInfo(); }
  std::string_view GetEntityName(uint16_t entity_id) const override {
    return std::string_view();
  }
  bool has_dataset() const override { return false; }
  DatasetId dataset_id() const override { return DatasetId::kUnknown; }
  uint64_t built_unix_seconds() const override { return 0; }
  uint64_t dataset_published_unix_seconds() const override { return 0; }
  std::string_view dataset_version() const override { return {}; }
  std::string_view dataset_source() const override { return {}; }
  std::string_view dataset_licence() const override { return {}; }
  size_t entry_count() const override { return 0; }
};

// Views into a validated buffer. Owns nothing; the buffer owner outlives it.
//
// raw_span rather than span for the member fields: these point into an mmap'd
// file whose owning MappedEntityResolver could in principle be destroyed while
// a view survives, and raw_span is what makes that a detected dangling access
// rather than a silent read of unmapped memory. Required by the
// chromium-rawptr plugin, and the right thing here regardless.
struct ArtifactView {
  ArtifactHeader header = {};
  base::raw_span<const Entry> entries;
  base::raw_span<const EntityRecord> records;
  // A view over the blob. substr() is bounds-checked, which keeps name lookup
  // free of raw pointer arithmetic.
  std::string_view strings;
};

// Validates every claim the header makes against the actual byte count, then
// hands back spans that are safe to index.
//
// This is the feature's first parser of externally-supplied bytes, so it is
// written as if the file were hostile: no field is trusted, all arithmetic is
// overflow-checked, and every section must lie entirely inside the buffer.
// §4.4.2: verification runs on EVERY load, including a bundled artifact, so
// the path is exercised from day one instead of being first exercised the day
// remote delivery starts depending on it.
//
// The signature covers the whole file with its own 64 bytes zeroed — the rule
// that leaves no unsigned bytes. Reconstructing that means copying the buffer,
// which is a few hundred KB once at startup on a blocking sequence, not per
// lookup.
// Overridable only by tests. The compiled-in key's PRIVATE half is not in the
// repo, so a unit test cannot produce a validly-signed artifact without this
// seam — and leaving the verification path untested was the exact failure mode
// §4.4.2 exists to prevent.
std::vector<uint8_t>& TestPublicKeySpki() {
  static base::NoDestructor<std::vector<uint8_t>> key;
  return *key;
}

bool VerifySignature(base::span<const uint8_t> bytes) {
  if (bytes.size() < sizeof(ArtifactHeader)) {
    return false;
  }
  static constexpr size_t kSignatureOffset =
      offsetof(ArtifactHeader, signature);
  std::vector<uint8_t> unsigned_copy(bytes.begin(), bytes.end());
  std::vector<uint8_t> signature(
      unsigned_copy.begin() + kSignatureOffset,
      unsigned_copy.begin() + kSignatureOffset +
          kEntityArtifactSignatureBytes);
  std::ranges::fill(
      base::span(unsigned_copy)
          .subspan(kSignatureOffset, kEntityArtifactSignatureBytes),
      uint8_t{0});

  const base::span<const uint8_t> spki =
      TestPublicKeySpki().empty()
          ? base::span<const uint8_t>(kEntityArtifactPublicKeySpki)
          : base::span<const uint8_t>(TestPublicKeySpki());
  const std::optional<crypto::keypair::PublicKey> key =
      crypto::keypair::PublicKey::FromSubjectPublicKeyInfo(spki);
  if (!key) {
    return false;
  }
  return crypto::sign::Verify(crypto::sign::SignatureKind::ED25519, *key,
                              unsigned_copy, signature);
}

bool ParseAndValidate(base::span<const uint8_t> bytes,
                      ArtifactView* out,
                      bool require_signature = true) {
  if (bytes.size() < sizeof(ArtifactHeader)) {
    return false;
  }
  // Bound the input BEFORE verifying, because verification copies the whole
  // file (Ed25519 cannot stream). The structural caps below would catch an
  // absurd entry_count, but they run after the copy — so without this a
  // multi-gigabyte file at the artifact path would be a multi-gigabyte
  // allocation at startup. The signature exists precisely for the case where
  // someone can write that file, so failing before the check is the wrong
  // order of operations.
  if (bytes.size() > kMaxArtifactBytes) {
    LOG(ERROR) << "Zephyrus privacy: entity artifact is implausibly large ("
               << bytes.size() << " bytes); refusing to load it";
    return false;
  }
  if (require_signature && !VerifySignature(bytes)) {
    // Unsigned, tampered with, or signed by a key this build does not carry.
    // Rejecting means bare domains, which §4.1 supports; accepting would mean
    // attributing browsing using data of unknown provenance.
    LOG(ERROR) << "Zephyrus privacy: entity artifact failed signature "
                  "verification; continuing without attribution";
    return false;
  }
  ArtifactHeader header;
  base::as_writable_bytes(base::span_from_ref(header))
      .copy_from(bytes.first(sizeof(ArtifactHeader)));

  if (!std::ranges::equal(header.magic, kEntityArtifactMagic)) {
    return false;
  }
  if (header.format_version != kEntityArtifactVersion || header.flags != 0) {
    return false;
  }
  if (header.entry_count > kMaxEntries || header.entity_count > kMaxEntities ||
      header.string_bytes > kMaxStringBytes) {
    return false;
  }

  // Overflow-checked layout arithmetic. A header claiming entry_count near
  // UINT32_MAX would otherwise wrap a plain multiply and produce an offset
  // that passes a naive bounds check.
  base::CheckedNumeric<size_t> entries_bytes =
      base::CheckMul(size_t{header.entry_count}, sizeof(Entry));
  base::CheckedNumeric<size_t> records_bytes =
      base::CheckMul(size_t{header.entity_count}, sizeof(EntityRecord));

  base::CheckedNumeric<size_t> entries_offset = sizeof(ArtifactHeader);
  base::CheckedNumeric<size_t> records_offset = entries_offset + entries_bytes;
  base::CheckedNumeric<size_t> strings_offset = records_offset + records_bytes;
  base::CheckedNumeric<size_t> total = strings_offset + header.string_bytes;

  size_t entries_off = 0, records_off = 0, strings_off = 0, total_size = 0;
  if (!entries_offset.AssignIfValid(&entries_off) ||
      !records_offset.AssignIfValid(&records_off) ||
      !strings_offset.AssignIfValid(&strings_off) ||
      !total.AssignIfValid(&total_size)) {
    return false;
  }
  // A file larger than its declared contents is allowed (trailing signature or
  // padding); one smaller is not.
  if (total_size > bytes.size()) {
    return false;
  }

  // Every section was just proven to lie inside `bytes`, and each is 8-byte
  // aligned by construction: sizeof(ArtifactHeader) is 160 (a multiple of 8,
  // enforced by the static_assert in entity_artifact.h) and Entry and
  // EntityRecord are both 8, so every offset is a multiple of 8 from a
  // page-aligned mapping. That is what makes the casts below well-defined as
  // well as in-bounds.
  //
  // This comment said "64 bytes" until the v2 header landed. A stale invariant
  // in a parser of untrusted input is worse than no invariant, because it is
  // the thing a reviewer checks the arithmetic against.
  //
  // SAFETY: `total_size <= bytes.size()` was checked above, and each span is
  // constructed from an offset and count that together sum to at most
  // total_size.
  const auto entries = UNSAFE_BUFFERS(base::span<const Entry>(
      reinterpret_cast<const Entry*>(bytes.subspan(entries_off).data()),
      size_t{header.entry_count}));
  const auto records = UNSAFE_BUFFERS(base::span<const EntityRecord>(
      reinterpret_cast<const EntityRecord*>(
          bytes.subspan(records_off).data()),
      size_t{header.entity_count}));
  const auto strings = UNSAFE_BUFFERS(std::string_view(
      reinterpret_cast<const char*>(bytes.subspan(strings_off).data()),
      size_t{header.string_bytes}));

  // Every entity name must lie inside the string blob, and every entry must
  // point at an entity that exists. Checked once here so Lookup() and
  // GetEntityName() can index without re-validating on every call.
  for (uint32_t i = 0; i < header.entity_count; ++i) {
    base::CheckedNumeric<size_t> end =
        base::CheckAdd(size_t{records[i].name_offset},
                       size_t{records[i].name_length});
    size_t end_value = 0;
    if (!end.AssignIfValid(&end_value) || end_value > header.string_bytes) {
      return false;
    }
  }
  for (uint32_t i = 0; i < header.entry_count; ++i) {
    if (entries[i].entity_id != kNoEntity &&
        entries[i].entity_id >= header.entity_count) {
      return false;
    }
    if (entries[i].category > static_cast<uint8_t>(Category::kMaxValue)) {
      return false;
    }
    // Sorted ascending is what makes the binary search correct. An unsorted
    // file is not a memory-safety problem, but it would silently return wrong
    // companies for real domains, which §2 treats as worse than no answer.
    if (i > 0 && entries[i].domain_hash <= entries[i - 1].domain_hash) {
      return false;
    }
  }

  out->header = header;
  out->entries = entries;
  out->records = records;
  out->strings = strings;
  return true;
}

// Shared lookup logic over a validated view.
class ValidatedResolver : public EntityResolver {
 public:
  explicit ValidatedResolver(ArtifactView view) : view_(view) {}

  EntityInfo Lookup(uint32_t domain_hash) const override {
    // Sorted, so binary search. The converter guarantees hashes are unique
    // (colliding domains are dropped at build time), so a hit is unambiguous.
    const auto it = std::lower_bound(
        view_.entries.begin(), view_.entries.end(), domain_hash,
        [](const Entry& e, uint32_t h) { return e.domain_hash < h; });
    if (it == view_.entries.end() || it->domain_hash != domain_hash) {
      return EntityInfo();
    }
    EntityInfo info;
    info.entity_id = it->entity_id;
    info.category = static_cast<Category>(it->category);
    return info;
  }

  std::string_view GetEntityName(uint16_t entity_id) const override {
    if (entity_id == kNoEntity || entity_id >= view_.records.size()) {
      return std::string_view();
    }
    const EntityRecord& rec = view_.records[entity_id];
    // Bounds were proven at load, and substr() re-checks them anyway.
    return view_.strings.substr(rec.name_offset, rec.name_length);
  }

  bool has_dataset() const override { return true; }
  DatasetId dataset_id() const override {
    return static_cast<DatasetId>(view_.header.dataset_id);
  }
  uint64_t built_unix_seconds() const override {
    return view_.header.built_unix_seconds;
  }
  uint64_t dataset_published_unix_seconds() const override {
    return view_.header.dataset_published_unix_seconds;
  }
  std::string_view dataset_version() const override {
    return Meta(view_.header.dataset_version_offset,
                view_.header.dataset_version_length);
  }
  std::string_view dataset_source() const override {
    return Meta(view_.header.source_offset, view_.header.source_length);
  }
  std::string_view dataset_licence() const override {
    return Meta(view_.header.licence_offset, view_.header.licence_length);
  }
  size_t entry_count() const override { return view_.entries.size(); }

 protected:
  // Bounds were proven at load; substr re-checks them anyway, so a malformed
  // offset yields an empty string rather than a read past the mapping.
  std::string_view Meta(uint32_t offset, uint32_t length) const {
    if (offset > view_.strings.size() ||
        length > view_.strings.size() - offset) {
      return std::string_view();
    }
    return view_.strings.substr(offset, length);
  }

  ArtifactView view_;
};

// Backed by an mmap'd file: ~0 private memory, shared across processes, pages
// evict under pressure (§8.4).
class MappedEntityResolver : public ValidatedResolver {
 public:
  MappedEntityResolver(std::unique_ptr<base::MemoryMappedFile> file,
                       ArtifactView view)
      : ValidatedResolver(view), file_(std::move(file)) {}

 private:
  std::unique_ptr<base::MemoryMappedFile> file_;
};

// Backed by a caller-owned copy. Tests only.
class OwnedEntityResolver : public ValidatedResolver {
 public:
  OwnedEntityResolver(std::vector<uint8_t> bytes, ArtifactView view)
      : ValidatedResolver(view), bytes_(std::move(bytes)) {}

 private:
  std::vector<uint8_t> bytes_;
};

}  // namespace

EntityResolver::~EntityResolver() = default;

// static
int DatasetAgeDaysFrom(uint64_t published_unix_seconds, base::Time now) {
  if (published_unix_seconds == 0) {
    return -1;  // Unknown, which callers must not render as "0 days old".
  }
  // Saturating on purpose: `published` comes out of the artifact header, and a
  // nonsense value must produce a nonsense-but-safe answer rather than
  // overflowing the arithmetic.
  const base::TimeDelta age = now - base::Time::UnixEpoch() -
                              base::Seconds(published_unix_seconds);
  if (age.is_negative()) {
    // Published in the future: a broken build or a skewed clock. Reported as
    // UNKNOWN, never as zero -- zero would mean brand new and be rendered
    // fresh, which would let a bad date silently defeat the staleness guard.
    return -1;
  }
  return age.InDays();
}

int EntityResolver::DatasetAgeDays(base::Time now) const {
  if (!has_dataset()) {
    return -1;
  }
  return DatasetAgeDaysFrom(dataset_published_unix_seconds(), now);
}

DatasetFreshness EntityResolver::Freshness(base::Time now) const {
  if (!has_dataset()) {
    return DatasetFreshness::kAbsent;
  }
  const int days = DatasetAgeDays(now);
  if (days < 0) {
    // Present but undated. Treated as stale on purpose: the whole point of
    // §4.4.1 is that unverified age must not be reported as current.
    return DatasetFreshness::kStale;
  }
  if (days < kDatasetFreshDays) {
    return DatasetFreshness::kFresh;
  }
  return days < kDatasetStaleDays ? DatasetFreshness::kAging
                                  : DatasetFreshness::kStale;
}

std::unique_ptr<EntityResolver> EntityResolver::CreateNull() {
  return std::make_unique<NullEntityResolver>();
}

// static
std::unique_ptr<EntityResolver> EntityResolver::CreateFromFile(
    const base::FilePath& path) {
  auto file = std::make_unique<base::MemoryMappedFile>();
  if (!file->Initialize(path)) {
    // Missing or unreadable is the ordinary case on a fresh install, not an
    // error worth shouting about.
    return CreateNull();
  }
  ArtifactView view;
  if (!ParseAndValidate(base::span(file->bytes()), &view)) {
    // Loud, because this one means a file EXISTS and is bad — corrupt, wrong
    // version, or tampered with. Falling back to null is correct (§10: never
    // fail open into unattributed traffic pretending to be attributed), but it
    // must not be silent.
    LOG(ERROR) << "Zephyrus: entity artifact failed validation, ignoring: "
               << path;
    return CreateNull();
  }
  return std::make_unique<MappedEntityResolver>(std::move(file), view);
}

// static
void EntityResolver::SetPublicKeySpkiForTesting(
    base::span<const uint8_t> spki) {
  // This swaps the key that decides whether an artifact is trusted, so it must
  // be unreachable outside tests. It ships in the binary (Chromium has no
  // test-only linkage for this), and a reachable setter would reduce signature
  // verification to "whatever was set last".
  CHECK_IS_TEST();
  TestPublicKeySpki().assign(spki.begin(), spki.end());
}

std::unique_ptr<EntityResolver> EntityResolver::CreateFromBytesForTesting(
    base::span<const uint8_t> bytes) {
  // Skips signature verification, so production must never reach it.
  CHECK_IS_TEST();
  std::vector<uint8_t> owned(bytes.begin(), bytes.end());
  ArtifactView view;
  // Structure only: signature verification is deliberately skipped here so the
  // parser's hostile-input tests can hand-build artifacts. The signature path
  // has its own tests, which sign for real via SetPublicKeySpkiForTesting.
  if (!ParseAndValidate(base::span<const uint8_t>(owned), &view,
                        /*require_signature=*/false)) {
    return nullptr;
  }
  // The view points into `owned`, which the resolver now owns; moving the
  // vector keeps the heap buffer at the same address, so the spans stay valid.
  return std::make_unique<OwnedEntityResolver>(std::move(owned), view);
}

EntityInfo LookupEntityWithFallback(EntityResolver* resolver,
                                    uint32_t domain_hash,
                                    std::string_view domain) {
  if (!resolver) {
    return EntityInfo();
  }
  EntityInfo info = resolver->Lookup(domain_hash);
  if (info.resolved() || !HostCanHaveOwner(domain)) {
    return info;
  }
  const std::string etld1 =
      net::registry_controlled_domains::GetDomainAndRegistry(
          domain, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (etld1.empty() || etld1 == domain) {
    return info;
  }
  return resolver->Lookup(base::PersistentHash(CanonicalHostForHash(etld1)));
}

}  // namespace zephyrus_privacy
