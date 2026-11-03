// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include <stddef.h>
#include <stdint.h>

#include <iterator>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "chrome/browser/zephyrus/threat/zephyrus_dns_wire.h"

// A resolver's answer comes off the network, so its bytes are whoever's. The first
// byte picks which name the answer is read against (so a seed that is a real answer
// for that name gets past the question check); the rest is the packet.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0) {
    return 0;
  }
  static constexpr const char* kHosts[] = {"example.com", "wquie-eiiow201113.pages.dev",
                                           "a.b.c.d.example.co.uk", "x-y_z.test.example"};
  const char* host = kHosts[data[0] % std::size(kHosts)];
  const base::span<const uint8_t> packet =
      UNSAFE_BUFFERS(base::span(data + 1, size - 1));
  zephyrus_threat::ParseDnsAnswer(packet, host);
  return 0;
}
