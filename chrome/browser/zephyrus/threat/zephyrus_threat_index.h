// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_THREAT_INDEX_H_
#define CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_THREAT_INDEX_H_

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"

class GURL;

namespace zephyrus_threat {

// A set of known-bad (or, for the popular-sites copy, known-good) web
// addresses, kept as keyed 48-bit hashes of Safe Browsing "expressions"
// (`host/path?query`, `host/path`, `host/`; see SBProtocolManagerUtil) so that a
// lookup never sends anything anywhere.
//
// Why keyed: the hash key is chosen when the blob is built. Nobody can work out
// ahead of time which address collides with a legitimate site's expression,
// so a feed cannot be poisoned to block a particular site by grinding hashes.
// Why 48 bits: with a million entries a chance collision is about 4e-9 per
// expression, and an entry costs 4 bytes.
//
// Blob layout (little-endian):
//   u32 magic "ZTI1", u32 version (1), u8 key[16], u32 count,
//   u32 directory[65537]  (entries whose top 16 bits are < b start here),
//   u32 low[count]        (the low 32 bits of each entry, sorted within a bucket).
//
// The blob comes from a file in the profile, so Load() refuses anything it
// cannot prove well formed (sizes, directory monotonic, every bucket sorted):
// no lookup then reads outside the blob.
class ZephyrusThreatIndex {
 public:
  static constexpr uint32_t kMagic = 0x3149545A;  // the bytes "ZTI1"
  static constexpr uint32_t kVersion = 1;
  static constexpr size_t kBuckets = 65536;
  static constexpr size_t kHeaderBytes = 4 + 4 + 16 + 4;
  static constexpr size_t kDirectoryBytes = (kBuckets + 1) * 4;

  ZephyrusThreatIndex();
  ~ZephyrusThreatIndex();
  ZephyrusThreatIndex(const ZephyrusThreatIndex&) = delete;
  ZephyrusThreatIndex& operator=(const ZephyrusThreatIndex&) = delete;

  // Takes the blob and validates all of it. False leaves the index empty.
  [[nodiscard]] bool Load(std::vector<uint8_t> blob);

  // Whether this exact expression (`host/path`, `host/`) is listed.
  bool Contains(std::string_view expression) const;

  // Whether the URL, or any host-suffix or path-prefix form of it, is listed.
  bool IsListed(const GURL& url) const;

  uint32_t size() const { return count_; }

 private:
  uint32_t DirectoryAt(size_t index) const;
  uint32_t LowAt(size_t index) const;

  std::vector<uint8_t> blob_;
  std::array<uint8_t, 16> key_ = {};
  uint32_t count_ = 0;
};

// Builds a blob. Used by tests and by the tool that makes the shipped data; the
// production data is built by research/make_threat_blob.py, which writes the
// same layout and is checked against this class by a test.
class ZephyrusThreatIndexBuilder {
 public:
  // A random key (production) or a given one (tests).
  ZephyrusThreatIndexBuilder();
  explicit ZephyrusThreatIndexBuilder(std::array<uint8_t, 16> key);
  ~ZephyrusThreatIndexBuilder();

  // `host/` for a whole host (matches every subdomain), or `host/path?query`.
  void AddExpression(std::string_view expression);

  std::vector<uint8_t> Finish() const;

 private:
  std::array<uint8_t, 16> key_;
  std::vector<uint64_t> entries_;
};

// SipHash-2-4 of `data` under `key`; exposed for the cross-language test.
uint64_t SipHash24(base::span<const uint8_t, 16> key,
                   base::span<const uint8_t> data);

}  // namespace zephyrus_threat

#endif  // CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_THREAT_INDEX_H_
