// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_FUZZ_FUZZER_ENTRY_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_FUZZ_FUZZER_ENTRY_H_

#include <stddef.h>
#include <stdint.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "base/check.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/hash/hash.h"
#include "base/i18n/icu_util.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/zephyrus/adblock/fuzz/adblock_fuzz_targets.h"

namespace zephyrus_adblock::fuzz {

// The input being run, so a CHECK failure inside a parser can save it. Chromium's
// CHECK aborts the process before libFuzzer's own crash handler has the input,
// and a finding that cannot be replayed is not a finding.
inline std::string_view g_current_input;

// Process-wide setup a fuzzer needs once: ICU (the lookahead regex path uses its
// regex engine, which needs its data) and the CHECK handler that saves the input.
class FuzzerEnvironment {
 public:
  FuzzerEnvironment()
      : assert_handler_(base::BindRepeating(&SaveInputAndDie)) {
    base::i18n::InitializeICU();
  }

 private:
  static void SaveInputAndDie(const char* file,
                              int line,
                              std::string_view message,
                              std::string_view stack) {
    const std::string name =
        "checkfail-" + base::NumberToString(base::PersistentHash(g_current_input));
    base::WriteFile(base::FilePath::FromUTF8Unsafe(name),
                    base::as_byte_span(g_current_input));
    std::cerr << "CHECK failed at " << file << ":" << line << ": " << message
              << "\ninput saved to " << name << "\n"
              << stack << "\n";
    std::abort();
  }

  logging::ScopedLogAssertHandler assert_handler_;
};

}  // namespace zephyrus_adblock::fuzz

// One libFuzzer entry point for `target` (a function of adblock_fuzz_targets.h).
// A broken invariant is a CHECK failure, which libFuzzer reports like a crash.
#define ZEPHYRUS_ADBLOCK_FUZZER(target)                                       \
  extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {   \
    static zephyrus_adblock::fuzz::FuzzerEnvironment environment;             \
    const std::string_view input(reinterpret_cast<const char*>(data), size);  \
    zephyrus_adblock::fuzz::g_current_input = input;                          \
    const std::string error = zephyrus_adblock::fuzz::target(input);          \
    CHECK(error.empty()) << error;                                            \
    return 0;                                                                 \
  }

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_FUZZ_FUZZER_ENTRY_H_
