// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_shared_index.h"

#include <array>
#include <bit>
#include <optional>
#include <string_view>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/hash/hash.h"
#include "base/logging.h"
#include "base/numerics/byte_conversions.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"

namespace zephyrus_adblock {

BASE_FEATURE(kZephyrusAdblockCompiledIndex,
             "ZephyrusAdblockCompiledIndex",
             base::FEATURE_ENABLED_BY_DEFAULT);

namespace {

constexpr uint32_t kCacheMagic = 0x31435A58;  // "XZC1"
constexpr uint32_t kCacheVersion = 2;  // 2: 64-bit word-wise checksum, no blob_size
// An index for any list we would load is a few megabytes; a file past this is
// not one of ours.
constexpr int64_t kMaxCacheFileBytes = 128 * 1024 * 1024;

// The first bytes of a cache file. Eight-byte aligned in size, so the blob that
// follows starts 4-byte aligned in an aligned buffer, which AdblockZIndex needs.
struct CacheHeader {
  uint32_t magic;
  uint32_t version;
  uint64_t list_key;
  uint32_t block_rule_count;
  uint32_t exception_rule_count;
  uint64_t checksum;  // over the other header fields and the whole blob
};
static_assert(sizeof(CacheHeader) == 32);

// A stable 64-bit hash of a buffer, eight bytes at a time over four independent
// lanes (the lanes are what let it run near memory speed). Stable on purpose: the
// value is stored in the cache file and compared by a later process, so it cannot
// be base::FastHash(), which is free to change between builds. Not cryptographic;
// it detects damage, it does not stop an adversary (see ChecksumOf()).
uint64_t Hash64(base::span<const uint8_t> data) {
  constexpr uint64_t kPrimeA = 0x9E3779B185EBCA87ull;
  constexpr uint64_t kPrimeB = 0xC2B2AE3D27D4EB4Full;
  constexpr uint64_t kPrimeC = 0x165667B19E3779F9ull;
  const auto word = [](base::span<const uint8_t> bytes) {
    return base::U64FromLittleEndian(bytes.first<8u>());
  };
  const uint64_t size = data.size();
  std::array<uint64_t, 4> lane = {kPrimeA, kPrimeB, kPrimeA ^ kPrimeB, size};
  base::span<const uint8_t> rest = data;
  while (rest.size() >= 32) {
    for (size_t i = 0; i < 4; ++i) {
      lane[i] = std::rotl(lane[i] + word(rest.subspan(i * 8, 8u)) * kPrimeB, 31) *
                kPrimeA;
    }
    rest = rest.subspan(32u);
  }
  uint64_t h = std::rotl(lane[0], 1) + std::rotl(lane[1], 7) +
               std::rotl(lane[2], 12) + std::rotl(lane[3], 18) + size;
  for (const uint64_t l : lane) {
    h = (h ^ (std::rotl(l * kPrimeB, 31) * kPrimeA)) * kPrimeA + kPrimeC;
  }
  while (rest.size() >= 8) {
    h ^= std::rotl(word(rest) * kPrimeB, 31) * kPrimeA;
    h = std::rotl(h, 27) * kPrimeA + kPrimeC;
    rest = rest.subspan(8u);
  }
  for (const uint8_t byte : rest) {
    h ^= byte * kPrimeC;
    h = std::rotl(h, 11) * kPrimeA;
  }
  h ^= h >> 33;
  h *= kPrimeB;
  h ^= h >> 29;
  h *= kPrimeC;
  h ^= h >> 32;
  return h;
}

// Catches a file that is truncated, bit-rotted or half-written by something
// other than us (ours are written atomically). It is an integrity check, not a
// defence against someone with write access to the profile, who could as well
// edit the filter list itself; Load() separately keeps even a hostile blob from
// reading out of bounds (measured: research/hostile_driver.py).
uint64_t ChecksumOf(const CacheHeader& header,
                    base::span<const uint8_t> blob) {
  CacheHeader copy = header;
  copy.checksum = Hash64(blob);
  return Hash64(base::byte_span_from_ref(copy));
}

// The process's current index, and the lock that serialises building it. The
// lock is held across a build on purpose: a second profile asking for the same
// list should wait half a second for the first one's result, not start a second
// build of its own.
struct Cache {
  base::Lock lock;
  scoped_refptr<const AdblockSharedIndex> current GUARDED_BY(lock);
};

Cache& GetCache() {
  static base::NoDestructor<Cache> cache;
  return *cache;
}

}  // namespace

// One line of every kind of rule the parser and the index builder handle. Any
// change to how they read or store a rule changes the index built from this,
// and with it every key.
constexpr char kCanaryRules[] = R"RULES(
||ads.example^
||tracker.example/pixel^$image,third-party
@@||ads.example/allowed^
||strong.example^$important
/banner/*/img^
|https://start.example/ad
.swf|
||cdn.example^$denyallow=ok.example|fine.example
/ads.js$domain=a.example|~b.a.example
*$script,3p,domain=entity.*
||pop.example^$popup
@@||site.example^$document
@@||nohide.example^$generichide
@@||plain.example^$elemhide,specifichide
||notscript.example^$~script,~xhr
||typed.example^$script,stylesheet,font
||xhr.example^$xhr,subdocument,ping,websocket,media,object,other
-banner-ads-
&adunit=
||long.example/some/quite/long/path/with/many/tokens/in/it^*^utm=
||dest.example^$doc,to=a.example|~b.a.example
)RULES";

// What this build's parser and builder make of the canary rules.
uint32_t SemanticsFingerprint() {
  static const uint32_t fingerprint = [] {
    AdblockFilterEngine engine;
    engine.AddRules(kCanaryRules);
    AdblockZIndexBuilder builder;
    engine.ForEachRule([&](AdblockFilterEngine::RuleGroup group,
                           const FilterRule& rule, std::string_view) {
      builder.Add(group, rule);
    });
    const std::optional<std::vector<uint8_t>> blob = builder.Finish();
    return blob ? base::PersistentHash(*blob) : 0u;
  }();
  return fingerprint;
}

// static
uint64_t AdblockSharedIndex::KeyFor(std::string_view list_text,
                                    std::string_view extra_rules) {
  // The length and 32-bit hashes of the texts. A different list that agreed on
  // all of them would leave one profile on a stale index until the next update,
  // which is a worse list rather than a broken one.
  const uint32_t hash = base::PersistentHash(base::as_byte_span(list_text)) ^
                        (base::PersistentHash(base::as_byte_span(extra_rules)) *
                         16777619u) ^
                        (SemanticsFingerprint() * 2654435761u);
  return (uint64_t{list_text.size() + extra_rules.size()} << 32) | hash;
}

AdblockSharedIndex::AdblockSharedIndex() = default;
AdblockSharedIndex::~AdblockSharedIndex() = default;

// static
scoped_refptr<const AdblockSharedIndex> AdblockSharedIndex::GetCached(
    uint64_t list_key,
    const base::FilePath& cache_file) {
  Cache& cache = GetCache();
  base::AutoLock hold(cache.lock);
  if (cache.current && cache.current->list_key() == list_key) {
    return cache.current;
  }
  if (cache_file.empty()) {
    return nullptr;
  }
  const std::optional<int64_t> size = base::GetFileSize(cache_file);
  if (!size || *size <= static_cast<int64_t>(sizeof(CacheHeader)) ||
      *size > kMaxCacheFileBytes) {
    return nullptr;
  }
  std::optional<std::vector<uint8_t>> bytes = base::ReadFileToBytes(cache_file);
  if (!bytes) {
    return nullptr;
  }
  scoped_refptr<const AdblockSharedIndex> loaded =
      FromFile(list_key, std::move(*bytes));
  if (loaded) {
    cache.current = loaded;
  }
  return loaded;
}

// static
scoped_refptr<const AdblockSharedIndex> AdblockSharedIndex::GetOrBuild(
    uint64_t list_key,
    const AdblockFilterEngine& engine,
    const base::FilePath& cache_file) {
  Cache& cache = GetCache();
  base::AutoLock hold(cache.lock);
  if (cache.current && cache.current->list_key() == list_key) {
    return cache.current;
  }
  scoped_refptr<const AdblockSharedIndex> built = Build(list_key, engine);
  if (!built) {
    return nullptr;
  }
  cache.current = built;
  if (!cache_file.empty() && base::CreateDirectory(cache_file.DirName())) {
    const std::vector<uint8_t>& image = built->file_;
    const std::string_view bytes(reinterpret_cast<const char*>(image.data()),
                                 image.size());
    if (!base::ImportantFileWriter::WriteFileAtomically(cache_file, bytes,
                                                        "ZephyrusAdBlock")) {
      VLOG(1) << "[Zephyrus] adblock: could not write the index cache "
              << cache_file;
    }
  }
  return built;
}

// static
void AdblockSharedIndex::ResetForTesting() {
  Cache& cache = GetCache();
  base::AutoLock hold(cache.lock);
  cache.current.reset();
}

// static
std::vector<uint8_t> AdblockSharedIndex::FileImageForTesting(
    uint64_t list_key,
    const AdblockFilterEngine& engine) {
  return MakeFileImage(list_key, engine);
}

// static
scoped_refptr<const AdblockSharedIndex> AdblockSharedIndex::Build(
    uint64_t list_key,
    const AdblockFilterEngine& engine) {
  std::vector<uint8_t> file = MakeFileImage(list_key, engine);
  if (file.empty()) {
    return nullptr;
  }
  return FromFile(list_key, std::move(file));
}

// static
std::vector<uint8_t> AdblockSharedIndex::MakeFileImage(
    uint64_t list_key,
    const AdblockFilterEngine& engine) {
  AdblockZIndexBuilder builder;
  engine.ForEachRule([&](AdblockFilterEngine::RuleGroup group,
                         const FilterRule& rule, std::string_view) {
    builder.Add(group, rule);
  });
  std::optional<std::vector<uint8_t>> blob = builder.Finish();
  if (!blob) {
    return {};
  }
  CacheHeader header = {};
  header.magic = kCacheMagic;
  header.version = kCacheVersion;
  header.list_key = list_key;
  header.block_rule_count = static_cast<uint32_t>(engine.block_rule_count());
  header.exception_rule_count =
      static_cast<uint32_t>(engine.exception_rule_count());
  header.checksum = ChecksumOf(header, *blob);
  std::vector<uint8_t> file(sizeof(CacheHeader) + blob->size());
  base::span(file).first(sizeof(CacheHeader))
      .copy_from(base::byte_span_from_ref(header));
  base::span(file).subspan(sizeof(CacheHeader)).copy_from(*blob);
  return file;
}

// static
scoped_refptr<const AdblockSharedIndex> AdblockSharedIndex::FromFile(
    uint64_t list_key,
    std::vector<uint8_t> file) {
  if (file.size() <= sizeof(CacheHeader)) {
    return nullptr;
  }
  CacheHeader header = {};
  base::as_writable_bytes(base::span_from_ref(header))
      .copy_from(base::span(file).first(sizeof(CacheHeader)));
  if (header.magic != kCacheMagic || header.version != kCacheVersion ||
      header.list_key != list_key ||
      header.checksum !=
          ChecksumOf(header, base::span(file).subspan(sizeof(CacheHeader)))) {
    return nullptr;
  }
  scoped_refptr<AdblockSharedIndex> shared =
      base::WrapRefCounted(new AdblockSharedIndex());
  shared->list_key_ = list_key;
  shared->block_rule_count_ = header.block_rule_count;
  shared->exception_rule_count_ = header.exception_rule_count;
  shared->file_ = std::move(file);
  // Load() validates every offset, so a damaged or hand-edited file is
  // rejected here rather than trusted.
  if (!shared->index_.Load(
          base::span(shared->file_).subspan(sizeof(CacheHeader)))) {
    return nullptr;
  }
  return shared;
}

}  // namespace zephyrus_adblock
