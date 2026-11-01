// IdLeakScanner prototype: finds the user's own identifiers (or digests of them) inside an outgoing request.
// Setup is done once per identifier (hashing happens here, never per request); scanning is allocation-free.
#pragma once
#include <openssl/evp.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
namespace idleak {
enum Kind : uint8_t { kPlain = 1, kMd5, kSha1, kSha256 };
struct Hit { Kind kind; const char* encoding; uint32_t offset; uint32_t ident; };
inline std::vector<uint8_t> Digest(const char* alg, std::string_view s) { unsigned char o[64]; unsigned n = 0; EVP_MD_CTX* c = EVP_MD_CTX_new(); EVP_DigestInit_ex(c, EVP_get_digestbyname(alg), nullptr); EVP_DigestUpdate(c, s.data(), s.size()); EVP_DigestFinal_ex(c, o, &n); EVP_MD_CTX_free(c); return {o, o + n}; }
inline std::string NormalizeEmail(std::string e, bool gmail_rules) { for (char& c : e) if (c >= 'A' && c <= 'Z') c += 32; while (!e.empty() && e.front() == ' ') e.erase(0, 1); while (!e.empty() && e.back() == ' ') e.pop_back();
  if (!gmail_rules) return e; size_t at = e.find('@'); if (at == std::string::npos) return e; std::string l = e.substr(0, at), d = e.substr(at + 1); if (d != "gmail.com" && d != "googlemail.com") return e;
  size_t plus = l.find('+'); if (plus != std::string::npos) l.erase(plus); std::string o; for (char c : l) if (c != '.') o += c; return o + "@gmail.com"; }
inline int HexVal(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
inline int B64Val(char c) { if (c >= 'A' && c <= 'Z') return c - 'A'; if (c >= 'a' && c <= 'z') return c - 'a' + 26; if (c >= '0' && c <= '9') return c - '0' + 52; if (c == '+' || c == '-') return 62; if (c == '/' || c == '_') return 63; return -1; }
inline uint64_t Key8(const uint8_t* p) { uint64_t k; memcpy(&k, p, 8); return k; }

class Scanner { public:
  // `ident` is whatever the caller wants echoed back (an index into its own table); nothing here keeps the plain value after hashing
  void AddEmail(std::string_view email, uint32_t ident) {
    for (bool g : {false, true}) { std::string n = NormalizeEmail(std::string(email), g);
      for (auto [alg, kind] : {std::pair{"MD5", kMd5}, {"SHA1", kSha1}, {"SHA256", kSha256}}) { auto d = Digest(alg, n); digests_[Key8(d.data())] = {kind, ident}; }
      plain_[Fold(n)] = ident; } }
  // Scans `data` (a URL, a form body, JSON...). Calls out(hit) per finding; stops after `max_hits`.
  template <class F> void Scan(std::string_view data, F&& out, size_t max_bytes = 16384) const {
    if (data.size() > max_bytes) data = data.substr(0, max_bytes); const size_t n = data.size();
    for (size_t i = 0; i < n;) {
      // hex run
      if (HexVal(data[i]) >= 0) { size_t j = i; while (j < n && HexVal(data[j]) >= 0) ++j; size_t len = j - i;
        for (size_t want : {64, 40, 32}) if (len >= want) { TryHex(data.substr(i, want), i, out); if (len > want) TryHex(data.substr(j - want, want), j - want, out); }
        // fall through: the same characters may also be a base64 candidate
      }
      if (B64Val(data[i]) >= 0 || data[i] == '=') { size_t j = i; while (j < n && (B64Val(data[j]) >= 0)) ++j; size_t len = j - i;
        if (len >= 22) for (size_t want : {43, 27, 22}) if (len >= want) { TryB64(data.substr(i, want), i, out); if (len > want) TryB64(data.substr(j - want, want), j - want, out); }
        if (j > i) { i = j; continue; } }
      ++i; }
    // plain or percent-encoded address: anchor on '@' / "%40"
    for (size_t at = data.find('@'); at != std::string_view::npos; at = data.find('@', at + 1)) TryPlain(data, at, 1, out);
    for (size_t at = data.find("%40"); at != std::string_view::npos; at = data.find("%40", at + 3)) TryPlain(data, at, 3, out); }
 private:
  static uint64_t Fold(std::string_view s) { uint64_t h = 1469598103934665603ull; for (char c : s) { h ^= (uint8_t)(c >= 'A' && c <= 'Z' ? c + 32 : c); h *= 1099511628211ull; } return h; }
  template <class F> void TryHex(std::string_view s, size_t off, F& out) const { uint8_t b[32]; size_t nb = s.size() / 2; for (size_t k = 0; k < nb; ++k) b[k] = (uint8_t)(HexVal(s[2 * k]) << 4 | HexVal(s[2 * k + 1])); Check(b, nb, "hex", off, out); }
  template <class F> void TryB64(std::string_view s, size_t off, F& out) const { uint8_t b[40]; size_t nb = 0; uint32_t acc = 0; int bits = 0; for (char c : s) { acc = acc << 6 | (uint32_t)B64Val(c); bits += 6; if (bits >= 8) { bits -= 8; if (nb < sizeof b) b[nb++] = (uint8_t)(acc >> bits); } } Check(b, nb, "base64", off, out); }
  template <class F> void Check(const uint8_t* b, size_t nb, const char* enc, size_t off, F& out) const { if (nb != 16 && nb != 20 && nb != 32) return; auto it = digests_.find(Key8(b)); if (it == digests_.end()) return;
    Kind want = nb == 16 ? kMd5 : nb == 20 ? kSha1 : kSha256; if (it->second.first == want) out(Hit{want, enc, (uint32_t)off, it->second.second}); }
  template <class F> void TryPlain(std::string_view d, size_t at, size_t sep, F& out) const { size_t b = at; auto okc = [](char c) { return HexVal(c) >= 0 || (c >= 'g' && c <= 'z') || (c >= 'G' && c <= 'Z') || c == '.' || c == '_' || c == '-' || c == '+'; };
    while (b > 0 && okc(d[b - 1])) --b; size_t e = at + sep; while (e < d.size() && (okc(d[e]) || d[e] == '@')) ++e; if (b == at || e == at + sep) return;
    std::string cand(d.substr(b, at - b)); cand += '@'; cand.append(d.substr(at + sep, e - at - sep)); auto it = plain_.find(Fold(cand)); if (it != plain_.end()) out(Hit{kPlain, sep == 3 ? "percent-encoded" : "plain", (uint32_t)b, it->second}); }
  std::unordered_map<uint64_t, std::pair<Kind, uint32_t>> digests_; std::unordered_map<uint64_t, uint32_t> plain_;
};
}
