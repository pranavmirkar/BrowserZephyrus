// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "base/containers/span.h"
#include "base/compiler_specific.h"
#include "chrome/browser/zephyrus/threat/zephyrus_threat_index.h"
#include "url/gurl.h"

// The threat blob is a file in the profile, so its bytes are whoever's. Whatever
// they are, Load() must refuse them or every lookup must stay inside the blob.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const base::span<const uint8_t> input = UNSAFE_BUFFERS(base::span(data, size));
  zephyrus_threat::ZephyrusThreatIndex index;
  if (index.Load(std::vector<uint8_t>(input.begin(), input.end()))) {
    index.Contains("a.example/");
    index.Contains("");
    index.IsListed(GURL("https://x.a.example/p/q.html?k=v"));
    index.IsListed(GURL("http://[::1]:8080/"));
  }
  return 0;
}
