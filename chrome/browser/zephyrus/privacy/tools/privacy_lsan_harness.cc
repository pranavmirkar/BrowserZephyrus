// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Standalone LeakSanitizer harness for the Privacy Intelligence allocating
// paths.
//
// §15 requires "ASAN/LSAN clean". ASAN runs on Windows (out/ASAN), but **LSAN
// is Linux-only in Chromium** — build/config/sanitizers/sanitizers.gni only
// turns it on for ASAN fuzzing on Linux/ChromeOS — and Zephyrus is developed on
// Windows. Same constraint TSAN had, same solution: compile the REAL sources
// against a minimal base/ shim and run them on Linux.
//
// **Scope, stated honestly.** This covers the components that allocate and can
// be compiled without the Chromium build graph:
//
//   PrivacyRingBuffer   fixed storage, but its Drain/overflow paths are the
//                       ones a future change is most likely to make allocate
//   DomainStringTable   the allocation-heavy one: a std::map of strings with an
//                       eviction cap and an epoch sweep, i.e. real lifetime
//                       logic that could strand entries
//
// NOT covered here: the entity artifact parser, the SQLite layer and the
// crypto, because they depend on //crypto, //sql and //net and cannot be
// shimmed honestly — a stubbed Ed25519 verifier would test the stub. Those get
// ASAN coverage on Windows (use-after-free, overflow) but no leak checking
// until there is a Linux Chromium checkout. Do not read a PASS here as "the
// feature is leak-free"; read it as "these two components are".
//
// Build and run (WSL / any Linux box with clang):
//
//   cd chrome/browser/zephyrus/privacy/tools
//   clang++ -std=c++20 -fsanitize=address -g -O1 \
//       -I shim -I ../../../../.. \
//       privacy_lsan_harness.cc ../privacy_ring_buffer.cc \
//       ../domain_string_table.cc -o /tmp/privacy_lsan && /tmp/privacy_lsan
//
// **detect_leaks=1 is REQUIRED, not optional.** LSAN ships inside ASAN on
// Linux, but it is NOT enabled by default on this Ubuntu's clang 18 — a run
// without the env var completes silently and reports nothing, which is
// indistinguishable from a clean result:
//
//   ASAN_OPTIONS=detect_leaks=1 /tmp/privacy_lsan
//
// **The exit code is not the whole signal.** Run it once with --leak-positive-
// control to prove LSAN is actually armed in your environment: that mode leaks
// one string on purpose and MUST report "detected memory leaks". A silent clean
// run from a build where LSAN was disabled looks identical to a real pass.

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "chrome/browser/zephyrus/privacy/domain_string_table.h"
#include "chrome/browser/zephyrus/privacy/privacy_ring_buffer.h"

namespace {

using zephyrus_privacy::DomainStringTable;
using zephyrus_privacy::PrivacyRingBuffer;
using zephyrus_privacy::RawEvent;

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// Fill past the cap, sweep repeatedly, clear, refill. If the sweep or the cap
// ever strands an entry, the table's map grows without bound and the strings
// it holds are unreachable-but-alive at exit, which is exactly what LSAN
// reports.
void ExerciseStringTable() {
  auto table = base::MakeRefCounted<DomainStringTable>();

  for (int round = 0; round < 20; ++round) {
    for (uint32_t i = 0; i < DomainStringTable::kMaxEntries + 500; ++i) {
      table->Record(i, "tracker" + std::to_string(i) + ".example.com");
    }
    for (uint32_t i = 0; i < DomainStringTable::kMaxSiteEntries + 50; ++i) {
      table->RecordSite(i, "site" + std::to_string(i) + ".example");
    }
    // Two sweeps retires everything not re-recorded, which is the path that
    // frees.
    table->SweepAndAdvance();
    table->SweepAndAdvance();
    Check(table->size() == 0, "sweep left entries behind");
  }

  table->Record(1, "kept.example");
  table->Clear();
  Check(table->size() == 0, "Clear left entries behind");

  // Drop the last reference: the refcount must actually reach zero, or the
  // table itself leaks.
  table = nullptr;
}

// The producer/consumer path, including the full-ring drop path. The ring owns
// a fixed array, so a leak here would mean something new started allocating.
void ExerciseRingBuffer() {
  auto buffer = std::make_unique<PrivacyRingBuffer>();
  RawEvent event{};
  event.site_id = 7;

  std::thread producer([&] {
    for (int i = 0; i < 200000; ++i) {
      event.domain_hash = static_cast<uint32_t>(i);
      buffer->Push(event);
    }
  });

  std::vector<RawEvent> out(1024);
  uint64_t drained = 0;
  while (drained < 50000) {
    const size_t n = buffer->Drain(base::span<RawEvent>(out));
    drained += n;
    if (n == 0) {
      std::this_thread::yield();
    }
  }
  producer.join();
  while (buffer->Drain(base::span<RawEvent>(out)) > 0) {
  }
}

// Proves LSAN is armed. Without this, a clean run in an environment where leak
// detection was silently off is indistinguishable from a real pass — the same
// trap the TSAN harness hit, where the exit code alone meant nothing.
void LeakPositiveControl() {
  // Leak a large block and retain NO pointer to it. Printing the address, or
  // leaving it in a live stack slot, can let LSAN classify the block as "still
  // reachable" rather than leaked -- a silent clean run, which is the exact
  // false negative this control exists to rule out.
  auto* leaked = new char[1 << 20];
  std::memset(leaked, 0xAB, 1 << 20);
  __asm__ __volatile__("" : : "r"(leaked) : "memory");
  leaked = nullptr;
  (void)leaked;
  std::fputs(
      "positive control: leaked 1 MiB with no retained pointer; LSAN MUST "
      "report a leak below. A clean run here means LSAN is NOT armed and the "
      "real run proves nothing.\n",
      stderr);
}

}  // namespace

int main(int argc, char** argv) {
  const bool positive_control =
      argc > 1 && std::strcmp(argv[1], "--leak-positive-control") == 0;

  const bool skip_threads =
      argc > 2 && std::strcmp(argv[2], "--no-threads") == 0;

  ExerciseStringTable();
  if (!skip_threads) {
    ExerciseRingBuffer();
  }

  if (positive_control) {
    LeakPositiveControl();
  }

  if (failures) {
    std::fprintf(stderr, "%d assertion failure(s)\n", failures);
    return 1;
  }
  std::printf("PASS (assertions). Leak verdict comes from LSAN below, if any.\n");
  return 0;
}
