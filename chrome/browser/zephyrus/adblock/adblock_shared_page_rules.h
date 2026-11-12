// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_SHARED_PAGE_RULES_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_SHARED_PAGE_RULES_H_

#include <stdint.h>

#include <memory>

#include "base/feature_list.h"
#include "base/functional/function_ref.h"

namespace zephyrus_adblock {

class AdblockCosmeticEngine;
class AdblockCspFilter;
class AdblockParamFilter;
class AdblockScriptletEngine;
class AdblockUrlSkipFilter;

// One copy of the page-level rules for the whole process, instead of one per
// profile. On by default; `--disable-features=ZephyrusAdblockSharedPageRules`
// gives every profile its own copy again.
BASE_DECLARE_FEATURE(kZephyrusAdblockSharedPageRules);

// Everything the blocker reads from the lists apart from the request matcher
// (which AdblockSharedIndex shares already): what to hide, what to inject,
// which parameters to strip, which redirects to skip, which policies to add.
//
// Each of these is built once from the list text and never changed afterwards,
// and every query on them is const and keeps no state, so one copy can be read
// by every profile, from any thread. What differs between profiles (the
// allowlist, the on/off switch, the counters) lives in the service, outside.
//
// MEASURED before this existed: these cost about 19 MB of private memory in
// EACH profile, and each profile parsed the same 9 MB of list text again to
// build them.
struct AdblockPageRules {
  AdblockPageRules();
  AdblockPageRules(AdblockPageRules&&);
  AdblockPageRules& operator=(AdblockPageRules&&);
  ~AdblockPageRules();

  std::shared_ptr<const AdblockCosmeticEngine> cosmetic;
  std::shared_ptr<const AdblockScriptletEngine> scriptlet;
  std::shared_ptr<const AdblockParamFilter> params;
  std::shared_ptr<const AdblockUrlSkipFilter> urlskip;
  std::shared_ptr<const AdblockCspFilter> csp;
};

// The rules for the list `key` identifies: the copy another profile already
// built, or `build`'s result, which is then kept for the next caller. A caller
// that arrives while another is building the same list waits for it instead of
// building a second copy. May block; call from a thread that is allowed to.
//
// `key` 0 means "do not share": `build` is always run and nothing is kept.
// So does the feature being off.
//
// Only the newest list is kept. A profile still holding an older one keeps it
// alive until it reloads, and no longer.
AdblockPageRules GetOrBuildSharedPageRules(
    uint64_t key,
    base::FunctionRef<AdblockPageRules()> build);

// Forgets the kept copy, so a test starts from nothing.
void ResetSharedPageRulesForTesting();

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_SHARED_PAGE_RULES_H_
