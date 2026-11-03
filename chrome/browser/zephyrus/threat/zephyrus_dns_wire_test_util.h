// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_DNS_WIRE_TEST_UTIL_H_
#define CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_DNS_WIRE_TEST_UTIL_H_

#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "chrome/browser/zephyrus/threat/zephyrus_dns_wire.h"

namespace zephyrus_threat::testing_util {

// Builds answers to BuildDnsQuery(host) the way resolvers send them.

// The bytes of an A record named by pointer to the question, for `a.b.c.d`.
inline std::vector<uint8_t> ARecord(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return {0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x04, a, b, c, d};
}

// An OPT record carrying one Extended DNS Error with `info_code`.
inline std::vector<uint8_t> OptWithEde(uint16_t info_code) {
  return {0x00, 0x00, 0x29, 0x04, 0xD0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00,
          0x0F, 0x00, 0x02, static_cast<uint8_t>(info_code >> 8),
          static_cast<uint8_t>(info_code & 0xFF)};
}

inline std::vector<uint8_t> Reply(std::string_view host,
                                  uint16_t rcode,
                                  uint16_t an,
                                  uint16_t ns,
                                  uint16_t ar,
                                  const std::vector<uint8_t>& records) {
  const std::vector<uint8_t> query = BuildDnsQuery(host);
  const size_t question_end = query.size() - 11;  // the OPT is the last 11 bytes
  std::vector<uint8_t> out = {0x00, 0x00,
                              static_cast<uint8_t>(0x81),
                              static_cast<uint8_t>(0x80 | (rcode & 0xF)),
                              0x00, 0x01,
                              static_cast<uint8_t>(an >> 8), static_cast<uint8_t>(an & 0xFF),
                              static_cast<uint8_t>(ns >> 8), static_cast<uint8_t>(ns & 0xFF),
                              static_cast<uint8_t>(ar >> 8), static_cast<uint8_t>(ar & 0xFF)};
  out.insert(out.end(), query.begin() + 12, query.begin() + question_end);
  out.insert(out.end(), records.begin(), records.end());
  return out;
}

// A normal answer: one real-looking address.
inline std::vector<uint8_t> CleanReply(std::string_view host) {
  return Reply(host, 0, 1, 0, 0, ARecord(93, 184, 216, 34));
}

// What Quad9 sends for a name it filters: NXDOMAIN and EDE 17 (Filtered).
inline std::vector<uint8_t> FilteredReply(std::string_view host) {
  std::vector<uint8_t> out = Reply(host, 3, 0, 0, 1, OptWithEde(17));
  out[3] = 0x03;  // Quad9 does not set "recursion available" on a refusal
  return out;
}

// What Cloudflare's security resolver sends for a name it blocks: 0.0.0.0.
inline std::vector<uint8_t> SinkholeReply(std::string_view host) {
  return Reply(host, 0, 1, 0, 0, ARecord(0, 0, 0, 0));
}

inline std::string AsString(const std::vector<uint8_t>& bytes) {
  return std::string(bytes.begin(), bytes.end());
}

}  // namespace zephyrus_threat::testing_util

#endif  // CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_DNS_WIRE_TEST_UTIL_H_
