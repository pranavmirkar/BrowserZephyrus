// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_SHARED_INDEX_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_SHARED_INDEX_H_

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_zindex.h"

namespace zephyrus_adblock {

// Whether the browser matches network requests with the compiled index shared
// by every profile (on), or with one AdblockFilterEngine per profile (off).
// On by default; `--disable-features=ZephyrusAdblockCompiledIndex` is the
// kill switch, and the per-profile engine is the fallback whenever an index
// cannot be built.
BASE_DECLARE_FEATURE(kZephyrusAdblockCompiledIndex);

// A compiled index of the network rules, immutable and shared.
//
// Every profile (the Private Workspace and Guest included) used to parse the
// whole filter list and keep its own copy of the rule engine, about 18 MB
// each and about 0.5 s of CPU each at every start and every list update. Now
// the first profile to need a given list builds the index and the rest take a
// reference to it. A new list (a different `list_key`) builds a new one; the
// old one is freed when the last profile lets go of it.
//
// Thread-safe. The matching functions are const and may be called from any
// thread (their scratch space is thread-local).
class AdblockSharedIndex
    : public base::RefCountedThreadSafe<AdblockSharedIndex> {
 public:
  // Identifies what an index was built from: the list text, the extra rules
  // parsed with it (the built-in starter list), and what this build's parser and
  // index builder do with a fixed canary list. The last part is what keeps a
  // cache written by an older build from being trusted after an upgrade changed
  // how a rule is read: the canary then produces a different index and so a
  // different key, with nobody having to remember to bump a version number.
  static uint64_t KeyFor(std::string_view list_text,
                         std::string_view extra_rules = std::string_view());

  // The index for `list_key` if the process already has it, or the copy cached
  // on disk at `cache_file` (empty = no disk cache). Fast: a file read and a
  // validation, about 10 ms for the shipped list, so it is what lets the
  // protection be in force at startup. Null on a miss.
  static scoped_refptr<const AdblockSharedIndex> GetCached(
      uint64_t list_key,
      const base::FilePath& cache_file);

  // Builds the index for `list_key` from the rules of `engine` (about half a
  // second for the shipped list), unless the process already has it; remembers
  // it and writes it to `cache_file` (empty = don't) so the next start is a
  // GetCached(). Concurrent callers for the same key wait for the first and
  // share its result. Null when an index cannot be built or loaded; the caller
  // then keeps using `engine`.
  //
  // Blocks: call it from a thread pool task that may block, never the UI
  // thread.
  static scoped_refptr<const AdblockSharedIndex> GetOrBuild(
      uint64_t list_key,
      const AdblockFilterEngine& engine,
      const base::FilePath& cache_file);

  // For tests: forget the process's cached index.
  static void ResetForTesting();

  bool ShouldBlock(const GURL& url,
                   const GURL& initiator,
                   ResourceType type) const {
    return index_.ShouldBlock(url, initiator, type);
  }
  uint32_t GetDocumentExceptions(const GURL& document_url) const {
    return index_.GetDocumentExceptions(document_url);
  }

  size_t block_rule_count() const { return block_rule_count_; }
  size_t exception_rule_count() const { return exception_rule_count_; }
  size_t size_bytes() const { return index_.size_bytes(); }
  uint64_t list_key() const { return list_key_; }

 private:
  friend class base::RefCountedThreadSafe<AdblockSharedIndex>;
  AdblockSharedIndex();
  ~AdblockSharedIndex();

  static scoped_refptr<const AdblockSharedIndex> Build(
      uint64_t list_key,
      const AdblockFilterEngine& engine);
  // Takes a whole cache file (header + blob); null if it is not a valid index
  // for `list_key`.
  static scoped_refptr<const AdblockSharedIndex> FromFile(
      uint64_t list_key,
      std::vector<uint8_t> file);

  uint64_t list_key_ = 0;
  size_t block_rule_count_ = 0;
  size_t exception_rule_count_ = 0;
  // The cache-file image: a small header, then the blob. index_ reads it in
  // place, so it is never resized or moved after Load().
  std::vector<uint8_t> file_;
  AdblockZIndex index_;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_SHARED_INDEX_H_
