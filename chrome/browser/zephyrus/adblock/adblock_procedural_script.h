// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_PROCEDURAL_SCRIPT_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_PROCEDURAL_SCRIPT_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/feature_list.h"

namespace zephyrus_adblock {

// Kill switch: --disable-features=ZephyrusAdblockProceduralCosmetic.
BASE_DECLARE_FEATURE(kZephyrusAdblockProceduralCosmetic);

// The page-side half of procedural cosmetic filters (`:has-text()`,
// `:upward()`, `:xpath()`, `:matches-css()` ...): the operators CSS cannot
// express, which a selector engine in the page has to evaluate.
//
// Returns a complete script for the main world: the runner with `selectors`
// (each a raw procedural selector from a list) embedded as a JSON array, which
// is the only way list text reaches it. It never uses eval or Function, never
// builds markup, and gives up on a selector it does not understand instead of
// guessing, so a list cannot make it do more than hide, remove or restyle the
// elements the page already has.
std::string BuildProceduralScript(const std::vector<std::string>& selectors);

// The runner alone, for tests.
std::string_view ProceduralRunnerSource();

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_PROCEDURAL_SCRIPT_H_
