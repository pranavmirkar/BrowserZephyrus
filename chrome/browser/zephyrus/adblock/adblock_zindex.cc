// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_zindex.h"
#include "chrome/browser/zephyrus/adblock/adblock_redirect_resources.h"

// The index arithmetic is validated once in Load() (every offset, count and
// table size) and every container read here is a base::span, std::vector or
// std::string, which are bounds-checked in this build (hardened libc++ and
// CHECKs in base::span). A corrupt blob is therefore rejected, or a CHECK, and
// never an out-of-bounds read; adblock_zindex_unittest.cc proves it with
// mutated blobs.
#pragma allow_unsafe_buffers

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <map>
#include <unordered_map>
#include <utility>

#include "base/check.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/no_destructor.h"
#include "base/numerics/byte_conversions.h"
#include "base/strings/string_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"

namespace zephyrus_adblock {

namespace {

using zindex_format::GroupHeader;
using zindex_format::Header;
using zindex_format::kBitmapBytes;
using zindex_format::kDomainCounts;
using zindex_format::kGramKinds;
using zindex_format::kGroupCount;
using zindex_format::Rule;
using zindex_format::Slot;
using RuleGroup = AdblockFilterEngine::RuleGroup;

constexpr size_t kMaxPatternLength = 65535;

uint32_t Fnv(std::string_view text) {
  uint32_t h = 2166136261u;
  for (const char c : text) {
    h ^= static_cast<uint8_t>(c);
    h *= 16777619u;
  }
  return h ? h : 1;
}

// An n-gram's key is its n bytes read as one little-endian integer, then mixed.
// The matcher loads 8 bytes at a time from a zero-padded copy of the URL and
// masks; the builder packs exactly n bytes. Both give the same integer.
constexpr std::array<uint64_t, 6> kGramMask = {
    0, 0, 0, 0xffffffull, 0xffffffffull, 0xffffffffffull};

uint32_t GramKey(uint64_t value) {
  // Never 0: an empty table slot has count 0, but keep 0 free as well.
  return static_cast<uint32_t>((value * 0x9E3779B97F4A7C15ull) >> 32) | 1u;
}

uint32_t GramKeyOf(std::string_view gram) {
  uint64_t value = 0;
  for (size_t i = 0; i < gram.size(); ++i) {
    value |= static_cast<uint64_t>(static_cast<uint8_t>(gram[i])) << (8 * i);
  }
  return GramKey(value);
}

uint32_t BitOf(uint32_t key) {
  return (key * 2654435761u) >> 16;
}

uint32_t Probe(uint32_t key, uint32_t log2) {
  return (key * 2654435761u) >> (32 - log2);
}

bool IsAlnum(char c) {
  return base::IsAsciiAlphaNumeric(c);
}

// Tokens so common in URLs that keying on them just recreates a linear scan
// (uBlock calls these "bad tokens").
bool IsUbiquitous(const std::string& token) {
  static constexpr std::string_view kBad[] = {
      "com",       "www",       "http",      "https",     "org",
      "net",       "html",      "htm",       "php",       "asp",
      "aspx",      "json",      "css",       "img",       "image",
      "images",    "static",    "assets",    "media",     "cdn",
      "api",       "app",       "apps",      "page",      "pages",
      "content",   "files",     "file",      "data",      "ajax",
      "main",      "index",     "lib",       "libs",      "min",
      "icon",      "icons",     "news",      "site",      "web",
      "online",    "global",    "public",    "default",   "share",
      "cdnjs",     "edu",       "gov",       "info",      "biz",
      "xyz",       "top",       "dev",       "src",       "res",
      "resources", "common",    "core",      "base",      "system",
      "user",      "users",     "home",      "login",     "mobile",
      "video",     "videos",    "font",      "fonts",     "script",
      "scripts",   "style",     "styles",    "text",      "name",
      "type",      "version",   "download",  "downloads", "search",
      "url",       "link",      "links",     "id",        "get",
      "set",       "html5",     "embed",     "player",    "track",
      "tracking",  "event",     "events",    "pixel",     "tag",
      "tags",      "stats",     "analytics", "log",       "logs",
      "collect",   "metrics",   "beacon"};
  return std::ranges::find(kBad, token) != std::end(kBad);
}

// The alphanumeric runs (>= 3) the pattern pins at both ends, so the URL must
// contain each as a whole token.
std::vector<std::string_view> BoundedTokens(const FilterRule& rule) {
  std::vector<std::string_view> out;
  const std::string& p = rule.pattern;
  const bool start_anchored = rule.anchor != FilterRule::kAnchorNone;
  size_t i = 0;
  while (i < p.size()) {
    if (!IsAlnum(p[i])) {
      ++i;
      continue;
    }
    size_t j = i;
    while (j < p.size() && IsAlnum(p[j])) {
      ++j;
    }
    const bool left_bound = i == 0 ? start_anchored : p[i - 1] != '*';
    const bool right_bound = j == p.size() ? rule.anchor_end : p[j] != '*';
    if (left_bound && right_bound && j - i >= 3) {
      out.emplace_back(p.data() + i, j - i);
    }
    i = j;
  }
  return out;
}

uint64_t Signature(const FilterRule& rule) {
  uint64_t sig = 0;
  for (const std::string_view token : BoundedTokens(rule)) {
    sig |= 1ull << (Fnv(token) & 63);
  }
  return sig;
}

std::string PickToken(
    const FilterRule& rule,
    const std::unordered_map<std::string, uint32_t>& frequency) {
  std::string best;
  uint32_t best_frequency = ~0u;
  for (const std::string_view token : BoundedTokens(rule)) {
    const std::string text(token);
    if (IsUbiquitous(text)) {
      continue;
    }
    const auto it = frequency.find(text);
    const uint32_t f = it == frequency.end() ? 0 : it->second;
    if (f < best_frequency ||
        (f == best_frequency && text.size() > best.size())) {
      best_frequency = f;
      best = text;
    }
  }
  return best;
}

// Windows of 3..5 bytes inside maximal literal runs (no '*' or '^'); each run
// contributes windows of its longest usable length.
template <typename F>
void ForEachWindow(const std::string& p, F&& visit) {
  size_t i = 0;
  while (i < p.size()) {
    if (p[i] == '*' || p[i] == '^') {
      ++i;
      continue;
    }
    size_t j = i;
    while (j < p.size() && p[j] != '*' && p[j] != '^') {
      ++j;
    }
    const size_t length = j - i;
    if (length >= 3) {
      const size_t n = std::min<size_t>(5, length);
      for (size_t k = i; k + n <= j; ++k) {
        visit(static_cast<int>(n), std::string_view(p).substr(k, n));
      }
    }
    i = j;
  }
}

// Appends `bytes`, pads to 8, returns where they start.
uint32_t Put(std::vector<uint8_t>& out, base::span<const uint8_t> bytes) {
  const size_t offset = out.size();
  out.insert(out.end(), bytes.begin(), bytes.end());
  while (out.size() % 8) {
    out.push_back(0);
  }
  return static_cast<uint32_t>(offset);
}

template <typename T>
uint32_t PutVector(std::vector<uint8_t>& out, const std::vector<T>& values) {
  static_assert(std::is_trivially_copyable_v<T>);
  return Put(out, base::as_byte_span(values));
}

// A view of `count` T's at `offset` in `blob`. The range is checked; the cast
// is sound because Load() has already required 4-byte alignment.
template <typename T>
base::span<const T> SpanAt(base::span<const uint8_t> blob,
                           uint32_t offset,
                           size_t count) {
  const base::span<const uint8_t> bytes =
      blob.subspan(offset, count * sizeof(T));
  // SAFETY: `bytes` is exactly count * sizeof(T) bytes of the blob, aligned to
  // alignof(T) (checked in Load()).
  return UNSAFE_BUFFERS(
      base::span<const T>(reinterpret_cast<const T*>(bytes.data()), count));
}

using KeyedIds = std::map<uint32_t, std::vector<uint32_t>>;

}  // namespace

// --- Builder ------------------------------------------------------------

AdblockZIndexBuilder::AdblockZIndexBuilder() = default;
AdblockZIndexBuilder::~AdblockZIndexBuilder() = default;

void AdblockZIndexBuilder::Add(RuleGroup group, const FilterRule& rule) {
  entries_.push_back(Entry{group, rule});
}

std::optional<std::vector<uint8_t>> AdblockZIndexBuilder::Finish() const {
  // 1. Pool strings and domains, and assign global rule ids.
  std::string arena;
  arena.reserve(1 << 20);
  std::vector<uint32_t> domains(1, 0);  // offset 0 reserved = none
  std::vector<Rule> rules;
  rules.reserve(entries_.size());
  std::map<std::string, uint32_t> interned;  // identical text shares storage
  auto intern = [&](const std::string& text) {
    const auto it = interned.find(text);
    if (it != interned.end()) {
      return it->second;
    }
    const uint32_t offset = static_cast<uint32_t>(arena.size());
    arena += text;
    interned[text] = offset;
    return offset;
  };
  for (const Entry& entry : entries_) {
    const FilterRule& r = entry.rule;
    if (r.pattern.size() > kMaxPatternLength) {
      return std::nullopt;
    }
    Rule out = {};
    out.pat_off = intern(r.pattern);
    out.pat_len = static_cast<uint16_t>(r.pattern.size());
    out.flags = static_cast<uint16_t>(
        (r.anchor & 3) | (r.anchor_end << 2) | (r.pure_host << 3) |
        (r.is_exception << 4) | ((r.party & 3) << 5));
    out.type_mask = static_cast<uint16_t>(r.type_mask);
    out.type_not = static_cast<uint16_t>(r.type_mask_not);
    out.doc_exc = static_cast<uint8_t>(r.document_exceptions);
    out.redirect_priority = r.redirect_priority;
    out.redirect_resource = r.redirect_any ? uint16_t{0} : r.redirect_resource;
    if (!r.domains_included.empty() || !r.domains_excluded.empty() ||
        !r.denyallow.empty() || !r.to_included.empty() ||
        !r.to_excluded.empty()) {
      out.dom_off = static_cast<uint32_t>(domains.size());
      domains.push_back(static_cast<uint32_t>(r.domains_included.size()));
      domains.push_back(static_cast<uint32_t>(r.domains_excluded.size()));
      domains.push_back(static_cast<uint32_t>(r.denyallow.size()));
      domains.push_back(static_cast<uint32_t>(r.to_included.size()));
      domains.push_back(static_cast<uint32_t>(r.to_excluded.size()));
      for (const auto* list : {&r.domains_included, &r.domains_excluded,
                               &r.denyallow, &r.to_included, &r.to_excluded}) {
        for (const std::string& domain : *list) {
          domains.push_back(intern(domain));
          domains.push_back(static_cast<uint32_t>(domain.size()));
        }
      }
    }
    const uint64_t sig = Signature(r);
    out.sig_lo = static_cast<uint32_t>(sig & 0xffffffffu);
    out.sig_hi = static_cast<uint32_t>(sig >> 32);
    rules.push_back(out);
  }

  // 2. How often each n-gram and token occurs across rules: the rarest key
  // wins, so a rule is filed where the fewest others are.
  std::unordered_map<uint32_t, uint32_t> gram_frequency;
  for (const Entry& entry : entries_) {
    ForEachWindow(entry.rule.pattern, [&](int, std::string_view gram) {
      ++gram_frequency[GramKeyOf(gram)];
    });
  }
  std::unordered_map<std::string, uint32_t> token_frequency;
  for (const Entry& entry : entries_) {
    std::vector<std::string> tokens;
    for (const std::string_view token : BoundedTokens(entry.rule)) {
      tokens.emplace_back(token);
    }
    std::ranges::sort(tokens);
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    for (const std::string& token : tokens) {
      ++token_frequency[token];
    }
  }

  // 3. Place each rule in exactly one index. std::map keeps the output
  // byte-for-byte reproducible, which a signed bundle will need.
  struct PerGroup {
    KeyedIds tokens;
    KeyedIds grams[kGramKinds];
    KeyedIds domains;
    std::vector<uint32_t> residual;
  };
  PerGroup per_group[kGroupCount];
  for (uint32_t id = 0; id < entries_.size(); ++id) {
    const Entry& entry = entries_[id];
    PerGroup& g = per_group[static_cast<size_t>(entry.group)];
    const std::string token = PickToken(entry.rule, token_frequency);
    if (!token.empty()) {
      g.tokens[Fnv(token)].push_back(id);
      continue;
    }
    int best_n = 0;
    uint32_t best_key = 0;
    uint32_t best_frequency = ~0u;
    ForEachWindow(entry.rule.pattern, [&](int n, std::string_view gram) {
      const uint32_t key = GramKeyOf(gram);
      const uint32_t f = gram_frequency[key];
      if (n > best_n || (n == best_n && f < best_frequency)) {
        best_n = n;
        best_key = key;
        best_frequency = f;
      }
    });
    if (best_n > 0) {
      g.grams[best_n - 3][best_key].push_back(id);
      continue;
    }
    // No literal to key on. A rule scoped to initiator domains is found
    // through those instead.
    if (!entry.rule.domains_included.empty()) {
      for (const std::string& domain : entry.rule.domains_included) {
        if (domain.size() > 2 &&
            domain.compare(domain.size() - 2, 2, ".*") == 0) {
          const std::string key = "*" + domain.substr(0, domain.size() - 2);
          g.domains[Fnv(key)].push_back(id);
        } else {
          g.domains[Fnv(domain)].push_back(id);
        }
      }
    } else {
      g.residual.push_back(id);
    }
  }

  // 4. Serialise.
  std::vector<uint8_t> out(sizeof(Header));
  Header header = {};
  header.magic = zindex_format::kMagic;
  header.version = zindex_format::kVersion;
  header.n_rules = static_cast<uint32_t>(rules.size());
  header.rules_off = PutVector(out, rules);
  header.doms_off = PutVector(out, domains);
  header.doms_n = static_cast<uint32_t>(domains.size());
  header.str_off = Put(out, base::as_byte_span(arena));
  header.str_n = static_cast<uint32_t>(arena.size());

  std::vector<uint8_t> union_grams(kGramKinds * kBitmapBytes, 0);
  std::vector<uint8_t> union_tokens(kBitmapBytes, 0);
  for (size_t gi = 0; gi < kGroupCount; ++gi) {
    std::vector<uint32_t> ids;
    GroupHeader& gh = header.groups[gi];
    auto write_table = [&](const KeyedIds& keyed, uint32_t* offset,
                           uint32_t* log2) {
      uint32_t capacity = 16;
      uint32_t l = 4;
      while (capacity < keyed.size() * 2) {
        capacity <<= 1;
        ++l;
      }
      std::vector<Slot> table(capacity, Slot{0, 0, 0});
      for (const auto& [key, rule_ids] : keyed) {
        uint32_t i = Probe(key, l);
        while (table[i].count) {
          i = (i + 1) & (capacity - 1);
        }
        table[i] = Slot{key, static_cast<uint32_t>(ids.size()),
                        static_cast<uint32_t>(rule_ids.size())};
        ids.insert(ids.end(), rule_ids.begin(), rule_ids.end());
      }
      *offset = PutVector(out, table);
      *log2 = l;
    };
    write_table(per_group[gi].tokens, &gh.tok_off, &gh.tok_log2);
    for (size_t n = 0; n < kGramKinds; ++n) {
      write_table(per_group[gi].grams[n], &gh.gram_off[n], &gh.gram_log2[n]);
    }
    write_table(per_group[gi].domains, &gh.dom_tab_off, &gh.dom_tab_log2);
    for (size_t n = 0; n < kGramKinds; ++n) {
      for (const auto& entry : per_group[gi].grams[n]) {
        const uint32_t bit = BitOf(entry.first);
        union_grams[n * kBitmapBytes + (bit >> 3)] |= 1u << (bit & 7);
      }
    }
    for (const auto& entry : per_group[gi].tokens) {
      const uint32_t bit = BitOf(entry.first);
      union_tokens[bit >> 3] |= 1u << (bit & 7);
    }
    gh.resid_off = PutVector(out, per_group[gi].residual);
    gh.resid_n = static_cast<uint32_t>(per_group[gi].residual.size());
    gh.ids_off = PutVector(out, ids);
    gh.ids_n = static_cast<uint32_t>(ids.size());
  }
  header.ugram_off = PutVector(out, union_grams);
  header.utok_off = PutVector(out, union_tokens);
  base::span(out).first(sizeof(Header))
      .copy_from(base::byte_span_from_ref(header));
  return out;
}

// --- Index --------------------------------------------------------------

namespace {

std::atomic<uint64_t>& NextGeneration() {
  static std::atomic<uint64_t> generation{0};
  return generation;
}

// Whether the budget of recursive wildcard steps is spent. Verbatim from the
// engine: a pattern full of `*` against a long URL cannot run away.
bool MatchAt(std::string_view pat,
             size_t pi,
             std::string_view text,
             size_t ti,
             bool anchor_end,
             int& budget) {
  if (--budget < 0) {
    return false;
  }
  while (pi < pat.size()) {
    const char c = pat[pi];
    if (c == '*') {
      while (pi < pat.size() && pat[pi] == '*') {
        ++pi;
      }
      if (pi == pat.size()) {
        return true;
      }
      for (size_t k = ti; k <= text.size(); ++k) {
        if (MatchAt(pat, pi, text, k, anchor_end, budget)) {
          return true;
        }
        if (budget < 0) {
          return false;
        }
      }
      return false;
    }
    if (c == '^') {
      if (ti == text.size()) {
        ++pi;
        continue;
      }
      const char t = text[ti];
      const bool separator = !(IsAlnum(t) || t == '_' || t == '%' ||
                               t == '.' || t == '-');
      if (separator) {
        ++pi;
        ++ti;
        continue;
      }
      return false;
    }
    if (ti < text.size() && text[ti] == c) {
      ++pi;
      ++ti;
      continue;
    }
    return false;
  }
  return anchor_end ? (ti == text.size()) : true;
}

const Slot* FindSlot(base::span<const Slot> table, uint32_t key) {
  const uint32_t log2 = std::countr_zero(table.size());
  const size_t mask = table.size() - 1;
  size_t i = Probe(key, log2);
  while (table[i].count) {
    if (table[i].key == key) {
      return &table[i];
    }
    i = (i + 1) & mask;
  }
  return nullptr;
}

}  // namespace

struct AdblockZIndex::Context {
  std::string_view url;
  std::string_view request_host;
  std::string_view initiator_host;
  size_t host_start = 0;
  size_t host_end = 0;
  bool third_party = false;
  ResourceType type = kTypeOther;
  uint64_t signature = 0;
  // Point into the calling thread's scratch for the length of one lookup; the
  // Context is a stack local, so a protected pointer would only add cost.
  RAW_PTR_EXCLUSION const std::vector<uint32_t>* tokens = nullptr;
  RAW_PTR_EXCLUSION const std::vector<uint32_t>* grams = nullptr;  // kGramKinds
};

// Per-thread scratch, reused for every request: no allocation once warm, and
// no cap on how many tokens a URL may have.
struct AdblockZIndex::Scratch {
  std::string buffer;
  std::vector<uint32_t> tokens;
  std::vector<uint32_t> grams[kGramKinds];
  std::string entity_key;
};

namespace {
AdblockZIndex::Scratch& ThreadScratch(int which);
}  // namespace

AdblockZIndex::AdblockZIndex() = default;
AdblockZIndex::~AdblockZIndex() = default;

namespace {
AdblockZIndex::Scratch& ThreadScratch(int which) {
  // Never destroyed: a thread that exits leaves its few KB behind, which is
  // cheaper than running a destructor on every thread of the network stack.
  thread_local base::NoDestructor<std::array<AdblockZIndex::Scratch, 2>>
      scratch;
  return (*scratch)[which];
}
}  // namespace

bool AdblockZIndex::Load(base::span<const uint8_t> blob) {
  blob_ = base::span<const uint8_t>();
  if (blob.size() < sizeof(Header)) {
    return false;
  }
  // The tables are read in place as arrays of 4-byte-aligned records.
  if (reinterpret_cast<uintptr_t>(blob.data()) & 3) {
    return false;
  }
  Header header = {};
  base::as_writable_bytes(base::span_from_ref(header))
      .copy_from(blob.first(sizeof(Header)));
  if (header.magic != zindex_format::kMagic ||
      header.version != zindex_format::kVersion) {
    return false;
  }
  const uint64_t size = blob.size();
  auto in_range = [&](uint64_t offset, uint64_t bytes) {
    return offset <= size && bytes <= size - offset;
  };
  if (!in_range(header.rules_off, uint64_t{header.n_rules} * sizeof(Rule)) ||
      !in_range(header.doms_off, uint64_t{header.doms_n} * 4) ||
      !in_range(header.str_off, header.str_n) ||
      ((header.rules_off | header.doms_off | header.str_off) & 3) ||
      !in_range(header.ugram_off, kGramKinds * kBitmapBytes) ||
      !in_range(header.utok_off, kBitmapBytes)) {
    return false;
  }
  const base::span<const Rule> rules =
      SpanAt<Rule>(blob, header.rules_off, header.n_rules);
  const base::span<const uint32_t> domain_pool =
      SpanAt<uint32_t>(blob, header.doms_off, header.doms_n);
  // Every record's string and domain references must stay inside their pools.
  for (const Rule& rule : rules) {
    if (uint64_t{rule.pat_off} + rule.pat_len > header.str_n) {
      return false;
    }
    if (rule.dom_off) {
      if (uint64_t{rule.dom_off} + kDomainCounts > header.doms_n) {
        return false;
      }
      const uint64_t count = uint64_t{domain_pool[rule.dom_off]} +
                             domain_pool[rule.dom_off + 1] +
                             domain_pool[rule.dom_off + 2] +
                             domain_pool[rule.dom_off + 3] +
                             domain_pool[rule.dom_off + 4];
      if (uint64_t{rule.dom_off} + kDomainCounts + count * 2 > header.doms_n) {
        return false;
      }
      for (uint64_t k = 0; k < count; ++k) {
        if (uint64_t{domain_pool[rule.dom_off + kDomainCounts + k * 2]} +
                domain_pool[rule.dom_off + kDomainCounts + 1 + k * 2] >
            header.str_n) {
          return false;
        }
      }
    }
  }

  GroupView views[kGroupCount];
  for (size_t g = 0; g < kGroupCount; ++g) {
    const GroupHeader& gh = header.groups[g];
    if (!in_range(gh.ids_off, uint64_t{gh.ids_n} * 4) ||
        !in_range(gh.resid_off, uint64_t{gh.resid_n} * 4) ||
        ((gh.ids_off | gh.resid_off | gh.dom_tab_off) & 3)) {
      return false;
    }
    GroupView& view = views[g];
    view.ids = SpanAt<uint32_t>(blob, gh.ids_off, gh.ids_n);
    view.residual = SpanAt<uint32_t>(blob, gh.resid_off, gh.resid_n);
    for (const uint32_t id : view.ids) {
      if (id >= header.n_rules) {
        return false;
      }
    }
    for (const uint32_t id : view.residual) {
      if (id >= header.n_rules) {
        return false;
      }
    }
    // A table needs at least one empty slot, or probing a missing key would
    // never end. And at least the 16 the writer always makes: Probe() shifts the
    // hash right by (32 - log2), which for log2 == 0 is a shift by the word size,
    // an arbitrary slot (found by fuzzing an all-zero table directory).
    auto table_at = [&](uint32_t offset, uint32_t log2,
                        base::span<const Slot>* out) {
      if (log2 < 4 || log2 > 24 || (offset & 3) ||
          !in_range(offset, uint64_t{sizeof(Slot)} << log2)) {
        return false;
      }
      *out = SpanAt<Slot>(blob, offset, size_t{1} << log2);
      size_t used = 0;
      for (const Slot& slot : *out) {
        if (slot.count) {
          ++used;
          if (uint64_t{slot.start} + slot.count > gh.ids_n) {
            return false;
          }
        }
      }
      return used < out->size();
    };
    if (!table_at(gh.tok_off, gh.tok_log2, &view.tokens)) {
      return false;
    }
    for (size_t n = 0; n < kGramKinds; ++n) {
      if (!table_at(gh.gram_off[n], gh.gram_log2[n], &view.grams[n])) {
        return false;
      }
      view.grams_empty[n] = std::ranges::none_of(
          view.grams[n], [](const Slot& slot) { return slot.count != 0; });
    }
    if (!table_at(gh.dom_tab_off, gh.dom_tab_log2, &view.domains)) {
      return false;
    }
    view.domains_empty = std::ranges::none_of(
        view.domains, [](const Slot& slot) { return slot.count != 0; });
  }

  blob_ = blob;
  header_ = header;
  rules_ = rules;
  domain_pool_ = domain_pool;
  strings_ = std::string_view(
      reinterpret_cast<const char*>(blob.subspan(header.str_off, header.str_n)
                                        .data()),
      header.str_n);
  for (size_t n = 0; n < kGramKinds; ++n) {
    union_grams_[n] =
        blob.subspan(header.ugram_off + n * kBitmapBytes, kBitmapBytes);
    union_grams_used_[n] = std::ranges::any_of(
        union_grams_[n], [](uint8_t byte) { return byte != 0; });
  }
  union_tokens_ = blob.subspan(header.utok_off, kBitmapBytes);
  for (size_t g = 0; g < kGroupCount; ++g) {
    groups_[g] = views[g];
  }
  generation_ = ++NextGeneration();
  return true;
}

bool AdblockZIndex::ShouldBlock(const GURL& url,
                                const GURL& initiator,
                                ResourceType type,
                                const RegexHits* regex) const {
  if (blob_.empty() || !url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  bool third_party = false;
  std::string_view initiator_host;
  if (initiator.is_valid() && !initiator.host().empty()) {
    initiator_host = initiator.host();
    third_party = !net::registry_controlled_domains::SameDomainOrHost(
        url, initiator,
        net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  }

  Scratch& scratch = ThreadScratch(0);
  Context c;
  Prepare(scratch, url.spec(), initiator_host, third_party, type, &c);

  if ((regex && regex->important) || Any(RuleGroup::kImportant, c, nullptr)) {
    return true;
  }
  if (!(regex && regex->block) && !Any(RuleGroup::kBlock, c, nullptr)) {
    return false;
  }
  if ((regex && regex->exception) || Any(RuleGroup::kException, c, nullptr)) {
    return false;
  }
  // `c` is finished with: the document lookup reuses a different scratch.
  if (initiator.is_valid() &&
      (GetDocumentExceptions(initiator) & kExceptDocument)) {
    return false;
  }
  return true;
}

uint32_t AdblockZIndex::GetDocumentExceptions(const GURL& document_url) const {
  if (blob_.empty() || !document_url.is_valid() ||
      !document_url.SchemeIsHTTPOrHTTPS()) {
    return 0;
  }
  const GroupView& view = groups_[static_cast<size_t>(RuleGroup::kDocument)];
  if (view.ids.empty() && view.residual.empty()) {
    return 0;
  }
  // The same page asks about itself for every blocked request it makes, so the
  // answer for the last page is kept.
  struct DocumentCache {
    uint64_t generation = 0;
    std::string url;
    uint32_t value = 0;
  };
  thread_local base::NoDestructor<DocumentCache> cache_holder;
  DocumentCache& cache = *cache_holder;
  if (cache.generation == generation_ && cache.url == document_url.spec()) {
    return cache.value;
  }

  Scratch& scratch = ThreadScratch(1);
  Context c;
  Prepare(scratch, document_url.spec(), document_url.host(),
          /*third_party=*/false, kTypeDocument, &c);
  uint32_t exceptions = 0;
  Any(RuleGroup::kDocument, c, &exceptions);
  cache.generation = generation_;
  cache.url = document_url.spec();
  cache.value = exceptions;
  return exceptions;
}

std::optional<uint16_t> AdblockZIndex::ResolveRedirect(
    const GURL& url,
    const GURL& initiator,
    ResourceType type) const {
  if (blob_.empty() || !url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  const GroupView& directives =
      groups_[static_cast<size_t>(RuleGroup::kRedirect)];
  if (directives.ids.empty() && directives.residual.empty()) {
    return std::nullopt;  // the lists have no stand-ins at all
  }
  bool third_party = false;
  std::string_view initiator_host;
  if (initiator.is_valid() && !initiator.host().empty()) {
    initiator_host = initiator.host();
    third_party = !net::registry_controlled_domains::SameDomainOrHost(
        url, initiator,
        net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  }
  Scratch& scratch = ThreadScratch(0);
  Context c;
  Prepare(scratch, url.spec(), initiator_host, third_party, type, &c);

  std::vector<RedirectCandidate> candidates;
  const auto add_candidate = [&](const Rule& r) {
    candidates.push_back({r.redirect_resource, r.redirect_priority});
  };
  const base::FunctionRef<void(const Rule&)> add_candidate_ref(add_candidate);
  Any(RuleGroup::kRedirect, c, nullptr, &add_candidate_ref);
  if (candidates.empty()) {
    return std::nullopt;
  }
  std::vector<uint16_t> exceptions;
  const auto add_exception = [&](const Rule& r) {
    exceptions.push_back(r.redirect_resource);
  };
  const base::FunctionRef<void(const Rule&)> add_exception_ref(add_exception);
  Any(RuleGroup::kRedirectException, c, nullptr, &add_exception_ref);
  return ChooseRedirect(candidates, exceptions, type);
}

void AdblockZIndex::Prepare(Scratch& scratch,
                            const std::string& spec,
                            std::string_view initiator_host,
                            bool third_party,
                            ResourceType type,
                            Context* c) const {
  const size_t n = spec.size();
  // Lowercased, with 8 zero bytes of padding so an n-gram can be read as one
  // 64-bit load.
  scratch.buffer.assign(n + 8, '\0');
  for (size_t i = 0; i < n; ++i) {
    const char ch = spec[i];
    scratch.buffer[i] =
        (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch + 32) : ch;
  }
  c->url = std::string_view(scratch.buffer).substr(0, n);
  size_t host_start = c->url.find("://");
  host_start = host_start == std::string_view::npos ? 0 : host_start + 3;
  size_t host_end = c->url.find_first_of("/:?", host_start);
  if (host_end == std::string_view::npos) {
    host_end = n;
  }
  c->request_host = c->url.substr(host_start, host_end - host_start);
  c->initiator_host = initiator_host;
  c->host_start = host_start;
  c->host_end = host_end;
  c->third_party = third_party;
  c->type = type;
  c->signature = 0;
  c->tokens = &scratch.tokens;
  c->grams = scratch.grams;

  scratch.tokens.clear();
  size_t token_start = 0;
  for (size_t i = 0; i <= n; ++i) {
    if (i < n && IsAlnum(c->url[i])) {
      continue;
    }
    if (i - token_start >= 3) {
      const uint32_t h = Fnv(c->url.substr(token_start, i - token_start));
      c->signature |= 1ull << (h & 63);
      const uint32_t bit = BitOf(h);
      if (union_tokens_[bit >> 3] & (1u << (bit & 7))) {
        scratch.tokens.push_back(h);
      }
    }
    token_start = i + 1;
  }
  const base::span<const uint8_t> padded = base::as_byte_span(scratch.buffer);
  for (size_t g = 0; g < kGramKinds; ++g) {
    scratch.grams[g].clear();
    if (!union_grams_used_[g]) {
      continue;
    }
    const size_t length = g + 3;
    for (size_t i = 0; i + length <= n; ++i) {
      const uint64_t v =
          base::U64FromLittleEndian(padded.subspan(i).first<8u>());
      const uint32_t key = GramKey(v & kGramMask[length]);
      const uint32_t bit = BitOf(key);
      // Almost every position stops here, after one load.
      if (union_grams_[g][bit >> 3] & (1u << (bit & 7))) {
        scratch.grams[g].push_back(key);
      }
    }
  }
}

bool AdblockZIndex::DomainHit(std::string_view host, uint32_t pos) const {
  return HostMatchesFilterDomain(
      host, strings_.substr(domain_pool_[pos], domain_pool_[pos + 1]));
}

bool AdblockZIndex::RuleMatches(const Rule& r, const Context& c) const {
  const uint64_t rule_sig = (uint64_t{r.sig_hi} << 32) | r.sig_lo;
  if ((rule_sig & ~c.signature) != 0) {
    return false;
  }
  const bool exception = (r.flags >> 4) & 1;
  const bool pure_host = (r.flags >> 3) & 1;
  const int anchor = r.flags & 3;
  const int party = (r.flags >> 5) & 3;
  const bool anchor_end = (r.flags >> 2) & 1;
  if (r.type_mask != 0 && !(r.type_mask & c.type)) {
    return false;
  }
  if (r.type_mask == 0 && !exception &&
      (c.type & (kTypeDocument | kTypePopup)) && !pure_host) {
    return false;
  }
  if (r.dom_off) {
    const uint32_t included = domain_pool_[r.dom_off];
    const uint32_t excluded = domain_pool_[r.dom_off + 1];
    const uint32_t denied = domain_pool_[r.dom_off + 2];
    const uint32_t to_included = domain_pool_[r.dom_off + 3];
    const uint32_t to_excluded = domain_pool_[r.dom_off + 4];
    const uint32_t include_at = r.dom_off + kDomainCounts;
    const uint32_t exclude_at = include_at + included * 2;
    const uint32_t deny_at = exclude_at + excluded * 2;
    const uint32_t to_include_at = deny_at + denied * 2;
    const uint32_t to_exclude_at = to_include_at + to_included * 2;
    for (uint32_t k = 0; k < denied; ++k) {
      if (DomainHit(c.request_host, deny_at + k * 2)) {
        return false;
      }
    }
    if (to_included) {
      bool ok = false;
      for (uint32_t k = 0; k < to_included && !ok; ++k) {
        ok = DomainHit(c.request_host, to_include_at + k * 2);
      }
      if (!ok) {
        return false;
      }
    }
    for (uint32_t k = 0; k < to_excluded; ++k) {
      if (DomainHit(c.request_host, to_exclude_at + k * 2)) {
        return false;
      }
    }
    if (r.type_not & c.type) {
      return false;
    }
    if (party == 1 && c.third_party) {
      return false;
    }
    if (party == 2 && !c.third_party) {
      return false;
    }
    if (included) {
      bool ok = false;
      for (uint32_t k = 0; k < included && !ok; ++k) {
        ok = DomainHit(c.initiator_host, include_at + k * 2);
      }
      if (!ok) {
        return false;
      }
    }
    for (uint32_t k = 0; k < excluded; ++k) {
      if (DomainHit(c.initiator_host, exclude_at + k * 2)) {
        return false;
      }
    }
  } else {
    if (r.type_not & c.type) {
      return false;
    }
    if (party == 1 && c.third_party) {
      return false;
    }
    if (party == 2 && !c.third_party) {
      return false;
    }
  }

  const std::string_view pat = strings_.substr(r.pat_off, r.pat_len);
  int budget = 100000;
  switch (anchor) {
    case FilterRule::kAnchorHostname: {
      for (size_t pos = c.host_start; pos < c.host_end; ++pos) {
        if (pos == c.host_start || c.url[pos - 1] == '.') {
          if (MatchAt(pat, 0, c.url, pos, anchor_end, budget)) {
            return true;
          }
          if (budget < 0) {
            return false;
          }
        }
      }
      return false;
    }
    case FilterRule::kAnchorStart:
      return MatchAt(pat, 0, c.url, 0, anchor_end, budget);
    default: {
      if (pat.empty()) {
        return false;
      }
      const char first = pat[0];
      if (first == '*') {
        return MatchAt(pat, 0, c.url, 0, anchor_end, budget);
      }
      const bool literal = first != '^';
      for (size_t pos = literal ? c.url.find(first) : 0;
           pos != std::string_view::npos && pos <= c.url.size();
           pos = literal ? c.url.find(first, pos + 1) : pos + 1) {
        if (MatchAt(pat, 0, c.url, pos, anchor_end, budget)) {
          return true;
        }
        if (budget < 0) {
          return false;
        }
      }
      return false;
    }
  }
}

bool AdblockZIndex::Any(
    RuleGroup group,
    const Context& c,
    uint32_t* collect,
    const base::FunctionRef<void(const Rule&)>* each) const {
  const GroupView& view = groups_[static_cast<size_t>(group)];
  bool matched = false;
  // With `collect` or `each`, every match is visited (its document exceptions
  // ORed in, or handed over); otherwise the first match ends the search.
  auto visit = [&](uint32_t id) {
    const Rule& r = rules_[id];
    if (!RuleMatches(r, c)) {
      return false;
    }
    matched = true;
    if (collect) {
      *collect |= r.doc_exc;
    }
    if (each) {
      (*each)(r);
    }
    return !(collect || each);
  };
  auto visit_slot = [&](const Slot* slot) {
    if (!slot) {
      return false;
    }
    for (uint32_t k = 0; k < slot->count; ++k) {
      if (visit(view.ids[slot->start + k])) {
        return true;
      }
    }
    return false;
  };

  // 1. Whole-token keys that survived the union bitmap.
  for (const uint32_t token : *c.tokens) {
    if (visit_slot(FindSlot(view.tokens, token))) {
      return true;
    }
  }
  // 2. Literal n-gram keys (rules with no bounded keyword), likewise
  // pre-filtered once per request.
  for (size_t g = 0; g < kGramKinds; ++g) {
    if (view.grams_empty[g]) {
      continue;
    }
    for (const uint32_t key : c.grams[g]) {
      if (visit_slot(FindSlot(view.grams[g], key))) {
        return true;
      }
    }
  }
  // 2b. Rules scoped to initiator domains: look up the initiator host and each
  // parent domain.
  if (!c.initiator_host.empty() && !view.domains_empty) {
    for (size_t pos = 0; pos != std::string_view::npos;) {
      const std::string_view suffix = c.initiator_host.substr(pos);
      if (visit_slot(FindSlot(view.domains, Fnv(suffix)))) {
        return true;
      }
      const size_t dot = c.initiator_host.find('.', pos);
      pos = dot == std::string_view::npos ? dot : dot + 1;
    }
    // Entity rules (`domain=google.*`): the initiator minus its public suffix,
    // and each parent of that.
    const size_t registry_length =
        net::registry_controlled_domains::GetCanonicalHostRegistryLength(
            c.initiator_host,
            net::registry_controlled_domains::EXCLUDE_UNKNOWN_REGISTRIES,
            net::registry_controlled_domains::EXCLUDE_PRIVATE_REGISTRIES);
    if (registry_length != 0 && registry_length != std::string::npos &&
        registry_length + 1 < c.initiator_host.size()) {
      const std::string_view stem = c.initiator_host.substr(
          0, c.initiator_host.size() - registry_length - 1);
      std::string& key = ThreadScratch(0).entity_key;
      for (size_t pos = 0;
           pos != std::string_view::npos && stem.size() - pos < 255;) {
        key.assign(1, '*');
        key.append(stem.substr(pos));
        if (visit_slot(FindSlot(view.domains, Fnv(key)))) {
          return true;
        }
        const size_t dot = stem.find('.', pos);
        pos = dot == std::string_view::npos ? dot : dot + 1;
      }
    }
  }
  // 3. Rules that offer nothing to index on.
  for (const uint32_t id : view.residual) {
    if (visit(id)) {
      return true;
    }
  }
  return matched;
}

}  // namespace zephyrus_adblock
