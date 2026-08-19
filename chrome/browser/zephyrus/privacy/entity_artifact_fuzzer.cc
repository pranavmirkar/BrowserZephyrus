// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// libFuzzer target for the entity artifact parser.
//
// §15 requires "fuzzers running without findings". This is the feature's only
// parser of attacker-supplied binary data: the artifact arrives as a file on
// disk, and §4.4.2 anticipates it arriving over the network once an updater
// exists. entity_artifact.h already says "treat a loaded file as UNTRUSTED";
// this is what holds that claim to account.
//
// **Deliberately fuzzing the UNSIGNED path.** CreateFromBytesForTesting skips
// signature verification, which is exactly what we want here: memory safety of
// the parser must not depend on the signature gate. A parser that is only safe
// because something upstream checked an Ed25519 signature is one
// misconfiguration — or one `require_signature=false` — away from being
// exploitable, and the signature check itself has to run on bytes this code
// already survived.
//
// Build and run:
//   gn gen out/FUZZ --args='use_libfuzzer=true is_asan=true is_debug=false
//                           is_component_build=false'
//   autoninja -C out/FUZZ zephyrus_entity_artifact_fuzzer
//   out/FUZZ/zephyrus_entity_artifact_fuzzer <corpus_dir> -max_total_time=300

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/test/allow_check_is_test_for_testing.h"
#include "chrome/browser/zephyrus/privacy/entity_resolver.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"

namespace {

struct Environment {
  Environment() {
    // CreateFromBytesForTesting is CHECK_IS_TEST()-guarded so production can
    // never reach a parser that skips signature verification. A fuzzer is not
    // a test process by default, so opt in explicitly.
    base::test::AllowCheckIsTestForTesting();
  }
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  // SAFETY: libFuzzer's contract is that `data` points to exactly `size`
  // readable bytes. This is the one place the pointer/length pair has to be
  // rebuilt into a span; everything past this line is bounds-checked.
  const auto input = UNSAFE_BUFFERS(base::span(data, size));

  std::unique_ptr<zephyrus_privacy::EntityResolver> resolver =
      zephyrus_privacy::EntityResolver::CreateFromBytesForTesting(input);
  if (!resolver) {
    // Rejected, which is the expected outcome for almost every input.
    return 0;
  }

  // Parsing succeeding is not the end of the test — the interesting reads
  // happen afterwards. A header that passed validation but describes a table
  // slightly larger than the buffer would only be caught here, when the binary
  // search walks it and the string blob is indexed.
  for (const uint32_t hash : {0u, 1u, 0x7fffffffu, 0xffffffffu}) {
    const zephyrus_privacy::EntityInfo info = resolver->Lookup(hash);
    resolver->GetEntityName(info.entity_id);
  }
  // Entity ids straight off the wire, including the reserved sentinel and one
  // past whatever the header claimed.
  for (uint16_t id : {uint16_t{0}, uint16_t{1}, zephyrus_privacy::kNoEntity,
                      static_cast<uint16_t>(resolver->entry_count() + 1)}) {
    resolver->GetEntityName(id);
  }
  // The §4.2 attribution metadata is (offset, length) pairs out of the same
  // untrusted header, resolved against the string blob.
  resolver->dataset_version();
  resolver->dataset_source();
  resolver->dataset_licence();
  resolver->dataset_published_unix_seconds();
  resolver->Freshness(base::Time::Now());

  return 0;
}
