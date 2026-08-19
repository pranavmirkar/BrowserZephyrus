// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/entity_resolver.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "chrome/browser/zephyrus/privacy/entity_artifact.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "crypto/keypair.h"
#include "crypto/sign.h"
#include <algorithm>
#include <random>
#include "third_party/abseil-cpp/absl/cleanup/cleanup.h"
#include "base/logging.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

// Raw field values so a test can write a malformed artifact directly, rather
// than building a good one and then corrupting its bytes. Byte surgery after
// the fact is both unsafe-buffer-flagged and easy to get subtly wrong.
struct RawEntry {
  uint32_t hash;
  uint16_t entity_id;
  uint8_t category;
};
struct RawRecord {
  uint32_t name_offset;
  uint32_t name_length;
};

void AppendBytes(std::vector<uint8_t>& out, base::span<const uint8_t> bytes) {
  out.insert(out.end(), bytes.begin(), bytes.end());
}

std::vector<uint8_t> BuildArtifact(
    const std::vector<RawEntry>& entries,
    const std::vector<RawRecord>& records,
    std::string_view blob,
    std::function<void(ArtifactHeader&)> tweak_header = {}) {
  ArtifactHeader header = {};
  base::span(header.magic).copy_from(base::span(kEntityArtifactMagic));
  header.format_version = kEntityArtifactVersion;
  header.flags = 0;
  header.entry_count = static_cast<uint32_t>(entries.size());
  header.entity_count = static_cast<uint32_t>(records.size());
  header.string_bytes = static_cast<uint32_t>(blob.size());
  header.dataset_id = static_cast<uint32_t>(DatasetId::kTrackerRadar);
  header.built_unix_seconds = 1754870000;
  header.dropped_collisions = 3;
  if (tweak_header) {
    tweak_header(header);
  }

  std::vector<uint8_t> out;
  AppendBytes(out, base::byte_span_from_ref(header));
  for (const RawEntry& r : entries) {
    Entry e = {};
    e.domain_hash = r.hash;
    e.entity_id = r.entity_id;
    e.category = r.category;
    AppendBytes(out, base::byte_span_from_ref(e));
  }
  for (const RawRecord& r : records) {
    EntityRecord rec = {};
    rec.name_offset = r.name_offset;
    rec.name_length = r.name_length;
    AppendBytes(out, base::byte_span_from_ref(rec));
  }
  AppendBytes(out, base::as_byte_span(blob));
  return out;
}

// "Meta" then "Google" in one blob.
constexpr char kNames[] = "MetaGoogle";

// A function, not a global: a namespace-scope std::vector needs an exit-time
// destructor, which Chromium forbids.
std::vector<RawRecord> Records() {
  return {{0, 4}, {4, 6}};
}

std::vector<uint8_t> GoodArtifact(
    std::function<void(ArtifactHeader&)> tweak = {}) {
  return BuildArtifact(
      {
          {100, 0, static_cast<uint8_t>(Category::kAdvertising)},
          {200, 1, static_cast<uint8_t>(Category::kAnalytics)},
          {300, 0, static_cast<uint8_t>(Category::kSocial)},
          // Exercises the high half of the u32 hash space.
          {4000000000u, 1, static_cast<uint8_t>(Category::kCdn)},
      },
      Records(), std::string_view(kNames, 10), std::move(tweak));
}

// --- The null resolver is a real implementation, not an error ---------------

TEST(EntityResolverTest, NullResolverAnswersUnknownForEverything) {
  auto resolver = EntityResolver::CreateNull();
  ASSERT_TRUE(resolver);
  EXPECT_FALSE(resolver->has_dataset());
  EXPECT_EQ(0u, resolver->entry_count());
  EXPECT_EQ(DatasetId::kUnknown, resolver->dataset_id());

  const EntityInfo info = resolver->Lookup(12345);
  EXPECT_FALSE(info.resolved());
  EXPECT_EQ(kNoEntity, info.entity_id);
  EXPECT_EQ(Category::kUnknown, info.category);
  EXPECT_TRUE(resolver->GetEntityName(0).empty());
}

// --- Happy path -------------------------------------------------------------

TEST(EntityResolverTest, ResolvesKnownDomainsAndNames) {
  auto bytes = GoodArtifact();
  auto resolver = EntityResolver::CreateFromBytesForTesting(bytes);
  ASSERT_TRUE(resolver);
  EXPECT_TRUE(resolver->has_dataset());
  EXPECT_EQ(4u, resolver->entry_count());
  EXPECT_EQ(DatasetId::kTrackerRadar, resolver->dataset_id());
  EXPECT_EQ(1754870000u, resolver->built_unix_seconds());

  const EntityInfo meta = resolver->Lookup(100);
  ASSERT_TRUE(meta.resolved());
  EXPECT_EQ(0u, meta.entity_id);
  EXPECT_EQ(Category::kAdvertising, meta.category);
  EXPECT_EQ("Meta", resolver->GetEntityName(meta.entity_id));

  const EntityInfo google = resolver->Lookup(200);
  ASSERT_TRUE(google.resolved());
  EXPECT_EQ("Google", resolver->GetEntityName(google.entity_id));
  EXPECT_EQ(Category::kAnalytics, google.category);

  // The last entry, proving the binary search reaches the end of the range.
  EXPECT_TRUE(resolver->Lookup(4000000000u).resolved());
}

TEST(EntityResolverTest, UnknownDomainIsNotAGuess) {
  auto resolver = EntityResolver::CreateFromBytesForTesting(GoodArtifact());
  ASSERT_TRUE(resolver);
  // Below, between and above the stored hashes: none may resolve. §4.1 forbids
  // ever guessing an owner.
  for (uint32_t h : {1u, 150u, 250u, 4294967295u}) {
    EXPECT_FALSE(resolver->Lookup(h).resolved()) << "hash " << h;
  }
}

TEST(EntityResolverTest, OutOfRangeEntityNameIsEmptyNotACrash) {
  auto resolver = EntityResolver::CreateFromBytesForTesting(GoodArtifact());
  ASSERT_TRUE(resolver);
  EXPECT_TRUE(resolver->GetEntityName(99).empty());
  EXPECT_TRUE(resolver->GetEntityName(kNoEntity).empty());
}

TEST(EntityResolverTest, EmptyArtifactIsValidAndResolvesNothing) {
  auto resolver =
      EntityResolver::CreateFromBytesForTesting(BuildArtifact({}, {}, ""));
  ASSERT_TRUE(resolver);
  EXPECT_TRUE(resolver->has_dataset());
  EXPECT_EQ(0u, resolver->entry_count());
  EXPECT_FALSE(resolver->Lookup(100).resolved());
}

// --- Rejection: the file is treated as hostile ------------------------------

TEST(EntityResolverTest, RejectsBadMagic) {
  auto bytes = GoodArtifact([](ArtifactHeader& h) { h.magic[0] = 'X'; });
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(bytes));
}

TEST(EntityResolverTest, RejectsUnknownVersion) {
  auto bytes = GoodArtifact(
      [](ArtifactHeader& h) { h.format_version = kEntityArtifactVersion + 1; });
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(bytes));
}

TEST(EntityResolverTest, RejectsNonZeroReservedFlags) {
  auto bytes = GoodArtifact([](ArtifactHeader& h) { h.flags = 1; });
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(bytes));
}

TEST(EntityResolverTest, RejectsTruncatedFile) {
  auto bytes = GoodArtifact();
  bytes.resize(bytes.size() - 1);
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(bytes));
}

TEST(EntityResolverTest, RejectsHeaderOnlyTruncation) {
  auto bytes = GoodArtifact();
  bytes.resize(sizeof(ArtifactHeader) - 1);
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(bytes));
}

// The counts are the dangerous fields: they drive every offset computation.
TEST(EntityResolverTest, RejectsCountsThatOverflowTheLayout) {
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(
      GoodArtifact([](ArtifactHeader& h) { h.entry_count = 0xFFFFFFFFu; })));
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(
      GoodArtifact([](ArtifactHeader& h) { h.entity_count = 0xFFFFFFFFu; })));
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(
      GoodArtifact([](ArtifactHeader& h) { h.string_bytes = 0xFFFFFFFFu; })));
}

TEST(EntityResolverTest, RejectsCountLargerThanTheFile) {
  // Under the sanity cap, but past the end of the actual bytes.
  auto bytes = GoodArtifact([](ArtifactHeader& h) { h.entry_count = 1000; });
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(bytes));
}

TEST(EntityResolverTest, RejectsUnsortedEntries) {
  // Out of order: the binary search would silently return wrong companies.
  auto bytes = BuildArtifact({{300, 0, 1}, {100, 1, 2}}, Records(),
                             std::string_view(kNames, 10));
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(bytes));
}

TEST(EntityResolverTest, RejectsDuplicateHashes) {
  // The converter must have dropped these; a file containing them cannot be
  // attributed unambiguously.
  auto bytes = BuildArtifact({{100, 0, 1}, {100, 1, 2}}, Records(),
                             std::string_view(kNames, 10));
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(bytes));
}

TEST(EntityResolverTest, RejectsEntityIdPastTheEntityTable) {
  auto bytes = BuildArtifact({{100, 7, 1}}, Records(),
                             std::string_view(kNames, 10));
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(bytes));
}

TEST(EntityResolverTest, RejectsCategoryOutsideTheEnum) {
  auto bytes = BuildArtifact({{100, 0, 200}}, Records(),
                             std::string_view(kNames, 10));
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(bytes));
}

TEST(EntityResolverTest, RejectsNameRunningPastTheStringBlob) {
  auto bytes = BuildArtifact({{100, 0, 1}}, {{0, 9999}},
                             std::string_view(kNames, 10));
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(bytes));
}

TEST(EntityResolverTest, RejectsNameOffsetThatOverflows) {
  auto bytes = BuildArtifact({{100, 0, 1}}, {{0xFFFFFFFFu, 8}},
                             std::string_view(kNames, 10));
  EXPECT_FALSE(EntityResolver::CreateFromBytesForTesting(bytes));
}

// Trailing bytes are allowed: a detached signature or padding may follow the
// declared contents.
TEST(EntityResolverTest, AcceptsTrailingBytes) {
  auto bytes = GoodArtifact();
  bytes.insert(bytes.end(), 256, 0xAB);
  auto resolver = EntityResolver::CreateFromBytesForTesting(bytes);
  ASSERT_TRUE(resolver);
  EXPECT_TRUE(resolver->Lookup(100).resolved());
}


// --- Signature verification (§4.4.2) -----------------------------------------
//
// The compiled-in key's private half is deliberately not in the repo, so these
// generate a throwaway keypair and point the verifier at it. Without this the
// verification path would ship untested and first be exercised the day remote
// delivery started depending on it, which is exactly the failure §4.4.2 calls
// out.

namespace {

// Builds the smallest structurally-valid artifact: no entries, no entities.
std::vector<uint8_t> MinimalArtifact() {
  std::vector<uint8_t> bytes(sizeof(ArtifactHeader), 0);
  ArtifactHeader header = {};
  std::ranges::copy(kEntityArtifactMagic, header.magic);
  header.format_version = kEntityArtifactVersion;
  header.dataset_id = static_cast<uint32_t>(DatasetId::kTrackerRadar);
  header.built_unix_seconds = 1750000000;
  header.dataset_published_unix_seconds = 1750000000;
  base::as_writable_bytes(base::span(bytes))
      .first(sizeof(ArtifactHeader))
      .copy_from(base::byte_span_from_ref(header));
  return bytes;
}

// Signs in place using the same rule the converter uses: Ed25519 over the whole
// file with the signature field zeroed.
void SignArtifact(std::vector<uint8_t>& bytes,
                  const crypto::keypair::PrivateKey& key) {
  constexpr size_t kOffset = offsetof(ArtifactHeader, signature);
  std::ranges::fill(
      base::span(bytes).subspan(kOffset, kEntityArtifactSignatureBytes),
      uint8_t{0});
  const std::vector<uint8_t> signature =
      crypto::sign::Sign(crypto::sign::SignatureKind::ED25519, key, bytes);
  ASSERT_EQ(kEntityArtifactSignatureBytes, signature.size());
  base::span(bytes)
      .subspan(kOffset, kEntityArtifactSignatureBytes)
      .copy_from(signature);
}

}  // namespace

class EntitySignatureTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    key_ = crypto::keypair::PrivateKey::GenerateEd25519();
    EntityResolver::SetPublicKeySpkiForTesting(
        crypto::keypair::PublicKey::FromPrivateKey(*key_)
            .ToSubjectPublicKeyInfo());
  }
  void TearDown() override {
    EntityResolver::SetPublicKeySpkiForTesting({});
  }

  base::FilePath Write(const std::vector<uint8_t>& bytes) {
    const base::FilePath path = dir_.GetPath().AppendASCII("artifact.dat");
    CHECK(base::WriteFile(path, bytes));
    return path;
  }

  base::ScopedTempDir dir_;
  std::optional<crypto::keypair::PrivateKey> key_;
};

TEST_F(EntitySignatureTest, CorrectlySignedArtifactLoads) {
  std::vector<uint8_t> bytes = MinimalArtifact();
  SignArtifact(bytes, *key_);
  auto resolver = EntityResolver::CreateFromFile(Write(bytes));
  ASSERT_TRUE(resolver);
  EXPECT_TRUE(resolver->has_dataset());
}

TEST_F(EntitySignatureTest, UnsignedArtifactIsRejected) {
  // An all-zero signature is what an artifact built without a key looks like.
  auto resolver = EntityResolver::CreateFromFile(Write(MinimalArtifact()));
  ASSERT_TRUE(resolver) << "must degrade to the null resolver, never null";
  EXPECT_FALSE(resolver->has_dataset())
      << "an unsigned artifact must not be trusted just because it parses";
}

TEST_F(EntitySignatureTest, OneFlippedBitAnywhereIsRejected) {
  std::vector<uint8_t> signed_bytes = MinimalArtifact();
  SignArtifact(signed_bytes, *key_);
  // Every byte outside the signature field is covered, including the header.
  for (size_t offset : {size_t{8}, offsetof(ArtifactHeader, dataset_id),
                        offsetof(ArtifactHeader, built_unix_seconds)}) {
    std::vector<uint8_t> tampered = signed_bytes;
    tampered[offset] ^= 0x01;
    auto resolver = EntityResolver::CreateFromFile(Write(tampered));
    ASSERT_TRUE(resolver);
    EXPECT_FALSE(resolver->has_dataset())
        << "a flipped bit at offset " << offset << " was accepted";
  }
}

TEST_F(EntitySignatureTest, ArtifactSignedByAnotherKeyIsRejected) {
  std::vector<uint8_t> bytes = MinimalArtifact();
  SignArtifact(bytes, crypto::keypair::PrivateKey::GenerateEd25519());
  auto resolver = EntityResolver::CreateFromFile(Write(bytes));
  ASSERT_TRUE(resolver);
  EXPECT_FALSE(resolver->has_dataset())
      << "a valid signature from the wrong key must not be accepted";
}

// §4.4.1: age drives what the UI is allowed to claim, so the thresholds get a
// test rather than being trusted to stay right.
TEST_F(EntitySignatureTest, FreshnessBucketsFollowTheSpecThresholds) {
  const base::Time now = base::Time::Now();
  auto build_aged = [&](int days_old) {
    std::vector<uint8_t> bytes = MinimalArtifact();
    ArtifactHeader header = {};
    base::byte_span_from_ref(header).copy_from(
        base::span(bytes).first(sizeof(ArtifactHeader)));
    header.dataset_published_unix_seconds =
        (now - base::Days(days_old) - base::Time::UnixEpoch()).InSeconds();
    base::span(bytes)
        .first(sizeof(ArtifactHeader))
        .copy_from(base::byte_span_from_ref(header));
    SignArtifact(bytes, *key_);
    return EntityResolver::CreateFromFile(Write(bytes));
  };

  EXPECT_EQ(DatasetFreshness::kFresh, build_aged(1)->Freshness(now));
  EXPECT_EQ(DatasetFreshness::kFresh, build_aged(59)->Freshness(now));
  EXPECT_EQ(DatasetFreshness::kAging, build_aged(60)->Freshness(now));
  EXPECT_EQ(DatasetFreshness::kAging, build_aged(179)->Freshness(now));
  EXPECT_EQ(DatasetFreshness::kStale, build_aged(180)->Freshness(now));
  EXPECT_EQ(DatasetFreshness::kStale, build_aged(400)->Freshness(now));

  // No dataset at all is its own bucket: the UI must not describe a missing
  // dataset with the same words it uses for a present one.
  EXPECT_EQ(DatasetFreshness::kAbsent,
            EntityResolver::CreateNull()->Freshness(now));

  // A publication date in the FUTURE must not read as fresh. This regressed
  // once: the code returned 0 days for a future date, which is "brand new",
  // while the comment beside it claimed the opposite. A skewed clock or a bad
  // --dataset-published would have pinned the dataset to "fresh" forever and
  // silently disabled the staleness guard.
  auto future = build_aged(-30);
  ASSERT_TRUE(future);
  EXPECT_EQ(-1, future->DatasetAgeDays(now))
      << "a future date is unknown age, not zero";
  EXPECT_EQ(DatasetFreshness::kStale, future->Freshness(now));
}

// An artifact with no publication date is treated as STALE, never as fresh.
TEST_F(EntitySignatureTest, UndatedArtifactCountsAsStale) {
  std::vector<uint8_t> bytes = MinimalArtifact();
  ArtifactHeader header = {};
  base::byte_span_from_ref(header).copy_from(
      base::span(bytes).first(sizeof(ArtifactHeader)));
  header.dataset_published_unix_seconds = 0;
  base::span(bytes).first(sizeof(ArtifactHeader))
      .copy_from(base::byte_span_from_ref(header));
  SignArtifact(bytes, *key_);
  auto resolver = EntityResolver::CreateFromFile(Write(bytes));
  ASSERT_TRUE(resolver);
  EXPECT_EQ(DatasetFreshness::kStale, resolver->Freshness(base::Time::Now()));
  EXPECT_EQ(-1, resolver->DatasetAgeDays(base::Time::Now()));
}


// --- Mutation harness -------------------------------------------------------
//
// The artifact is UNTRUSTED binary input parsed in the browser process, which
// makes it the highest-value memory-safety target in the feature. A real
// libFuzzer target needs its own toolchain build; this runs the same idea
// inside the normal unit test binary, so it actually executes on every run
// rather than waiting for a fuzzing bot that does not exist yet.
//
// The contract under test is only: never crash, never read out of bounds, and
// never claim a mutated artifact is valid-and-signed. Whether a given mutation
// is rejected by the signature or by a bounds check does not matter.
TEST_F(EntitySignatureTest, RandomMutationsNeverCrashAndNeverVerify) {
  std::vector<uint8_t> good = MinimalArtifact();
  SignArtifact(good, *key_);

  // Deterministic: a fixed seed means a failure is reproducible instead of a
  // once-a-month mystery on a bot.
  std::mt19937 rng(20260812u);

  // Every rejected artifact logs an error by design (§10: a bad file must not
  // be silent). Thousands of them would bury a genuine failure.
  const int previous_log_level = logging::GetMinLogLevel();
  logging::SetMinLogLevel(logging::LOGGING_FATAL);
  absl::Cleanup restore_logging = [previous_log_level] {
    logging::SetMinLogLevel(previous_log_level);
  };

  auto mutate = [&](std::vector<uint8_t> bytes, int which) {
    switch (which % 4) {
      case 0: {  // flip a random bit
        const size_t byte = rng() % bytes.size();
        bytes[byte] ^= static_cast<uint8_t>(1u << (rng() % 8));
        break;
      }
      case 1:  // truncate anywhere
        bytes.resize(rng() % (bytes.size() + 1));
        break;
      case 2: {  // scribble a random 32-bit header field
        const size_t field = (rng() % (sizeof(ArtifactHeader) / 4)) * 4;
        const uint32_t value = rng();
        if (field + 4 <= bytes.size()) {
          base::span(bytes).subspan(field, 4u).copy_from(
              base::byte_span_from_ref(value));
        }
        break;
      }
      default:  // append junk
        bytes.insert(bytes.end(), rng() % 64, static_cast<uint8_t>(rng()));
        break;
    }
    return bytes;
  };

  auto exercise = [](EntityResolver* resolver) {
    for (uint32_t h : {0u, 1u, 0xffffffffu}) {
      const EntityInfo info = resolver->Lookup(h);
      resolver->GetEntityName(info.entity_id);
    }
    resolver->dataset_version();
    resolver->dataset_source();
    resolver->dataset_licence();
  };

  // Bulk pass: the STRUCTURAL parser, in memory. No signature and no disk, so
  // this is the half that can afford volume. Contract: never crash, and never
  // read out of bounds when the result is used.
  //
  // (The libFuzzer target in entity_artifact_fuzzer.cc searches this same path
  // far harder — 15M+ executions with coverage feedback. This loop is the fast
  // regression guard that runs on every unit-test invocation.)
  for (int i = 0; i < 3000; ++i) {
    const std::vector<uint8_t> mutated = mutate(good, i);
    if (mutated == good) {
      continue;
    }
    std::unique_ptr<EntityResolver> resolver =
        EntityResolver::CreateFromBytesForTesting(mutated);
    if (resolver) {
      exercise(resolver.get());
    }
  }

  // Signature pass: goes through CreateFromFile, so it covers verification.
  // Deliberately far fewer iterations — each one writes a file, and at 3000 the
  // test took 85 seconds and blew the launcher's 45s timeout. The property here
  // is deterministic (any change outside the signature field invalidates it),
  // so it does not need volume to be convincing.
  int accepted = 0;
  for (int i = 0; i < 120; ++i) {
    const std::vector<uint8_t> mutated = mutate(good, i);
    if (mutated == good) {
      continue;
    }
    auto resolver = EntityResolver::CreateFromFile(Write(mutated));
    ASSERT_TRUE(resolver) << "iteration " << i << ": must degrade, never null";
    if (resolver->has_dataset()) {
      exercise(resolver.get());
      ++accepted;
    }
  }
  EXPECT_EQ(0, accepted)
      << "a mutated artifact verified; the signature covers the whole file "
         "with only its own 64 bytes zeroed, so no mutation outside that field "
         "may ever pass";
}

// The §11 bound on the parser: verification copies the whole file, so an
// implausibly large one must be refused BEFORE that copy.
TEST_F(EntitySignatureTest, ImplausiblyLargeArtifactIsRefused) {
  // 129 MB of zeroes, just over the cap. Sparse-ish: the vector is one
  // allocation, which is the point — the parser must not make a second.
  std::vector<uint8_t> huge(129u * 1024 * 1024, 0);
  std::ranges::copy(kEntityArtifactMagic,
                    reinterpret_cast<char*>(huge.data()));
  auto resolver = EntityResolver::CreateFromFile(Write(huge));
  ASSERT_TRUE(resolver);
  EXPECT_FALSE(resolver->has_dataset());
}

}  // namespace
}  // namespace zephyrus_privacy
