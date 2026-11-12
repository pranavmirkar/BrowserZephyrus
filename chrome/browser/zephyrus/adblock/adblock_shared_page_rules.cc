// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_shared_page_rules.h"

#include <utility>

#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/zephyrus/adblock/adblock_cosmetic_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_csp_filter.h"
#include "chrome/browser/zephyrus/adblock/adblock_param_filter.h"
#include "chrome/browser/zephyrus/adblock/adblock_scriptlet_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_urlskip_filter.h"

namespace zephyrus_adblock {

BASE_FEATURE(kZephyrusAdblockSharedPageRules,
             "ZephyrusAdblockSharedPageRules",
             base::FEATURE_ENABLED_BY_DEFAULT);

AdblockPageRules::AdblockPageRules() = default;
AdblockPageRules::AdblockPageRules(AdblockPageRules&&) = default;
AdblockPageRules& AdblockPageRules::operator=(AdblockPageRules&&) = default;
AdblockPageRules::~AdblockPageRules() = default;

namespace {

struct Kept {
  base::Lock lock;
  uint64_t key GUARDED_BY(lock) = 0;
  AdblockPageRules rules GUARDED_BY(lock);
};

Kept& TheKept() {
  static base::NoDestructor<Kept> kept;
  return *kept;
}

// A second set of references to the same engines.
AdblockPageRules Share(const AdblockPageRules& rules) {
  AdblockPageRules copy;
  copy.cosmetic = rules.cosmetic;
  copy.scriptlet = rules.scriptlet;
  copy.params = rules.params;
  copy.urlskip = rules.urlskip;
  copy.csp = rules.csp;
  return copy;
}

}  // namespace

AdblockPageRules GetOrBuildSharedPageRules(
    uint64_t key,
    base::FunctionRef<AdblockPageRules()> build) {
  if (key == 0 ||
      !base::FeatureList::IsEnabled(kZephyrusAdblockSharedPageRules)) {
    return build();
  }
  Kept& kept = TheKept();
  // Held across the build on purpose: two profiles opening at once both ask
  // for the same list, and the second should wait a few tens of milliseconds
  // for the first rather than parse the list again and hold a second copy.
  base::AutoLock hold(kept.lock);
  if (kept.key != key) {
    kept.rules = build();
    kept.key = key;
  }
  return Share(kept.rules);
}

void ResetSharedPageRulesForTesting() {
  Kept& kept = TheKept();
  base::AutoLock hold(kept.lock);
  kept.rules = AdblockPageRules();
  kept.key = 0;
}

}  // namespace zephyrus_adblock
