// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_URLSKIP_FILTER_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_URLSKIP_FILTER_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/feature_list.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "third_party/re2/src/re2/re2.h"
#include "url/gurl.h"

namespace zephyrus_adblock {

// Whether a link that only bounces you through a tracker is followed straight to
// its destination. On by default; `--disable-features=ZephyrusAdblockUrlSkip` is
// the kill switch.
BASE_DECLARE_FEATURE(kZephyrusAdblockUrlSkip);

// `$urlskip=`: link-redirector bypass, as in uBlock Origin.
//
// A link such as https://safelinks.protection.outlook.com/?url=https%3A%2F%2Fnews.example
// or https://track.example/click?u=aHR0cHM6Ly9uZXdzLmV4YW1wbGU asks a tracking
// service to log the click and then send you on. The destination is written into
// the link itself, so the detour can be skipped: the page is requested at the
// destination, and the tracker never hears of it.
//
// A rule is a request pattern (anything a network rule can say: host, path,
// `$doc`, `$to=`...) and a list of steps run, in order, on a "current string"
// that starts as the URL:
//   ?name      the value of that query parameter of the current string
//   &N         the NAME of the Nth query parameter (1-based)
//   #          the fragment
//   /regex/    the first capture group of the regex
//   +https     prepend https://, or turn a leading http:// into https://
//   -base64    -safebase64    -uricomponent    decode the current string
//   -blocked   not a step: the rule also applies to a request that would be
//              blocked (otherwise a blocked request is blocked, not skipped)
// The result must be a valid http(s) URL other than the one asked for. If any
// step cannot be done -- the parameter is not there, the regex does not match,
// the base64 is not base64 -- there is no skip and the request goes ahead as
// written. A step this code does not know drops the whole rule.
//
// Only rules from trusted list sections are read (uBlock Origin's own lists,
// the same test as for trusted scriptlets): a redirect the list author controls
// is a way to send you somewhere, so a third-party list cannot have it.
//
// The pattern is matched by AdblockFilterEngine (as for `$removeparam`), except
// that a pattern written as a regex is matched here, with RE2, since the engine
// keeps regex rules apart. Thread-compatible: build once, then use const.
class AdblockUrlSkipFilter {
 public:
  AdblockUrlSkipFilter();
  AdblockUrlSkipFilter(const AdblockUrlSkipFilter&) = delete;
  AdblockUrlSkipFilter& operator=(const AdblockUrlSkipFilter&) = delete;
  ~AdblockUrlSkipFilter();

  // Reads every usable `$urlskip=` line of `list_text` that sits in a trusted
  // section. `trust_unsectioned`: whether lines before the first section marker
  // count as trusted (true for the browser's own built-in rules and in tests).
  // Returns how many rules were usable.
  size_t AddRules(std::string_view list_text, bool trust_unsectioned);

  // The destination `url` should be sent to instead, or nullopt (the usual
  // case). `blocked`: whether the block lists would block this request; then only
  // a `-blocked` rule may skip it. Only a navigation should be passed.
  std::optional<GURL> Skip(const GURL& url,
                           const GURL& initiator,
                           ResourceType type,
                           bool blocked) const;

  // The steps written after `urlskip=`, run on `url` (rule options aside).
  // Exposed so the steps can be tested apart from rule matching.
  static std::optional<GURL> ApplySteps(std::string_view steps,
                                        const GURL& url);

  size_t rule_count() const { return rule_count_; }
  // Lines that mention urlskip but could not be used.
  size_t skipped_count() const { return skipped_count_; }
  // Usable lines left out only because their section is not trusted.
  size_t untrusted_count() const { return untrusted_count_; }

 private:
  struct Step {
    enum Kind {
      kQuery,
      kParamName,
      kFragment,
      kRegex,
      kPlusHttps,
      kBase64,
      kSafeBase64,
      kUriComponent,
    };
    Kind kind = kQuery;
    std::string name;     // kQuery
    size_t position = 0;  // kParamName, 1-based
    std::shared_ptr<const RE2> regex;
  };
  struct Spec {
    std::vector<Step> steps;
    bool also_when_blocked = false;
  };
  // A rule whose pattern is itself a regex.
  struct RegexRule {
    FilterRule options;  // pattern cleared
    std::shared_ptr<const RE2> pattern;
    size_t spec = 0;
  };

  static bool ParseSteps(std::string_view text, Spec* spec);
  bool AddOne(std::string_view line);
  // Runs the steps on `url`; the destination, or nullopt.
  static std::optional<GURL> Run(const Spec& spec, const GURL& url);

  AdblockFilterEngine engine_;       // a rule's tag is its spec index + 1
  std::vector<Spec> specs_;
  std::vector<RegexRule> regex_rules_;
  size_t rule_count_ = 0;
  size_t skipped_count_ = 0;
  size_t untrusted_count_ = 0;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_URLSKIP_FILTER_H_
