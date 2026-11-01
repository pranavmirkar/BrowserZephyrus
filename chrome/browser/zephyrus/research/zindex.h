// ZIndex prototype: a compact, immutable, memory-mappable index of network filter rules.
//
// Same semantics as AdblockFilterEngine::ShouldBlock; different storage and candidate selection:
//   * rules are 24-byte POD records, patterns live in one string arena (no per-rule heap objects)
//   * tokenised rules are found through an open-addressing table keyed by a 32-bit token hash
//     (a hash collision only adds a candidate; the pattern match stays authoritative)
//   * rules without a bounded keyword are indexed by their rarest literal 3/4/5-gram instead of
//     being scanned linearly on every request
//   * the whole thing is one byte blob: build once when a list updates, mmap everywhere
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <map>
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "base/strings/string_util.h"
#include "url/gurl.h"

namespace zidx {
#ifdef ZIDX_STATS
inline thread_local uint64_t g_stat[8];
#define ZSTAT(i) (++g_stat[i])
#else
#define ZSTAT(i) ((void)0)
#endif
using zephyrus_adblock::FilterRule;
using zephyrus_adblock::ResourceType;

enum Group { kBlock = 0, kException = 1, kImportant = 2, kDocument = 3, kGroups = 4 };
constexpr uint32_t kMagic = 0x58444955;  // "UIDX"
constexpr uint32_t kVersion = 1;


struct Rule {            // 28 bytes, naturally aligned
  uint32_t pat_off;
  uint16_t pat_len;
  uint16_t flags;        // b0-1 anchor, b2 anchor_end, b3 pure_host, b4 is_exception, b5-6 party
  uint16_t type_mask;
  uint16_t type_not;
  uint32_t dom_off;      // 0 = no domain lists; else offset into the domain pool
  uint8_t doc_exc;
  uint8_t pad[3];
  uint32_t sig_lo, sig_hi;   // bit set per whole token the URL must contain; (sig & ~url_sig)==0 is a free reject
};
struct Slot { uint32_t key, start, count; };   // count==0 -> empty
struct GroupHdr {
  uint32_t tok_off, tok_log2;          // Slot[1<<tok_log2]
  uint32_t gram_off[3], gram_log2[3];  // n-gram tables for n=3,4,5
  uint32_t ids_off, ids_n;             // shared u32 rule-id pool
  uint32_t bm_off;                     // 3 bitmaps of 65536 bits, one per n-gram length, in front of the gram tables
  uint32_t dom_tab_off, dom_tab_log2;  // initiator-domain table (rules like `*$script,3p,domain=a.com`)
  uint32_t resid_off, resid_n;         // rules with no usable key: scanned every request
};
struct Header {
  uint32_t magic, version, n_rules;
  uint32_t rules_off, doms_off, doms_n, str_off, str_n;
  GroupHdr g[kGroups];
};

static_assert(sizeof(Rule) == 28 && sizeof(Slot) == 12 && alignof(Rule) == 4);

inline uint32_t Fnv(const char* p, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; ++i) { h ^= (uint8_t)p[i]; h *= 16777619u; } return h ? h : 1; }
inline uint32_t Gram(const char* p, int n) { uint32_t k = n; for (int i = 0; i < n; ++i) k = k * 257u + (uint8_t)p[i]; return k | 1u; }   // never 0
inline uint32_t Probe(uint32_t key, uint32_t log2) { return (key * 2654435761u) >> (32 - log2); }

// ------------------------------------------------------------------------------------------------
// Builder
// ------------------------------------------------------------------------------------------------
class Builder {
 public:
  void Add(Group g, const FilterRule& r, const std::string& keyword /* empty = untokenised */) {
    Entry e{g, r, keyword}; entries_.push_back(std::move(e));
  }
  std::vector<uint8_t> Finish() {
    // 1. pool strings + domains, and assign global rule ids
    std::string arena; arena.reserve(1 << 20);
    std::vector<uint32_t> doms(1, 0);   // offset 0 reserved = none
    std::vector<Rule> rules; rules.reserve(entries_.size());
    std::map<std::string, uint32_t> interned;   // identical patterns share storage
    auto intern = [&](const std::string& s) { auto it = interned.find(s); if (it != interned.end()) return it->second; uint32_t off = (uint32_t)arena.size(); arena += s; interned[s] = off; return off; };
    for (auto& e : entries_) {
      const FilterRule& r = e.rule; Rule o{}; if (r.pattern.size() > 65535) { fprintf(stderr, "pattern too long\n"); abort(); }
      o.pat_off = intern(r.pattern); o.pat_len = (uint16_t)r.pattern.size();
      o.flags = (uint16_t)((r.anchor & 3) | (r.anchor_end << 2) | (r.pure_host << 3) | (r.is_exception << 4) | ((r.party & 3) << 5));
      o.type_mask = (uint16_t)r.type_mask; o.type_not = (uint16_t)r.type_mask_not; o.doc_exc = (uint8_t)r.document_exceptions;
      if (!r.domains_included.empty() || !r.domains_excluded.empty() || !r.denyallow.empty()) {
        o.dom_off = (uint32_t)doms.size();
        doms.push_back((uint32_t)r.domains_included.size()); doms.push_back((uint32_t)r.domains_excluded.size()); doms.push_back((uint32_t)r.denyallow.size());
        for (auto* v : {&r.domains_included, &r.domains_excluded, &r.denyallow}) for (auto& d : *v) { doms.push_back(intern(d)); doms.push_back((uint32_t)d.size()); }
      }
      o.sig_lo = (uint32_t)(Signature(r) & 0xffffffffu); o.sig_hi = (uint32_t)(Signature(r) >> 32);
      rules.push_back(o);
    }
    // 2. n-gram frequency over all literal text (rarest window wins)
    std::unordered_map<uint32_t, uint32_t> freq;
    for (auto& e : entries_) ForEachWindow(e.rule.pattern, [&](int n, const char* p, size_t) { ++freq[Gram(p, n)]; });
    // token frequency across rules (each token counted once per rule), used to prefer the rarest key
    std::unordered_map<std::string, uint32_t> tokfreq;
    for (auto& e : entries_) { std::vector<std::string> t; BoundedTokens(e.rule, &t); std::sort(t.begin(), t.end()); t.erase(std::unique(t.begin(), t.end()), t.end()); for (auto& x : t) ++tokfreq[x]; }
    // 3. place each rule in exactly one index
    struct PerGroup { std::unordered_map<uint32_t, std::vector<uint32_t>> tok, gram[3], dom; std::vector<uint32_t> resid; } pg[kGroups];
    for (uint32_t id = 0; id < entries_.size(); ++id) {
      auto& e = entries_[id]; auto& g = pg[e.group];
      { std::string tok = PickToken(e.rule, tokfreq); if (!tok.empty()) { g.tok[Fnv(tok.data(), tok.size())].push_back(id); ++stats_tok; continue; } }
      int best_n = 0; uint32_t best_key = 0, best_f = ~0u;
      ForEachWindow(e.rule.pattern, [&](int n, const char* p, size_t) {
        uint32_t k = Gram(p, n), f = freq[k];
        if (n > best_n || (n == best_n && f < best_f)) { best_n = n; best_key = k; best_f = f; } });
      if (best_n > 0) { g.gram[best_n - 3][best_key].push_back(id); continue; }
      // No literal to key on. If the rule is scoped to initiator domains, find it through those instead.
      if (!e.rule.domains_included.empty()) {
        for (auto& d : e.rule.domains_included) {
          if (d.size() > 2 && d.compare(d.size() - 2, 2, ".*") == 0) { std::string k = "*" + d.substr(0, d.size() - 2); g.dom[Fnv(k.data(), k.size())].push_back(id); }
          else g.dom[Fnv(d.data(), d.size())].push_back(id); }
        ++stats_dom[e.group]; }
      else g.resid.push_back(id);
    }
    // 4. serialise
    std::vector<uint8_t> out(sizeof(Header)); Header h{}; h.magic = kMagic; h.version = kVersion; h.n_rules = (uint32_t)rules.size();
    auto put = [&](const void* p, size_t n) { size_t off = out.size(); out.resize(off + n); if (n) memcpy(out.data() + off, p, n); while (out.size() % 8) out.push_back(0); return (uint32_t)off; };
    h.rules_off = put(rules.data(), rules.size() * sizeof(Rule));
    h.doms_off = put(doms.data(), doms.size() * 4); h.doms_n = (uint32_t)doms.size();
    h.str_off = put(arena.data(), arena.size()); h.str_n = (uint32_t)arena.size();
    for (int gi = 0; gi < kGroups; ++gi) {
      std::vector<uint32_t> ids; GroupHdr& gh = h.g[gi];
      auto table = [&](std::unordered_map<uint32_t, std::vector<uint32_t>>& m, uint32_t* off, uint32_t* lg) {
        uint32_t cap = 16, l = 4; while (cap < m.size() * 2) { cap <<= 1; ++l; }
        std::vector<Slot> t(cap, Slot{0, 0, 0});
        for (auto& kv : m) { uint32_t i = Probe(kv.first, l); while (t[i].count) i = (i + 1) & (cap - 1); t[i] = Slot{kv.first, (uint32_t)ids.size(), (uint32_t)kv.second.size()}; ids.insert(ids.end(), kv.second.begin(), kv.second.end()); }
        *off = put(t.data(), t.size() * sizeof(Slot)); *lg = l; };
      table(pg[gi].tok, &gh.tok_off, &gh.tok_log2);
      for (int n = 0; n < 3; ++n) table(pg[gi].gram[n], &gh.gram_off[n], &gh.gram_log2[n]);
      table(pg[gi].dom, &gh.dom_tab_off, &gh.dom_tab_log2);
      { std::vector<uint8_t> bm(3 * 8192, 0); for (int n = 0; n < 3; ++n) for (auto& kv : pg[gi].gram[n]) { uint32_t b = (kv.first * 2654435761u) >> 16; bm[n * 8192 + (b >> 3)] |= 1u << (b & 7); } gh.bm_off = put(bm.data(), bm.size()); }
      gh.resid_off = put(pg[gi].resid.data(), pg[gi].resid.size() * 4); gh.resid_n = (uint32_t)pg[gi].resid.size();
      gh.ids_off = put(ids.data(), ids.size() * 4); gh.ids_n = (uint32_t)ids.size();
      stats_resid[gi] = pg[gi].resid.size();
      for (int n = 0; n < 3; ++n) { size_t c = 0; for (auto& kv : pg[gi].gram[n]) c += kv.second.size(); stats_gram[n] += c; }
    }
    memcpy(out.data(), &h, sizeof h);
    return out;
  }
  size_t stats_resid[kGroups] = {}; size_t stats_gram[3] = {}; size_t stats_dom[kGroups] = {}; size_t stats_tok = 0;

 private:
  struct Entry { Group group; FilterRule rule; std::string keyword; };
  // Tokens so common in URLs that keying on them just recreates a linear scan (uBlock calls these "bad tokens").
  static bool Ubiquitous(const std::string& t) {
    static const char* kBad[] = {"com","www","http","https","org","net","html","htm","php","asp","aspx","json","css","img","image","images","static","assets","media","cdn","api","app","apps","page","pages","content","files","file","data","ajax","main","index","lib","libs","min","icon","icons","news","site","web","online","global","public","default","share","cdnjs","edu","gov","info","biz","xyz","top","dev","src","res","resources","common","core","base","system","user","users","home","login","mobile","video","videos","font","fonts","script","scripts","style","styles","text","name","type","version","download","downloads","search","url","link","links","id","get","set","html5","embed","player","track","tracking","event","events","pixel","tag","tags","stats","analytics","log","logs","collect","metrics","beacon"};
    for (const char* b : kBad) if (t == b) return true; return false; }
  static void BoundedTokens(const FilterRule& r, std::vector<std::string>* out) {
    const std::string& p = r.pattern; const bool sa = r.anchor != FilterRule::kAnchorNone; size_t n = p.size(), i = 0;
    while (i < n) { if (!base::IsAsciiAlphaNumeric(p[i])) { ++i; continue; } size_t j = i; while (j < n && base::IsAsciiAlphaNumeric(p[j])) ++j;
      const bool lb = i == 0 ? sa : p[i - 1] != '*'; const bool rb = j == n ? r.anchor_end : p[j] != '*'; if (lb && rb && j - i >= 3) out->push_back(p.substr(i, j - i)); i = j; } }
  static std::string PickToken(const FilterRule& r, std::unordered_map<std::string, uint32_t>& freq) {
    std::vector<std::string> t; BoundedTokens(r, &t); std::string best; uint32_t bf = ~0u;
    for (auto& x : t) { if (Ubiquitous(x)) continue; uint32_t f = freq[x]; if (f < bf || (f == bf && x.size() > best.size())) { bf = f; best = x; } }
    return best; }
  // Every alphanumeric run (>=3) that the pattern pins at both ends must appear in the URL as a whole token.
  static uint64_t Signature(const FilterRule& r) {
    const std::string& p = r.pattern; const bool start_anchored = r.anchor != FilterRule::kAnchorNone; uint64_t sig = 0; size_t n = p.size(), i = 0;
    while (i < n) {
      if (!base::IsAsciiAlphaNumeric(p[i])) { ++i; continue; }
      size_t j = i; while (j < n && base::IsAsciiAlphaNumeric(p[j])) ++j;
      const bool lb = i == 0 ? start_anchored : p[i - 1] != '*'; const bool rb = j == n ? r.anchor_end : p[j] != '*';
      if (lb && rb && j - i >= 3) sig |= 1ull << (Fnv(p.data() + i, j - i) & 63);
      i = j;
    }
    return sig;
  }
  // Windows of 3..5 bytes inside maximal literal runs (no '*' or '^'). Longest windows first.
  template <class F> static void ForEachWindow(const std::string& p, F&& f) {
    size_t i = 0;
    while (i < p.size()) {
      if (p[i] == '*' || p[i] == '^') { ++i; continue; }
      size_t j = i; while (j < p.size() && p[j] != '*' && p[j] != '^') ++j;
      size_t len = j - i;
      if (len >= 3) { int n = (int)std::min<size_t>(5, len); for (size_t k = i; k + n <= j; ++k) f(n, p.data() + k, k); }
      i = j;
    }
  }
  std::vector<Entry> entries_;
};

// ------------------------------------------------------------------------------------------------
// Matcher over an immutable blob
// ------------------------------------------------------------------------------------------------
class Index {
 public:
  // Validates every offset a record or table can reach, so matching never reads out of bounds.
  bool Load(const uint8_t* data, size_t size) {
    if (size < sizeof(Header)) return false;
    base_ = data; size_ = size; memcpy(&h_, data, sizeof h_);
    if (h_.magic != kMagic || h_.version != kVersion) return false;
    auto in = [&](uint64_t off, uint64_t bytes) { return off <= size && bytes <= size - off; };
    if (!in(h_.rules_off, (uint64_t)h_.n_rules * sizeof(Rule)) || !in(h_.doms_off, (uint64_t)h_.doms_n * 4) || !in(h_.str_off, h_.str_n)) return false;
    if ((h_.rules_off | h_.doms_off | h_.str_off) & 3 || ((uintptr_t)data & 3)) return false;
    rules_ = (const Rule*)(base_ + h_.rules_off); doms_ = (const uint32_t*)(base_ + h_.doms_off); str_ = (const char*)(base_ + h_.str_off);
    for (uint32_t i = 0; i < h_.n_rules; ++i) {
      const Rule& r = rules_[i];
      if ((uint64_t)r.pat_off + r.pat_len > h_.str_n) return false;
      if (r.dom_off) {
        if ((uint64_t)r.dom_off + 3 > h_.doms_n) return false;
        uint64_t cnt = (uint64_t)doms_[r.dom_off] + doms_[r.dom_off + 1] + doms_[r.dom_off + 2];
        if ((uint64_t)r.dom_off + 3 + cnt * 2 > h_.doms_n) return false;
        for (uint64_t k = 0; k < cnt; ++k) if ((uint64_t)doms_[r.dom_off + 3 + k * 2] + doms_[r.dom_off + 4 + k * 2] > h_.str_n) return false;
      }
    }
    for (int g = 0; g < kGroups; ++g) {
      const GroupHdr& gh = h_.g[g];
      if (!in(gh.ids_off, (uint64_t)gh.ids_n * 4) || !in(gh.resid_off, (uint64_t)gh.resid_n * 4) || ((gh.ids_off | gh.resid_off | gh.bm_off | gh.dom_tab_off) & 3)) return false;
      const uint32_t* ids = (const uint32_t*)(base_ + gh.ids_off);
      for (uint32_t i = 0; i < gh.ids_n; ++i) if (ids[i] >= h_.n_rules) return false;
      const uint32_t* rs = (const uint32_t*)(base_ + gh.resid_off);
      for (uint32_t i = 0; i < gh.resid_n; ++i) if (rs[i] >= h_.n_rules) return false;
      auto chk = [&](uint32_t off, uint32_t lg) { if (lg > 24 || !in(off, (uint64_t)sizeof(Slot) << lg) || (off & 3)) return false;
        const Slot* t = (const Slot*)(base_ + off); uint32_t used = 0; for (uint32_t i = 0; i < (1u << lg); ++i) if (t[i].count) { ++used; if ((uint64_t)t[i].start + t[i].count > gh.ids_n) return false; }
        return used < (1u << lg); /* probing needs at least one empty slot or a lookup could loop forever */ };
      if (!chk(gh.tok_off, gh.tok_log2)) return false;
      for (int n = 0; n < 3; ++n) { if (!chk(gh.gram_off[n], gh.gram_log2[n])) return false; gram_empty_[g][n] = Empty(gh, n); }
      if (!in(gh.bm_off, 3 * 8192)) return false;
      if (!chk(gh.dom_tab_off, gh.dom_tab_log2)) return false;
      { const Slot* t = (const Slot*)(base_ + gh.dom_tab_off); domtab_empty_[g] = true; for (uint32_t i = 0; i < (1u << gh.dom_tab_log2); ++i) if (t[i].count) { domtab_empty_[g] = false; break; } }
    }
    return true;
  }
  size_t bytes() const { return size_; }

  bool ShouldBlock(const GURL& url, const GURL& initiator, ResourceType type) const {
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) return false;
    thread_local std::string lower;
    Lower(url.spec(), &lower);
    bool third_party = false; std::string_view ini_host;
    if (initiator.is_valid() && !initiator.host().empty()) {
      ini_host = initiator.host();
      third_party = !net::registry_controlled_domains::SameDomainOrHost(url, initiator, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
    }
    Ctx c = MakeCtx(lower, ini_host, third_party, type);
    if (Any(kImportant, c, nullptr)) return true;
    if (!Any(kBlock, c, nullptr)) return false;
    if (Any(kException, c, nullptr)) return false;
    if (initiator.is_valid() && (DocumentExceptions(initiator) & zephyrus_adblock::kExceptDocument)) return false;
    return true;
  }
  uint32_t DocumentExceptions(const GURL& doc) const {
    if (!doc.is_valid() || !doc.SchemeIsHTTPOrHTTPS()) return 0;
    const GroupHdr& gh = h_.g[kDocument];
    if (gh.ids_n == 0 && gh.resid_n == 0) return 0;
    thread_local std::string lower2; Lower(doc.spec(), &lower2);
    Ctx c = MakeCtx(lower2, doc.host(), false, zephyrus_adblock::kTypeDocument);
    uint32_t ex = 0; Any(kDocument, c, &ex); return ex;
  }

 private:
  struct Ctx { std::string_view url, req_host, ini_host; size_t host_start, host_end; bool third_party; ResourceType type; uint64_t sig; uint32_t ntok; uint32_t tok[96]; };
  static void Lower(const std::string& s, std::string* out) { out->resize(s.size()); for (size_t i = 0; i < s.size(); ++i) { char c = s[i]; (*out)[i] = (c >= 'A' && c <= 'Z') ? c + 32 : c; } }
  static Ctx MakeCtx(const std::string& lower, std::string_view ini_host, bool tp, ResourceType type) {
    size_t hs = lower.find("://"); hs = (hs == std::string::npos) ? 0 : hs + 3;
    size_t he = lower.find_first_of("/:?", hs); if (he == std::string::npos) he = lower.size();
    Ctx c{lower, std::string_view(lower).substr(hs, he - hs), ini_host, hs, he, tp, type, 0, 0, {}};
    size_t s = 0; for (size_t i = 0; i <= lower.size(); ++i) { if (i < lower.size() && base::IsAsciiAlphaNumeric(lower[i])) continue;
      if (i - s >= 3) { uint32_t h = Fnv(lower.data() + s, i - s); c.sig |= 1ull << (h & 63); if (c.ntok < 96) c.tok[c.ntok++] = h; } s = i + 1; }
    return c;
  }
  // Verbatim port of the baseline's wildcard matcher, over string_views.
  static bool MatchAt(std::string_view pat, size_t pi, std::string_view text, size_t ti, bool anchor_end, int& budget) {
    ZSTAT(3);
    if (--budget < 0) return false;
    while (pi < pat.size()) {
      char c = pat[pi];
      if (c == '*') {
        while (pi < pat.size() && pat[pi] == '*') ++pi;
        if (pi == pat.size()) return true;
        for (size_t k = ti; k <= text.size(); ++k) { if (MatchAt(pat, pi, text, k, anchor_end, budget)) return true; if (budget < 0) return false; }
        return false;
      }
      if (c == '^') {
        if (ti == text.size()) { ++pi; continue; }
        char t = text[ti]; bool sep = !(base::IsAsciiAlphaNumeric(t) || t == '_' || t == '%' || t == '.' || t == '-');
        if (sep) { ++pi; ++ti; continue; }
        return false;
      }
      if (ti < text.size() && text[ti] == c) { ++pi; ++ti; continue; }
      return false;
    }
    return anchor_end ? (ti == text.size()) : true;
  }
  bool DomainHit(std::string_view host, uint32_t pos) const { return zephyrus_adblock::HostMatchesFilterDomain(host, std::string_view(str_ + doms_[pos], doms_[pos + 1])); }
  bool RuleMatches(const Rule& r, const Ctx& c) const {
    ZSTAT(0);
    if (((((uint64_t)r.sig_hi << 32) | r.sig_lo) & ~c.sig) != 0) return false;
    ZSTAT(1);
    const bool exception = (r.flags >> 4) & 1, pure_host = (r.flags >> 3) & 1; const int anchor = r.flags & 3, party = (r.flags >> 5) & 3; const bool anchor_end = (r.flags >> 2) & 1;
    if (r.type_mask != 0 && !(r.type_mask & c.type)) return false;
    if (r.type_mask == 0 && !exception && (c.type & (zephyrus_adblock::kTypeDocument | zephyrus_adblock::kTypePopup)) && !pure_host) return false;
    if (r.dom_off) {
      const uint32_t ni = doms_[r.dom_off], ne = doms_[r.dom_off + 1], nd = doms_[r.dom_off + 2]; uint32_t p = r.dom_off + 3;
      const uint32_t inc = p, exc = inc + ni * 2, den = exc + ne * 2;
      for (uint32_t k = 0; k < nd; ++k) if (DomainHit(c.req_host, den + k * 2)) return false;
      if (r.type_not & c.type) return false;
      if (party == 1 && c.third_party) return false;
      if (party == 2 && !c.third_party) return false;
      if (ni) { bool ok = false; for (uint32_t k = 0; k < ni && !ok; ++k) ok = DomainHit(c.ini_host, inc + k * 2); if (!ok) return false; }
      for (uint32_t k = 0; k < ne; ++k) if (DomainHit(c.ini_host, exc + k * 2)) return false;
    } else {
      if (r.type_not & c.type) return false;
      if (party == 1 && c.third_party) return false;
      if (party == 2 && !c.third_party) return false;
    }
    ZSTAT(2);
    std::string_view pat(str_ + r.pat_off, r.pat_len); int budget = 100000;
    switch (anchor) {
      case 1: {   // hostname
        for (size_t pos = c.host_start; pos < c.host_end; ++pos) if (pos == c.host_start || c.url[pos - 1] == '.') { if (MatchAt(pat, 0, c.url, pos, anchor_end, budget)) return true; if (budget < 0) return false; }
        return false; }
      case 2: return MatchAt(pat, 0, c.url, 0, anchor_end, budget);
      default: {
        if (pat.empty()) return false;
        char first = pat[0];
        if (first == '*') return MatchAt(pat, 0, c.url, 0, anchor_end, budget);
        const bool literal = first != '^';
        for (size_t pos = literal ? c.url.find(first) : 0; pos != std::string_view::npos && pos <= c.url.size(); pos = literal ? c.url.find(first, pos + 1) : pos + 1) {
          if (MatchAt(pat, 0, c.url, pos, anchor_end, budget)) return true; if (budget < 0) return false; }
        return false; }
    }
  }
  const Slot* Find(uint32_t off, uint32_t lg, uint32_t key) const {
    const Slot* t = (const Slot*)(base_ + off); const uint32_t mask = (1u << lg) - 1; uint32_t i = Probe(key, lg);
    while (t[i].count) { if (t[i].key == key) return &t[i]; i = (i + 1) & mask; }
    return nullptr;
  }
  bool Any(Group g, const Ctx& c, uint32_t* collect) const {
    const GroupHdr& gh = h_.g[g]; const uint32_t* ids = (const uint32_t*)(base_ + gh.ids_off); bool matched = false;
    auto visit = [&](uint32_t id) { const Rule& r = rules_[id]; if (RuleMatches(r, c)) { matched = true; if (collect) *collect |= r.doc_exc; return !collect; } return false; };
    // 1. whole-token keys (hashes were computed once per request, for all four groups)
    for (uint32_t t = 0; t < c.ntok; ++t) if (const Slot* sl = Find(gh.tok_off, gh.tok_log2, c.tok[t])) for (uint32_t k = 0; k < sl->count; ++k) if (visit(ids[sl->start + k])) return true;
    const std::string_view u = c.url;
    // 2. literal n-gram keys for rules that have no bounded keyword
    for (int n = 3; n <= 5; ++n) {
      if (gram_empty_[g][n - 3]) continue;
      const uint8_t* bm = base_ + gh.bm_off + (n - 3) * 8192;
      for (size_t i = 0; i + n <= u.size(); ++i) {
        const uint32_t key = Gram(u.data() + i, n), b = (key * 2654435761u) >> 16;
        if (!(bm[b >> 3] & (1u << (b & 7)))) continue;                       // one L1 load rejects almost every position
        if (const Slot* sl = Find(gh.gram_off[n - 3], gh.gram_log2[n - 3], key)) for (uint32_t k = 0; k < sl->count; ++k) if (visit(ids[sl->start + k])) return true;
      }
    }
    // 2b. rules scoped to initiator domains: look up the initiator host and each parent domain
    if (!c.ini_host.empty() && !domtab_empty_[g]) {
      for (size_t pos = 0; pos != std::string_view::npos; ) {
        std::string_view suf = c.ini_host.substr(pos);
        if (const Slot* sl = Find(gh.dom_tab_off, gh.dom_tab_log2, Fnv(suf.data(), suf.size()))) for (uint32_t k = 0; k < sl->count; ++k) if (visit(ids[sl->start + k])) return true;
        size_t dot = c.ini_host.find('.', pos); pos = dot == std::string_view::npos ? dot : dot + 1;
      }
      // entity rules (`domain=google.*`): the initiator minus its public suffix, and each parent of that
      const size_t reg = net::registry_controlled_domains::GetCanonicalHostRegistryLength(c.ini_host, net::registry_controlled_domains::EXCLUDE_UNKNOWN_REGISTRIES, net::registry_controlled_domains::EXCLUDE_PRIVATE_REGISTRIES);
      if (reg != 0 && reg != std::string::npos && reg + 1 < c.ini_host.size()) {
        std::string_view stem = c.ini_host.substr(0, c.ini_host.size() - reg - 1); char buf[260];
        for (size_t pos = 0; pos != std::string_view::npos && stem.size() - pos < 255; ) {
          std::string_view suf = stem.substr(pos); buf[0] = '*'; memcpy(buf + 1, suf.data(), suf.size());
          if (const Slot* sl = Find(gh.dom_tab_off, gh.dom_tab_log2, Fnv(buf, suf.size() + 1))) for (uint32_t k = 0; k < sl->count; ++k) if (visit(ids[sl->start + k])) return true;
          size_t dot = stem.find('.', pos); pos = dot == std::string_view::npos ? dot : dot + 1;
        }
      }
    }
    // 3. rules that offer nothing to index on
    const uint32_t* rs = (const uint32_t*)(base_ + gh.resid_off);
    for (uint32_t k = 0; k < gh.resid_n; ++k) if (visit(rs[k])) return true;
    return matched;
  }
  bool Empty(const GroupHdr& gh, int t) const { const Slot* sl = (const Slot*)(base_ + gh.gram_off[t]); for (uint32_t i = 0; i < (1u << gh.gram_log2[t]); ++i) if (sl[i].count) return false; return true; }

  bool gram_empty_[kGroups][3] = {}; bool domtab_empty_[kGroups] = {}; const uint8_t* base_ = nullptr; size_t size_ = 0; Header h_{}; const Rule* rules_ = nullptr; const uint32_t* doms_ = nullptr; const char* str_ = nullptr;
};
}  // namespace zidx
