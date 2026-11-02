// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_shared_index.h"

#include <optional>
#include <string_view>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/hash/hash.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"

namespace zephyrus_adblock {

BASE_FEATURE(kZephyrusAdblockCompiledIndex,
             "ZephyrusAdblockCompiledIndex",
             base::FEATURE_ENABLED_BY_DEFAULT);

namespace {

constexpr uint32_t kCacheMagic = 0x31435A58;  // "XZC1"
constexpr uint32_t kCacheVersion = 1;
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
  uint32_t blob_size;
  uint32_t checksum;  // over the other header fields and the whole blob
};
static_assert(sizeof(CacheHeader) == 32);

// Catches a file that is truncated, bit-rotted or half-written by something
// other than us (ours are written atomically). It is an integrity check, not a
// defence against someone with write access to the profile, who could as well
// edit the filter list itself; Load() separately keeps even a hostile blob from
// reading out of bounds.
uint32_t ChecksumOf(const CacheHeader& header,
                    base::span<const uint8_t> blob) {
  CacheHeader copy = header;
  copy.checksum = 0;
  const uint32_t h1 = base::PersistentHash(base::byte_span_from_ref(copy));
  const uint32_t h2 = base::PersistentHash(blob);
  return h1 ^ (h2 * 16777619u);
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
scoped_refptr<const AdblockSharedIndex> AdblockSharedIndex::Build(
    uint64_t list_key,
    const AdblockFilterEngine& engine) {
  AdblockZIndexBuilder builder;
  engine.ForEachRule([&](AdblockFilterEngine::RuleGroup group,
                         const FilterRule& rule, std::string_view) {
    builder.Add(group, rule);
  });
  std::optional<std::vector<uint8_t>> blob = builder.Finish();
  if (!blob) {
    return nullptr;
  }
  CacheHeader header = {};
  header.magic = kCacheMagic;
  header.version = kCacheVersion;
  header.list_key = list_key;
  header.block_rule_count = static_cast<uint32_t>(engine.block_rule_count());
  header.exception_rule_count =
      static_cast<uint32_t>(engine.exception_rule_count());
  header.blob_size = static_cast<uint32_t>(blob->size());
  header.checksum = ChecksumOf(header, *blob);
  std::vector<uint8_t> file(sizeof(CacheHeader) + blob->size());
  base::span(file).first(sizeof(CacheHeader))
      .copy_from(base::byte_span_from_ref(header));
  base::span(file).subspan(sizeof(CacheHeader)).copy_from(*blob);
  return FromFile(list_key, std::move(file));
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
      header.blob_size != file.size() - sizeof(CacheHeader) ||
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
