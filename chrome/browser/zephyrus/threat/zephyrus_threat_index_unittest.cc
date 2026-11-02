// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/threat/zephyrus_threat_index.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/numerics/byte_conversions.h"
#include "base/strings/string_split.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_threat {
namespace {

std::array<uint8_t, 16> TestKey() {
  std::array<uint8_t, 16> key;
  for (size_t i = 0; i < key.size(); ++i) {
    key[i] = static_cast<uint8_t>(i);
  }
  return key;
}

std::vector<uint8_t> BuildBlob(std::initializer_list<std::string_view> entries) {
  ZephyrusThreatIndexBuilder builder(TestKey());
  for (const std::string_view e : entries) {
    builder.AddExpression(e);
  }
  return builder.Finish();
}

// The reference vector from the SipHash paper: key 00..0f, input 00..0e.
TEST(ZephyrusThreatIndexTest, SipHashMatchesThePublishedVector) {
  std::array<uint8_t, 15> input;
  for (size_t i = 0; i < input.size(); ++i) {
    input[i] = static_cast<uint8_t>(i);
  }
  EXPECT_EQ(SipHash24(TestKey(), input), 0xa129ca6149be45e5ull);
  // And the empty input, from the same table.
  EXPECT_EQ(SipHash24(TestKey(), base::span<const uint8_t>()),
            0x726fdb47dd0e0e31ull);
}

TEST(ZephyrusThreatIndexTest, ListedHostsAndAddressesMatch) {
  ZephyrusThreatIndex index;
  ASSERT_TRUE(index.Load(BuildBlob({"evil.example/", "bad.example/pay/login.html",
                                    "phish.example/a?token=1"})));
  EXPECT_EQ(index.size(), 3u);

  // A whole host: every subdomain, every path.
  EXPECT_TRUE(index.IsListed(GURL("https://evil.example/")));
  EXPECT_TRUE(index.IsListed(GURL("http://www.evil.example/anything?x=1")));
  EXPECT_TRUE(index.IsListed(GURL("https://a.b.c.evil.example:8443/deep/path")));
  // One address: that path, with or without a query, and nothing else on the host.
  EXPECT_TRUE(index.IsListed(GURL("https://bad.example/pay/login.html")));
  EXPECT_TRUE(index.IsListed(GURL("https://bad.example/pay/login.html?next=/")));
  EXPECT_FALSE(index.IsListed(GURL("https://bad.example/")));
  EXPECT_FALSE(index.IsListed(GURL("https://bad.example/pay/other.html")));
  // A listed address with a query matches that query only.
  EXPECT_TRUE(index.IsListed(GURL("https://phish.example/a?token=1")));
  EXPECT_FALSE(index.IsListed(GURL("https://phish.example/a?token=2")));
  // Neighbours are not caught by accident.
  EXPECT_FALSE(index.IsListed(GURL("https://notevil.example/")));
  EXPECT_FALSE(index.IsListed(GURL("https://evil.example.org/")));
  EXPECT_FALSE(index.IsListed(GURL("https://example/")));
  EXPECT_FALSE(index.IsListed(GURL("not a url")));
  EXPECT_FALSE(index.IsListed(GURL()));
}

TEST(ZephyrusThreatIndexTest, AnEmptyIndexListsNothing) {
  ZephyrusThreatIndex index;
  EXPECT_FALSE(index.IsListed(GURL("https://evil.example/")));
  ASSERT_TRUE(index.Load(BuildBlob({})));
  EXPECT_EQ(index.size(), 0u);
  EXPECT_FALSE(index.IsListed(GURL("https://evil.example/")));
}

TEST(ZephyrusThreatIndexTest, TheKeyDecidesWhatCollides) {
  // The same entries under two keys produce different blobs: the key is real.
  ZephyrusThreatIndexBuilder a(TestKey());
  std::array<uint8_t, 16> other = TestKey();
  other[0] ^= 0xFF;
  ZephyrusThreatIndexBuilder b(other);
  a.AddExpression("evil.example/");
  b.AddExpression("evil.example/");
  EXPECT_NE(a.Finish(), b.Finish());
}

TEST(ZephyrusThreatIndexTest, DuplicatesCollapse) {
  ZephyrusThreatIndex index;
  ASSERT_TRUE(index.Load(BuildBlob({"a.example/", "a.example/", "b.example/"})));
  EXPECT_EQ(index.size(), 2u);
}

TEST(ZephyrusThreatIndexTest, AManyEntryIndexFindsEveryEntryAndNoNeighbour) {
  ZephyrusThreatIndexBuilder builder(TestKey());
  constexpr int kEntries = 20000;
  for (int i = 0; i < kEntries; ++i) {
    builder.AddExpression("host" + std::to_string(i) + ".example/");
  }
  ZephyrusThreatIndex index;
  ASSERT_TRUE(index.Load(builder.Finish()));
  for (int i = 0; i < kEntries; i += 7) {
    EXPECT_TRUE(index.Contains("host" + std::to_string(i) + ".example/")) << i;
  }
  int false_hits = 0;
  for (int i = kEntries; i < kEntries + 20000; ++i) {
    false_hits += index.Contains("host" + std::to_string(i) + ".example/");
  }
  EXPECT_EQ(false_hits, 0);  // 20k probes at ~7e-9 each
}

TEST(ZephyrusThreatIndexTest, LoadRefusesWhatIsNotWellFormed) {
  const std::vector<uint8_t> good = BuildBlob({"a.example/", "b.example/", "c.example/"});
  ZephyrusThreatIndex index;
  ASSERT_TRUE(index.Load(good));

  EXPECT_FALSE(index.Load({}));
  EXPECT_EQ(index.size(), 0u);
  EXPECT_FALSE(index.Load(std::vector<uint8_t>(good.begin(), good.end() - 1)));
  std::vector<uint8_t> longer = good;
  longer.push_back(0);
  EXPECT_FALSE(index.Load(longer));

  const auto patched = [&](size_t at, uint32_t value) {
    std::vector<uint8_t> copy = good;
    base::span(copy).subspan(at, 4u).copy_from(base::U32ToLittleEndian(value));
    return copy;
  };
  EXPECT_FALSE(index.Load(patched(0, 0)));          // magic
  EXPECT_FALSE(index.Load(patched(4, 2)));          // version
  EXPECT_FALSE(index.Load(patched(24, 0xFFFFFFFF)));  // count larger than the file
  EXPECT_FALSE(index.Load(patched(24, 2)));         // count smaller than the file
  // The directory: must start at 0, never decrease, end at the count.
  EXPECT_FALSE(index.Load(patched(ZephyrusThreatIndex::kHeaderBytes, 1)));
  EXPECT_FALSE(index.Load(
      patched(ZephyrusThreatIndex::kHeaderBytes + 65536 * 4, 2)));
  EXPECT_FALSE(index.Load(
      patched(ZephyrusThreatIndex::kHeaderBytes + 100 * 4, 0xFFFFFFFF)));
}

TEST(ZephyrusThreatIndexTest, LoadRefusesAnUnsortedBucket) {
  // Two expressions that share a bucket (a birthday collision in 65,536
  // buckets: a few hundred tries).
  const std::array<uint8_t, 16> key = TestKey();
  std::map<size_t, std::string> by_bucket;
  std::string a;
  std::string b;
  for (int i = 0; b.empty(); ++i) {
    const std::string e = "x" + std::to_string(i) + ".example/";
    const uint64_t h = SipHash24(key, base::as_byte_span(e)) & 0xFFFFFFFFFFFFull;
    const auto [it, inserted] = by_bucket.emplace(static_cast<size_t>(h >> 32), e);
    if (!inserted) {
      a = it->second;
      b = e;
    }
  }
  ZephyrusThreatIndexBuilder builder(key);
  builder.AddExpression(a);
  builder.AddExpression(b);
  std::vector<uint8_t> blob = builder.Finish();
  ZephyrusThreatIndex index;
  ASSERT_TRUE(index.Load(blob));
  EXPECT_TRUE(index.Contains(a));
  EXPECT_TRUE(index.Contains(b));

  // Swap the two stored halves: now descending within the bucket.
  const size_t low_base =
      ZephyrusThreatIndex::kHeaderBytes + ZephyrusThreatIndex::kDirectoryBytes;
  std::swap_ranges(blob.begin() + low_base, blob.begin() + low_base + 4,
                   blob.begin() + low_base + 4);
  EXPECT_FALSE(index.Load(blob));
}

// A blob is a file in the profile, so an attacker (or a bad disk) chooses its
// bytes. Whatever they are, loading and asking must not read outside it.
TEST(ZephyrusThreatIndexTest, MutatedBlobsAreRefusedOrHarmless) {
  const std::vector<uint8_t> good = BuildBlob({"a.example/", "b.example/p", "c.example/"});
  uint64_t state = 0x9E3779B97F4A7C15ull;
  const auto next = [&] {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 0x2545F4914F6CDD1Dull;
  };
  int accepted = 0;
  for (int round = 0; round < 3000; ++round) {
    std::vector<uint8_t> copy = good;
    for (int flip = 0; flip < 1 + static_cast<int>(next() % 4); ++flip) {
      switch (next() % 3) {
        case 0:
          copy[next() % copy.size()] ^= static_cast<uint8_t>(1u << (next() % 8));
          break;
        case 1: {
          const size_t at = (next() % (copy.size() - 4)) & ~size_t{3};
          const std::array<uint32_t, 5> values = {0, 1, 0xFFFFFFFF, 0x7FFFFFFF,
                                                  0x80000000};
          base::span(copy).subspan(at, 4u).copy_from(
              base::U32ToLittleEndian(values[next() % 5]));
          break;
        }
        default:
          copy.resize(next() % (copy.size() + 1));
          break;
      }
      if (copy.empty()) {
        break;
      }
    }
    ZephyrusThreatIndex index;
    if (index.Load(copy)) {
      ++accepted;
      index.Contains("a.example/");
      index.IsListed(GURL("https://x.a.example/p?q=1"));
    }
  }
  EXPECT_GT(accepted, 0);
}

// Not a unit test: it measures. The blobs the browser ships are made by a Python
// script (research/make_threat_blob.py) and read by this C++ class, with Chromium's
// own URL canonicalisation in between. If the two disagree about how an address
// is written, a listed address is silently missed. ZEPHYRUS_THREAT_BLOB and
// ZEPHYRUS_THREAT_FEED_SAMPLE (URLs, one per line, taken from the feeds the blob
// was built from) must be set, or the test is skipped.
TEST(ZephyrusThreatIndexTest, TheShippedBlobFindsTheFeedItWasBuiltFrom) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  const std::optional<std::string> blob_path = env->GetVar("ZEPHYRUS_THREAT_BLOB");
  const std::optional<std::string> sample_path =
      env->GetVar("ZEPHYRUS_THREAT_FEED_SAMPLE");
  if (!blob_path || !sample_path) {
    GTEST_SKIP() << "set ZEPHYRUS_THREAT_BLOB and ZEPHYRUS_THREAT_FEED_SAMPLE";
  }
  std::optional<std::vector<uint8_t>> blob =
      base::ReadFileToBytes(base::FilePath::FromUTF8Unsafe(*blob_path));
  ASSERT_TRUE(blob.has_value());
  ZephyrusThreatIndex index;
  ASSERT_TRUE(index.Load(std::move(*blob)));
  std::string sample;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath::FromUTF8Unsafe(*sample_path),
                                     &sample));
  int total = 0;
  int found = 0;
  int unparsable = 0;
  std::vector<std::string> missed;
  for (const std::string_view line : base::SplitStringPiece(
           sample, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const GURL url(line);
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
      ++unparsable;
      continue;
    }
    ++total;
    if (index.IsListed(url)) {
      ++found;
    } else if (missed.size() < 8) {
      missed.emplace_back(line);
    }
  }
  std::cout << "  shipped blob: " << index.size() << " entries; " << found << " of "
            << total << " sampled feed URLs found (" << unparsable
            << " not http/https or not parsable)\n";
  for (const std::string& m : missed) {
    std::cout << "    missed: " << m << "\n";
  }
  ASSERT_GT(total, 100);
  EXPECT_GE(found, total * 98 / 100);
}

TEST(ZephyrusThreatIndexTest, ThePopularListHoldsSitesAndNotUserContentHosts) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  const std::optional<std::string> blob_path = env->GetVar("ZEPHYRUS_POPULAR_BLOB");
  if (!blob_path) {
    GTEST_SKIP() << "set ZEPHYRUS_POPULAR_BLOB";
  }
  std::optional<std::vector<uint8_t>> blob =
      base::ReadFileToBytes(base::FilePath::FromUTF8Unsafe(*blob_path));
  ASSERT_TRUE(blob.has_value());
  ZephyrusThreatIndex popular;
  ASSERT_TRUE(popular.Load(std::move(*blob)));
  for (const char* site : {"https://www.google.com/search?q=x", "https://en.wikipedia.org/wiki/A",
                           "https://mail.yahoo.com/", "https://www.amazon.in/dp/B0"}) {
    EXPECT_TRUE(popular.IsListed(GURL(site))) << site;
  }
  // A page on a shared host belongs to whoever made it: never skipped.
  for (const char* site : {"https://evil-login.blogspot.com/", "https://x.github.io/",
                           "https://phish.vercel.app/", "https://abc.wordpress.com/",
                           "https://never-heard-of-it-9f2a.zzz-unlisted-9f2a.net/"}) {
    EXPECT_FALSE(popular.IsListed(GURL(site))) << site;
  }
}

}  // namespace
}  // namespace zephyrus_threat
