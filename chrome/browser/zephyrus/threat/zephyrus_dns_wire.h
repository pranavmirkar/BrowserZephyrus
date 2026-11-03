// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_DNS_WIRE_H_
#define CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_DNS_WIRE_H_

#include <stdint.h>

#include <string_view>
#include <vector>

#include "base/containers/span.h"

namespace zephyrus_threat {

// The DNS wire format (RFC 1035 / RFC 8484), only as far as Shield DNS needs it:
// ask a filtering resolver for one host's A record, and read whether it refused.
//
// The answer comes from the network, so ParseDnsAnswer treats it as hostile: every
// read is bounds-checked, nothing is allocated from a length the packet states, and
// anything that does not look exactly like the answer to our question is
// kUnknown. It is fuzzed (zephyrus_dns_wire_fuzzer).

enum class WireAnswer {
  kClean,
  kBlocked,
  // Unreadable, not the answer to our question, or a plain "no such name" (which
  // may be a typo). Callers treat this as clean: a missing answer never blocks.
  kUnknown,
};

// An RFC 8484 query (id 0) for the A record of `host`, with an EDNS0 OPT record so a
// resolver is allowed to say why it refused. Empty when `host` cannot be a DNS name
// (empty label, label over 63 bytes, name over 253, or a byte outside
// letters, digits, '-', '_', '.').
std::vector<uint8_t> BuildDnsQuery(std::string_view host);

// Reads `response` as the answer to BuildDnsQuery(host).
//
// Blocked means the resolver says so in one of the two ways filtering resolvers do:
//  - NOERROR with an A record of 0.0.0.0 (Cloudflare's security resolver), or
//  - an Extended DNS Error (RFC 8914) of Blocked (15), Censored (16) or
//    Filtered (17), usually with NXDOMAIN (Quad9 answers NXDOMAIN + Filtered).
// A bare NXDOMAIN, with no stated reason, is unknown.
WireAnswer ParseDnsAnswer(base::span<const uint8_t> response, std::string_view host);

}  // namespace zephyrus_threat

#endif  // CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_DNS_WIRE_H_
