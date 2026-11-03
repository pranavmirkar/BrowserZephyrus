// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/threat/zephyrus_dns_wire.h"

#include <algorithm>

namespace zephyrus_threat {

namespace {

constexpr size_t kHeaderBytes = 12;
// root name (1) + type (2) + udp size (2) + extended rcode/flags (4) + rdlength (2)
constexpr size_t kOptBytes = 11;
constexpr size_t kMaxLabel = 63;
constexpr size_t kMaxName = 253;
// A real answer to one A question has a handful of records. More is not worth reading.
constexpr size_t kMaxRecords = 32;

constexpr uint16_t kTypeA = 1;
constexpr uint16_t kTypeOpt = 41;
constexpr uint16_t kClassIn = 1;
constexpr uint16_t kOptionEde = 15;
constexpr uint16_t kEdeBlocked = 15;
constexpr uint16_t kEdeCensored = 16;
constexpr uint16_t kEdeFiltered = 17;

void PutU16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v & 0xFF));
}

bool ReadU16(base::span<const uint8_t> d, size_t& pos, uint16_t& v) {
  if (pos > d.size() || d.size() - pos < 2) {
    return false;
  }
  v = static_cast<uint16_t>((d[pos] << 8) | d[pos + 1]);
  pos += 2;
  return true;
}

bool Skip(base::span<const uint8_t> d, size_t& pos, size_t n) {
  if (pos > d.size() || d.size() - pos < n) {
    return false;
  }
  pos += n;
  return true;
}

bool Take(base::span<const uint8_t> d,
          size_t& pos,
          size_t n,
          base::span<const uint8_t>& out) {
  if (pos > d.size() || d.size() - pos < n) {
    return false;
  }
  out = d.subspan(pos, n);
  pos += n;
  return true;
}

// Steps over a (possibly compressed) name. `root` is set when the name is the
// single root label.
bool SkipName(base::span<const uint8_t> d, size_t& pos, bool& root) {
  root = pos < d.size() && d[pos] == 0;
  while (true) {
    if (pos >= d.size()) {
      return false;
    }
    const uint8_t b = d[pos];
    if (b == 0) {
      ++pos;
      return true;
    }
    if ((b & 0xC0) == 0xC0) {  // a pointer ends the name
      return Skip(d, pos, 2);
    }
    if ((b & 0xC0) != 0) {  // the reserved label types
      return false;
    }
    if (!Skip(d, pos, 1u + b)) {
      return false;
    }
  }
}

}  // namespace

std::vector<uint8_t> BuildDnsQuery(std::string_view host) {
  if (host.empty() || host.size() > kMaxName) {
    return {};
  }
  std::vector<uint8_t> q;
  q.reserve(kHeaderBytes + host.size() + 2 + 4 + kOptBytes);
  PutU16(q, 0);       // id (RFC 8484 asks for 0 so equal queries are cacheable)
  PutU16(q, 0x0100);  // recursion desired
  PutU16(q, 1);       // one question
  PutU16(q, 0);
  PutU16(q, 0);
  PutU16(q, 1);  // one additional record: the OPT
  size_t label_start = 0;
  while (label_start <= host.size()) {
    size_t dot = host.find('.', label_start);
    if (dot == std::string_view::npos) {
      dot = host.size();
    }
    const std::string_view label = host.substr(label_start, dot - label_start);
    if (label.empty() || label.size() > kMaxLabel) {
      return {};
    }
    q.push_back(static_cast<uint8_t>(label.size()));
    for (char c : label) {
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
      const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                      c == '_';
      if (!ok) {
        return {};
      }
      q.push_back(static_cast<uint8_t>(c));
    }
    label_start = dot + 1;
  }
  q.push_back(0);
  PutU16(q, kTypeA);
  PutU16(q, kClassIn);
  q.push_back(0);  // OPT: root name
  PutU16(q, kTypeOpt);
  PutU16(q, 1232);  // the UDP size that avoids fragmentation (DNS flag day 2020)
  PutU16(q, 0);     // extended rcode, version
  PutU16(q, 0);     // flags
  PutU16(q, 0);     // no options
  return q;
}

WireAnswer ParseDnsAnswer(base::span<const uint8_t> r, std::string_view host) {
  const std::vector<uint8_t> query = BuildDnsQuery(host);
  if (query.size() < kHeaderBytes + kOptBytes + 5) {
    return WireAnswer::kUnknown;
  }
  const base::span<const uint8_t> question =
      base::span(query).subspan(kHeaderBytes, query.size() - kHeaderBytes - kOptBytes);

  size_t pos = 0;
  uint16_t id = 0, flags = 0, qd = 0, an = 0, ns = 0, ar = 0;
  if (!ReadU16(r, pos, id) || !ReadU16(r, pos, flags) || !ReadU16(r, pos, qd) ||
      !ReadU16(r, pos, an) || !ReadU16(r, pos, ns) || !ReadU16(r, pos, ar)) {
    return WireAnswer::kUnknown;
  }
  // A response (QR), a standard query's (opcode 0), not cut short (TC), for our one question.
  if (id != 0 || (flags & 0x8000) == 0 || (flags & 0x7800) != 0 ||
      (flags & 0x0200) != 0 || qd != 1) {
    return WireAnswer::kUnknown;
  }
  const unsigned rcode = flags & 0xF;
  if (rcode != 0 && rcode != 3) {  // NOERROR or NXDOMAIN; SERVFAIL etc. say nothing
    return WireAnswer::kUnknown;
  }
  base::span<const uint8_t> echoed;
  if (!Take(r, pos, question.size(), echoed) || !std::ranges::equal(echoed, question)) {
    return WireAnswer::kUnknown;
  }
  const size_t records = size_t{an} + ns + ar;
  if (records > kMaxRecords) {
    return WireAnswer::kUnknown;
  }

  bool sinkhole = false;
  bool refused_with_reason = false;
  for (size_t i = 0; i < records; ++i) {
    bool root = false;
    uint16_t type = 0, rclass = 0, rdlength = 0;
    base::span<const uint8_t> rdata;
    if (!SkipName(r, pos, root) || !ReadU16(r, pos, type) || !ReadU16(r, pos, rclass) ||
        !Skip(r, pos, 4) /* ttl */ || !ReadU16(r, pos, rdlength) ||
        !Take(r, pos, rdlength, rdata)) {
      return WireAnswer::kUnknown;
    }
    if (i < an && type == kTypeA && rclass == kClassIn && rdata.size() == 4 &&
        std::ranges::all_of(rdata, [](uint8_t b) { return b == 0; })) {
      sinkhole = true;
    }
    if (i >= size_t{an} + ns && type == kTypeOpt && root) {
      size_t opt_pos = 0;
      while (opt_pos < rdata.size()) {
        uint16_t code = 0, length = 0;
        base::span<const uint8_t> option;
        if (!ReadU16(rdata, opt_pos, code) || !ReadU16(rdata, opt_pos, length) ||
            !Take(rdata, opt_pos, length, option)) {
          return WireAnswer::kUnknown;
        }
        if (code == kOptionEde && option.size() >= 2) {
          const uint16_t info = static_cast<uint16_t>((option[0] << 8) | option[1]);
          if (info == kEdeBlocked || info == kEdeCensored || info == kEdeFiltered) {
            refused_with_reason = true;
          }
        }
      }
    }
  }

  if ((sinkhole && rcode == 0) || refused_with_reason) {
    return WireAnswer::kBlocked;
  }
  return rcode == 0 ? WireAnswer::kClean : WireAnswer::kUnknown;
}

}  // namespace zephyrus_threat
