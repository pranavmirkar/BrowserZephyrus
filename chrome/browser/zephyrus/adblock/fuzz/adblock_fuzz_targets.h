// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_FUZZ_ADBLOCK_FUZZ_TARGETS_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_FUZZ_ADBLOCK_FUZZ_TARGETS_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"

class GURL;

namespace zephyrus_adblock::fuzz {

// Every place the blocker reads text or bytes it did not write: filter lists
// (downloaded from third-party hosts), the compiled index (a file in the
// profile), and the rule syntaxes parsed out of them. Each target runs ONE input
// through one parser and through whatever consumes the parser's output, and
// returns "" when nothing was wrong or a description of the first invariant it
// found broken. A crash, a hang or a sanitizer report is a finding on its own;
// the returned text is for the invariants a parser promises (a selector that
// cannot close its own rule, an index that agrees with the engine it was built
// from, a redirect that stays a web address).
//
// The same functions are driven by libFuzzer (fuzz/*_fuzzer.cc, coverage guided,
// under AddressSanitizer) and by a deterministic mutation test that runs with
// the ordinary unit tests (adblock_fuzz_regression_unittest.cc).
using TargetFn = std::string (*)(std::string_view input);

struct Target {
  const char* name;
  TargetFn run;
};

// Filter-list text -> network rules -> compiled index. The engine and the index
// must agree on every probe request (block, document exceptions, stand-in).
std::string ListParser(std::string_view input);
// Arbitrary bytes as a compiled index. Load() may refuse them; if it accepts, any
// lookup must be safe.
std::string IndexBlob(std::string_view input);
// `/regex/` rules: RE2, the widened-regex path and ICU, then matching.
std::string RegexMatcher(std::string_view input);
// `$removeparam` rules and the built-in tracking parameters.
std::string ParamFilter(std::string_view input);
// `$urlskip=` rules, and the steps on their own (first line = steps, the rest =
// the URL they run on).
std::string UrlSkip(std::string_view input);
// `##` / `#@#` / `:style()` cosmetic rules: what they inject is CSS the page
// cannot override, so it must stay inert.
std::string CosmeticEngine(std::string_view input);
// `##+js(...)` scriptlet rules: what they inject is JavaScript built from list
// text, so the arguments must stay string literals.
std::string ScriptletEngine(std::string_view input);
// `!#if` preprocessing, section markers, headers, `!#include` and domain keys.
std::string ListUtil(std::string_view input);

base::span<const Target> Targets();

// The invariants themselves, written independently of the code they judge, so a
// test can hand each one a known-bad value and see it refuse (a check that cannot
// fail proves nothing).
namespace checks {
// "" when `to` is a plain web address different from `from`.
std::string SkipTarget(const GURL& from, const GURL& to);
// A hiding selector that cannot close its rule, open a comment or leave a quote,
// bracket or parenthesis open.
bool SelectorIsInert(std::string_view css);
// The same, and no url(), image-set(), expression(), backslash or at-rule.
bool DeclarationsAreInert(std::string_view declarations);
// `try{var f=zephyrusScriptlets["n"];if(f)f("a");}catch(e){}`: string literals only.
bool CallLineIsOnlyLiterals(std::string_view line);
}  // namespace checks

// A small valid compiled index, for seeding IndexBlob and for tests.
std::vector<uint8_t> SeedIndexBlob();

}  // namespace zephyrus_adblock::fuzz

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_FUZZ_ADBLOCK_FUZZ_TARGETS_H_
