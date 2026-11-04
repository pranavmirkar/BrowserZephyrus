// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_CSP_FILTER_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_CSP_FILTER_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/feature_list.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "url/gurl.h"

namespace zephyrus_adblock {

// Kill switch: --disable-features=ZephyrusAdblockCsp.
BASE_DECLARE_FEATURE(kZephyrusAdblockCsp);

// `$csp=` rules: a page load whose rule says so gets an extra
// Content-Security-Policy header, so the BROWSER refuses the scripts, frames or
// popups the site would have run. (`||site.example^$csp=frame-src 'self'` is how
// the lists stop an ad frame without knowing its address.)
//
// Like `$removeparam`, these lines are read here and not by AdblockFilterEngine,
// which keeps ignoring them: admitted there, the rule would BLOCK the page.
// Matching reuses that engine in a separate instance, so patterns, `$domain`,
// `$third-party` and the request type mean what they mean everywhere else.
//
// A policy can only ever tighten a page (every header is enforced together), so
// the worst a bad list can do is break a site, which a block rule can do too.
// What is refused: a policy with a reporting directive (`report-uri`,
// `report-to`: it would send the page's violations to a third party), any
// control character or comma, one over 4 KB, and a rule that would apply to
// every page of the web.
//
// Exceptions: `@@pattern$csp` cancels every policy for matching pages,
// `@@pattern$csp=value` that one policy.
//
// Thread-compatible: build on any thread, then use const from one at a time.
class AdblockCspFilter {
 public:
  AdblockCspFilter();
  AdblockCspFilter(const AdblockCspFilter&) = delete;
  AdblockCspFilter& operator=(const AdblockCspFilter&) = delete;
  ~AdblockCspFilter();

  // Adds every usable `$csp` line of `list_text`. Returns how many were.
  size_t AddRules(std::string_view list_text);

  // The policies to add to the response for `url`: loaded as a page (`type`
  // kDocument) or a frame (kSubdocument), by `initiator` (empty for a typed
  // address). Empty for every other type, and almost always.
  std::vector<std::string> PoliciesFor(const GURL& url,
                                       const GURL& initiator,
                                       ResourceType type) const;

  // Whether `policy` is one this filter would ever add.
  static bool IsAcceptablePolicy(std::string_view policy);

  size_t rule_count() const { return rule_count_; }
  size_t skipped_count() const { return skipped_count_; }

 private:
  struct Spec {
    bool exception = false;
    std::string policy;  // empty for the exception that cancels every policy
  };

  bool AddOne(std::string_view line);

  AdblockFilterEngine engine_;
  std::vector<Spec> specs_;  // a rule's tag is its index + 1
  size_t rule_count_ = 0;
  size_t skipped_count_ = 0;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_CSP_FILTER_H_
