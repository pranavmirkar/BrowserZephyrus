// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_ZINDEX_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_ZINDEX_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/function_ref.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "url/gurl.h"

// AdblockZIndex: a compact, immutable, memory-mappable index of network filter
// rules, with the same matching semantics as AdblockFilterEngine::ShouldBlock.
//
// WHY. AdblockFilterEngine keeps every rule as a heap object in a hash of
// vectors: ~33 MB of heap per profile for the shipped lists, a full copy built
// by every profile (Private Workspace and Guest included), and ~17 us per
// request. This keeps each rule as a 28-byte record in one byte blob that can
// be written once when a list updates and mapped read-only by every profile.
//
// HOW.
//  * Patterns live in one string arena; rules are plain records.
//  * Each rule is filed once: under its rarest whole token (never a ubiquitous
//    one such as "com"), else its rarest literal 3/4/5-gram, else its initiator
//    domain (`domain=` rules), else a residual list that is scanned every time.
//  * Tables are open addressing over 32-bit keys. A collision only adds a
//    candidate; the pattern match stays authoritative.
//  * Union bitmaps over every token and n-gram reject most positions of a URL
//    before any table is read.
//  * Load() checks every offset, alignment and that each table has an empty
//    slot, so a corrupt blob cannot crash or hang a lookup.
//
// The index is built FROM the engine's parsed rules (AdblockFilterEngine::
// ForEachRule), never from list text, so the two cannot disagree about what a
// line means. adblock_zindex_unittest.cc proves they agree on what a request
// does. Little-endian only.
namespace zephyrus_adblock {

namespace zindex_format {

inline constexpr uint32_t kMagic = 0x58444955;  // "UIDX"
inline constexpr uint32_t kVersion = 4;  // 4: redirect groups, stand-in id per rule
                                          // 3: $to= lists in the domain pool
// A rule's entry in the domain pool starts with this many counts (included,
// excluded, denyallow, to-included, to-excluded); the (offset, length) pairs of
// the strings follow in the same order.
inline constexpr uint32_t kDomainCounts = 5;
inline constexpr size_t kGroupCount = 6;  // RuleGroup, in its order
inline constexpr size_t kGramKinds = 3;  // n-grams of length 3, 4 and 5
inline constexpr size_t kBitmapBytes = 8192;

// 28 bytes, naturally aligned.
struct Rule {
  uint32_t pat_off;
  uint16_t pat_len;
  // b0-1 anchor, b2 anchor_end, b3 pure_host, b4 is_exception, b5-6 party.
  uint16_t flags;
  uint16_t type_mask;
  uint16_t type_not;
  uint32_t dom_off;  // 0 = no domain lists; else offset into the domain pool.
  uint8_t doc_exc;
  // A redirect directive: the stand-in's id and priority. In the exception
  // group, id 0 cancels every directive. Not read for any other group.
  int8_t redirect_priority;
  uint16_t redirect_resource;
  // One bit per whole token the URL must contain; (sig & ~url_sig) == 0 is a
  // free reject.
  uint32_t sig_lo;
  uint32_t sig_hi;
};

// count == 0 means empty.
struct Slot {
  uint32_t key;
  uint32_t start;
  uint32_t count;
};

struct GroupHeader {
  uint32_t tok_off;
  uint32_t tok_log2;
  uint32_t gram_off[kGramKinds];
  uint32_t gram_log2[kGramKinds];
  uint32_t ids_off;
  uint32_t ids_n;
  uint32_t dom_tab_off;
  uint32_t dom_tab_log2;
  uint32_t resid_off;  // rules with no usable key: scanned every request
  uint32_t resid_n;
};

struct Header {
  uint32_t magic;
  uint32_t version;
  uint32_t n_rules;
  uint32_t rules_off;
  uint32_t doms_off;
  uint32_t doms_n;
  uint32_t str_off;
  uint32_t str_n;
  // One 65536-bit bitmap per n-gram length and one for whole tokens, each the
  // UNION over all four groups. A request position whose key misses the bitmap
  // cannot be looked up anywhere, so it is rejected once, not once per group.
  uint32_t ugram_off;  // kGramKinds x kBitmapBytes
  uint32_t utok_off;   // kBitmapBytes
  GroupHeader groups[kGroupCount];
};

static_assert(sizeof(Rule) == 28);
static_assert(sizeof(Slot) == 12);
static_assert(alignof(Rule) == 4);

}  // namespace zindex_format

// Collects rules and serialises them to a blob. Not thread-safe; meant to run
// on a worker thread when a list updates.
class AdblockZIndexBuilder {
 public:
  AdblockZIndexBuilder();
  AdblockZIndexBuilder(const AdblockZIndexBuilder&) = delete;
  AdblockZIndexBuilder& operator=(const AdblockZIndexBuilder&) = delete;
  ~AdblockZIndexBuilder();

  void Add(AdblockFilterEngine::RuleGroup group, const FilterRule& rule);

  // The serialised index, or nullopt when a rule cannot be represented (a
  // pattern over 65,535 bytes). The caller then keeps using the engine.
  std::optional<std::vector<uint8_t>> Finish() const;

 private:
  struct Entry {
    AdblockFilterEngine::RuleGroup group;
    FilterRule rule;
  };
  std::vector<Entry> entries_;
};

// Matches requests against a blob. The blob must outlive the index and be
// 4-byte aligned (a mapped file and a std::vector<uint8_t> both are).
// ShouldBlock() is const and safe from any thread: its scratch space is
// thread-local.
class AdblockZIndex {
 public:
  AdblockZIndex();
  AdblockZIndex(const AdblockZIndex&) = delete;
  AdblockZIndex& operator=(const AdblockZIndex&) = delete;
  ~AdblockZIndex();

  // Validates every offset a record or table can reach, so matching never reads
  // out of bounds. False leaves the index unusable.
  bool Load(base::span<const uint8_t> blob);

  // `regex` is what the regex rules say about this request (see RegexHits); the
  // index holds no regex rules itself.
  bool ShouldBlock(const GURL& url,
                   const GURL& initiator,
                   ResourceType type,
                   const RegexHits* regex = nullptr) const;

  // Same contract as AdblockFilterEngine::GetDocumentExceptions.
  uint32_t GetDocumentExceptions(const GURL& document_url) const;

  // Same contract as AdblockFilterEngine::ResolveRedirect.
  std::optional<uint16_t> ResolveRedirect(const GURL& url,
                                          const GURL& initiator,
                                          ResourceType type) const;

  size_t size_bytes() const { return blob_.size(); }

 public:
  // Per-thread working memory; defined in the .cc.
  struct Scratch;

 private:
  struct Context;
  struct GroupView {
    // Views into the index's own blob, which outlives them (they are rebuilt
    // with it and never handed out). RAW_PTR_EXCLUSION, not raw_span: every
    // lookup reads these, and a protected pointer would add a reference count to
    // each copy on a path measured in nanoseconds.
    RAW_PTR_EXCLUSION base::span<const zindex_format::Slot> tokens;
    base::span<const zindex_format::Slot> grams[zindex_format::kGramKinds];
    RAW_PTR_EXCLUSION base::span<const zindex_format::Slot> domains;
    RAW_PTR_EXCLUSION base::span<const uint32_t> ids;
    RAW_PTR_EXCLUSION base::span<const uint32_t> residual;
    bool grams_empty[zindex_format::kGramKinds] = {};
    bool domains_empty = true;
  };

  void Prepare(Scratch& scratch,
               const std::string& spec,
               std::string_view initiator_host,
               bool third_party,
               ResourceType type,
               Context* c) const;
  bool RuleMatches(const zindex_format::Rule& rule, const Context& c) const;
  // With `collect`, every match is visited and its document exceptions ORed in;
  // with `each`, every match is handed to it. Otherwise the first match ends
  // the search.
  bool Any(AdblockFilterEngine::RuleGroup group,
           const Context& c,
           uint32_t* collect,
           const base::FunctionRef<void(const zindex_format::Rule&)>* each =
               nullptr) const;
  bool DomainHit(std::string_view host, uint32_t pos) const;

  // See GroupView: views into storage the caller keeps alive for the index's life.
  RAW_PTR_EXCLUSION base::span<const uint8_t> blob_;
  zindex_format::Header header_ = {};
  RAW_PTR_EXCLUSION base::span<const zindex_format::Rule> rules_;
  RAW_PTR_EXCLUSION base::span<const uint32_t> domain_pool_;
  std::string_view strings_;
  base::span<const uint8_t> union_grams_[zindex_format::kGramKinds];
  RAW_PTR_EXCLUSION base::span<const uint8_t> union_tokens_;
  bool union_grams_used_[zindex_format::kGramKinds] = {};
  GroupView groups_[zindex_format::kGroupCount];
  // Keyed by this index's generation, so a new index (a list update) can never
  // be answered from an old one's cached document exceptions.
  uint64_t generation_ = 0;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_ZINDEX_H_
