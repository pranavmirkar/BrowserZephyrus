// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/threat/zephyrus_threat_index.h"

#include <algorithm>
#include <bit>
#include <utility>

#include "base/numerics/byte_conversions.h"
#include "base/rand_util.h"
#include "components/safe_browsing/core/browser/db/sb_protocol_manager_util.h"
#include "url/gurl.h"

namespace zephyrus_threat {

namespace {

constexpr uint64_t kKeyMask = 0xFFFFFFFFFFFFull;  // 48 bits

}  // namespace

uint64_t SipHash24(base::span<const uint8_t, 16> key,
                   base::span<const uint8_t> data) {
  const uint64_t k0 = base::U64FromLittleEndian(key.first<8u>());
  const uint64_t k1 = base::U64FromLittleEndian(key.last<8u>());
  uint64_t v0 = 0x736f6d6570736575ull ^ k0;
  uint64_t v1 = 0x646f72616e646f6dull ^ k1;
  uint64_t v2 = 0x6c7967656e657261ull ^ k0;
  uint64_t v3 = 0x7465646279746573ull ^ k1;
  const auto round = [&] {
    v0 += v1;
    v1 = std::rotl(v1, 13);
    v1 ^= v0;
    v0 = std::rotl(v0, 32);
    v2 += v3;
    v3 = std::rotl(v3, 16);
    v3 ^= v2;
    v0 += v3;
    v3 = std::rotl(v3, 21);
    v3 ^= v0;
    v2 += v1;
    v1 = std::rotl(v1, 17);
    v1 ^= v2;
    v2 = std::rotl(v2, 32);
  };
  size_t i = 0;
  for (; i + 8 <= data.size(); i += 8) {
    const uint64_t m = base::U64FromLittleEndian(data.subspan(i).first<8u>());
    v3 ^= m;
    round();
    round();
    v0 ^= m;
  }
  uint64_t last = uint64_t{data.size()} << 56;
  for (size_t j = 0; i + j < data.size(); ++j) {
    last |= uint64_t{data[i + j]} << (8 * j);
  }
  v3 ^= last;
  round();
  round();
  v0 ^= last;
  v2 ^= 0xff;
  round();
  round();
  round();
  round();
  return v0 ^ v1 ^ v2 ^ v3;
}

ZephyrusThreatIndex::ZephyrusThreatIndex() = default;
ZephyrusThreatIndex::~ZephyrusThreatIndex() = default;

uint32_t ZephyrusThreatIndex::DirectoryAt(size_t index) const {
  return base::U32FromLittleEndian(
      base::span(blob_).subspan(kHeaderBytes + index * 4).first<4u>());
}

uint32_t ZephyrusThreatIndex::LowAt(size_t index) const {
  return base::U32FromLittleEndian(
      base::span(blob_).subspan(kHeaderBytes + kDirectoryBytes + index * 4).first<4u>());
}

bool ZephyrusThreatIndex::Load(std::vector<uint8_t> blob) {
  blob_.clear();
  count_ = 0;
  if (blob.size() < kHeaderBytes + kDirectoryBytes) {
    return false;
  }
  const base::span<const uint8_t> bytes(blob);
  if (base::U32FromLittleEndian(bytes.first<4u>()) != kMagic ||
      base::U32FromLittleEndian(bytes.subspan<4u, 4u>()) != kVersion) {
    return false;
  }
  const uint32_t count = base::U32FromLittleEndian(bytes.subspan<24u, 4u>());
  const uint64_t expected =
      uint64_t{kHeaderBytes} + kDirectoryBytes + uint64_t{count} * 4;
  if (uint64_t{blob.size()} != expected) {
    return false;
  }
  base::span(key_).copy_from(bytes.subspan<8u, 16u>());
  blob_ = std::move(blob);

  // The directory starts at 0, never decreases and ends at the count.
  bool ok = DirectoryAt(0) == 0 && DirectoryAt(kBuckets) == count;
  for (size_t b = 0; ok && b < kBuckets; ++b) {
    ok = DirectoryAt(b) <= DirectoryAt(b + 1);
  }
  // Within a bucket the stored halves are strictly increasing, which is what
  // makes the binary search in Contains() correct.
  for (size_t b = 0; ok && b < kBuckets; ++b) {
    const size_t first = DirectoryAt(b);
    const size_t last = DirectoryAt(b + 1);
    for (size_t i = first + 1; ok && i < last; ++i) {
      ok = LowAt(i - 1) < LowAt(i);
    }
  }
  if (!ok) {
    blob_.clear();
    key_ = {};
    return false;
  }
  count_ = count;
  return true;
}

bool ZephyrusThreatIndex::Contains(std::string_view expression) const {
  if (count_ == 0) {
    return false;
  }
  const uint64_t h = SipHash24(key_, base::as_byte_span(expression)) & kKeyMask;
  const size_t bucket = static_cast<size_t>(h >> 32);
  const uint32_t low = static_cast<uint32_t>(h);
  size_t first = DirectoryAt(bucket);
  size_t last = DirectoryAt(bucket + 1);
  while (first < last) {
    const size_t mid = first + (last - first) / 2;
    const uint32_t value = LowAt(mid);
    if (value == low) {
      return true;
    }
    if (value < low) {
      first = mid + 1;
    } else {
      last = mid;
    }
  }
  return false;
}

bool ZephyrusThreatIndex::IsListed(const GURL& url) const {
  if (count_ == 0 || !url.is_valid()) {
    return false;
  }
  std::vector<std::string> patterns;
  safe_browsing::SBProtocolManagerUtil::GeneratePatternsToCheck(url, &patterns);
  return std::ranges::any_of(
      patterns, [this](const std::string& p) { return Contains(p); });
}

ZephyrusThreatIndexBuilder::ZephyrusThreatIndexBuilder() {
  base::RandBytes(key_);
}

ZephyrusThreatIndexBuilder::ZephyrusThreatIndexBuilder(
    std::array<uint8_t, 16> key)
    : key_(key) {}

ZephyrusThreatIndexBuilder::~ZephyrusThreatIndexBuilder() = default;

void ZephyrusThreatIndexBuilder::AddExpression(std::string_view expression) {
  entries_.push_back(SipHash24(key_, base::as_byte_span(expression)) & kKeyMask);
}

std::vector<uint8_t> ZephyrusThreatIndexBuilder::Finish() const {
  std::vector<uint64_t> sorted = entries_;
  std::ranges::sort(sorted);
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

  const size_t count = sorted.size();
  std::vector<uint8_t> out(ZephyrusThreatIndex::kHeaderBytes +
                           ZephyrusThreatIndex::kDirectoryBytes + count * 4);
  const base::span<uint8_t> bytes(out);
  const auto put32 = [&](size_t at, uint32_t value) {
    bytes.subspan(at, 4u).copy_from(base::U32ToLittleEndian(value));
  };
  put32(0, ZephyrusThreatIndex::kMagic);
  put32(4, ZephyrusThreatIndex::kVersion);
  bytes.subspan(8u, 16u).copy_from(key_);
  put32(24, static_cast<uint32_t>(count));
  size_t next = 0;
  for (size_t b = 0; b <= ZephyrusThreatIndex::kBuckets; ++b) {
    while (next < count && (sorted[next] >> 32) < b) {
      ++next;
    }
    put32(ZephyrusThreatIndex::kHeaderBytes + b * 4,
          static_cast<uint32_t>(next));
  }
  const size_t low_base =
      ZephyrusThreatIndex::kHeaderBytes + ZephyrusThreatIndex::kDirectoryBytes;
  for (size_t i = 0; i < count; ++i) {
    put32(low_base + i * 4, static_cast<uint32_t>(sorted[i]));
  }
  return out;
}

}  // namespace zephyrus_threat
