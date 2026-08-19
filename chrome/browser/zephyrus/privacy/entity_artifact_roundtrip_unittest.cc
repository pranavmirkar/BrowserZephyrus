// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// End-to-end: the Python converter writes, the C++ resolver reads.
//
// entity_resolver_unittest.cc builds artifacts with C++ structs, so it proves
// the validator works but says nothing about whether the CONVERTER agrees with
// it. entity_hash_agreement_unittest.cc pins the hash function but not the
// byte layout. This test closes the remaining gap: the bytes below were
// produced by tools/build_entity_artifact.py from a small Tracker Radar
// fixture, and are asserted to load and resolve here.
//
// It catches a whole class of silent failure — struct padding, endianness, or
// field-order drift between struct.pack() and the C++ structs. None of those
// crash; they just make every lookup miss, which looks exactly like a clean
// web.
//
// To regenerate: run the converter over a fixture and dump the output bytes.
// If this fails after a format change, the converter and entity_artifact.h
// have diverged.

#include <stdint.h>

#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/hash/hash.h"
#include "chrome/browser/zephyrus/privacy/entity_artifact.h"
#include "chrome/browser/zephyrus/privacy/entity_resolver.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

// 235 bytes from tools/build_entity_artifact.py --dataset tracker-radar, over
// a fixture of four domains owned by two entities (one deliberately with no
// owner and an unrecognised category). Format v2, UNSIGNED: this is loaded
// through CreateFromBytesForTesting, which checks structure only. Signature
// verification has its own tests -- see entity_resolver_unittest.cc.
//
// Regenerate with tools/build_entity_artifact.py when the format changes; a
// hand-edited blob here would stop proving the converter and the C++ reader
// still agree, which is the entire point of this file.
constexpr uint8_t kConverterOutput[] = {
    0x5a, 0x45, 0x50, 0x48, 0x45, 0x4e, 0x54, 0x32, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x1b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x93, 0x9c, 0x7b, 0x6a,
    0x00, 0x00, 0x00, 0x00, 0x80, 0xe1, 0x4e, 0x68, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00,
    0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xe0, 0xc7, 0x65, 0x55, 0x01, 0x00, 0x01, 0x00,
    0x14, 0x4c, 0x77, 0x61, 0xff, 0xff, 0x00, 0x00, 0xda, 0x42, 0xd3, 0x75,
    0x01, 0x00, 0x02, 0x00, 0xe0, 0xbd, 0xf6, 0xe9, 0x00, 0x00, 0x03, 0x00,
    0x11, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x72, 0x6f, 0x75, 0x6e, 0x64, 0x74, 0x72, 0x69,
    0x70, 0x2d, 0x66, 0x69, 0x78, 0x74, 0x75, 0x72, 0x65, 0x4d, 0x65, 0x74,
    0x61, 0x47, 0x6f, 0x6f, 0x67, 0x6c, 0x65,
};

std::unique_ptr<EntityResolver> LoadConverterOutput() {
  return EntityResolver::CreateFromBytesForTesting(
      base::span(kConverterOutput));
}

TEST(EntityArtifactRoundtripTest, ConverterOutputPassesValidation) {
  auto resolver = LoadConverterOutput();
  ASSERT_TRUE(resolver) << "the C++ validator rejected the converter's output: "
                           "struct layout or field order has drifted";
  EXPECT_TRUE(resolver->has_dataset());
  EXPECT_EQ(DatasetId::kTrackerRadar, resolver->dataset_id());
  EXPECT_EQ(4u, resolver->entry_count());
  EXPECT_NE(0u, resolver->built_unix_seconds());
}

// The real proof: hash a domain the way the browser does at runtime, and find
// the entry the converter wrote for it.
TEST(EntityArtifactRoundtripTest, RuntimeHashFindsConverterEntries) {
  auto resolver = LoadConverterOutput();
  ASSERT_TRUE(resolver);

  const EntityInfo dc =
      resolver->Lookup(base::PersistentHash(std::string_view("doubleclick.net")));
  ASSERT_TRUE(dc.resolved()) << "runtime hash missed a converter-written entry";
  EXPECT_EQ("Google", resolver->GetEntityName(dc.entity_id));
  EXPECT_EQ(Category::kAdvertising, dc.category);

  const EntityInfo ga = resolver->Lookup(
      base::PersistentHash(std::string_view("google-analytics.com")));
  ASSERT_TRUE(ga.resolved());
  EXPECT_EQ("Google", resolver->GetEntityName(ga.entity_id));
  EXPECT_EQ(Category::kAnalytics, ga.category);

  const EntityInfo fb = resolver->Lookup(
      base::PersistentHash(std::string_view("connect.facebook.net")));
  ASSERT_TRUE(fb.resolved());
  EXPECT_EQ("Meta", resolver->GetEntityName(fb.entity_id));
  EXPECT_EQ(Category::kSocial, fb.category);
}

// A domain the dataset knows but whose owner it does not: the category is
// still useful, and inventing a company would violate §4.1.
TEST(EntityArtifactRoundtripTest, OwnerlessDomainKeepsCategoryButNamesNobody) {
  auto resolver = LoadConverterOutput();
  ASSERT_TRUE(resolver);

  const EntityInfo info =
      resolver->Lookup(base::PersistentHash(std::string_view("unowned.example")));
  // The entry exists...
  EXPECT_FALSE(info.resolved());
  EXPECT_EQ(kNoEntity, info.entity_id);
  // ...and the unrecognised source category became UNKNOWN rather than a guess.
  EXPECT_EQ(Category::kUnknown, info.category);
  EXPECT_TRUE(resolver->GetEntityName(info.entity_id).empty());
}

TEST(EntityArtifactRoundtripTest, DomainNotInTheDatasetResolvesToNothing) {
  auto resolver = LoadConverterOutput();
  ASSERT_TRUE(resolver);
  EXPECT_FALSE(
      resolver->Lookup(base::PersistentHash(std::string_view("example.org")))
          .resolved());
}

}  // namespace
}  // namespace zephyrus_privacy
