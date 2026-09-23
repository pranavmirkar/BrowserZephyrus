// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"

#include <algorithm>
#include <optional>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"

namespace zephyrus_adblock {

namespace {

// ABP "separator" (^): anything that is not a letter, digit, or one of _%.-,
// and the end of the string also counts as a separator.
bool IsSeparator(char c) {
  return !(base::IsAsciiAlphaNumeric(c) || c == '_' || c == '%' || c == '.' ||
           c == '-');
}

// Matches `pattern` (supporting '*' wildcard and '^' separator) against `text`
// starting at text[ti]. If `anchor_end`, the match must consume to the end.
// `budget` bounds total work: it is decremented on each step and, if exhausted,
// the match fails safe (returns false = "no match" = don't block). This makes
// matching provably bounded so a pathological pattern can never hang the UI
// thread (a DoS guard, important now that filter lists auto-update).
bool MatchAt(const std::string& pattern,
             size_t pi,
             const std::string& text,
             size_t ti,
             bool anchor_end,
             int& budget) {
  if (--budget < 0) {
    return false;
  }
  while (pi < pattern.size()) {
    const char c = pattern[pi];
    if (c == '*') {
      // Collapse consecutive wildcards.
      while (pi < pattern.size() && pattern[pi] == '*') {
        ++pi;
      }
      if (pi == pattern.size()) {
        return true;  // trailing '*' matches the remainder
      }
      for (size_t k = ti; k <= text.size(); ++k) {
        if (MatchAt(pattern, pi, text, k, anchor_end, budget)) {
          return true;
        }
        if (budget < 0) {
          return false;
        }
      }
      return false;
    }
    if (c == '^') {
      if (ti == text.size()) {
        ++pi;  // end-of-string satisfies a separator without consuming
        continue;
      }
      if (IsSeparator(text[ti])) {
        ++pi;
        ++ti;
        continue;
      }
      return false;
    }
    // Literal character.
    if (ti < text.size() && text[ti] == c) {
      ++pi;
      ++ti;
      continue;
    }
    return false;
  }
  return anchor_end ? (ti == text.size()) : true;
}

// The rule's hash-bucket key: the longest run of [a-z0-9], length >= 3, that
// the pattern BOUNDS on both sides. Empty if there is none, which files the
// rule with the ones tried against every request.
//
// A request only looks in the buckets named by its own tokens, and a token is
// a WHOLE run of letters and digits in the URL. So a keyword is sound only if
// every URL the rule matches carries it as a whole token -- which holds only
// when the pattern pins both ends of the run: a literal separator, a `^`, a
// hostname or start anchor on the left, an end anchor on the right. A run that
// touches a `*`, or the pattern's own unanchored end, may be the start of a
// longer token: `/banner` matches `/banners/x.png`, whose token is "banners",
// so a rule filed under "banner" was never even tried there. MEASURED: 412
// rules in the shipped list were filed that way.
std::string FindKeyword(const std::string& pattern,
                        bool start_anchored,
                        bool end_anchored) {
  std::string best;
  const size_t n = pattern.size();
  size_t i = 0;
  while (i < n) {
    if (!base::IsAsciiAlphaNumeric(pattern[i])) {
      ++i;
      continue;
    }
    size_t j = i;
    while (j < n && base::IsAsciiAlphaNumeric(pattern[j])) {
      ++j;
    }
    const bool left_bounded = i == 0 ? start_anchored : pattern[i - 1] != '*';
    const bool right_bounded = j == n ? end_anchored : pattern[j] != '*';
    if (left_bounded && right_bounded && j - i >= 3 && j - i > best.size()) {
      best = pattern.substr(i, j - i);
    }
    i = j;
  }
  return best;
}

// All [a-z0-9] runs of length >= 3 in `url_lower`, as views into it: the
// bucket maps take a string_view key directly, so a request no longer copies
// every token it looks up.
std::vector<std::string_view> ExtractTokens(std::string_view url_lower) {
  std::vector<std::string_view> tokens;
  size_t start = 0;
  for (size_t i = 0; i <= url_lower.size(); ++i) {
    if (i < url_lower.size() && base::IsAsciiAlphaNumeric(url_lower[i])) {
      continue;
    }
    if (i - start >= 3) {
      tokens.push_back(url_lower.substr(start, i - start));
    }
    start = i + 1;
  }
  return tokens;
}

// True if `host` is `domain` or a subdomain of it, or -- for an entity rule
// (`domain=google.*`) -- the same name under any public suffix. Entity domains
// used to be compared as literal text, so the rules using them never matched.
bool HostMatchesDomain(std::string_view host, const std::string& domain) {
  return HostMatchesFilterDomain(host, domain);
}

// `$redirect=` resources that are only an empty stand-in: blocking the request
// outright gives the page the same nothing. Rules redirecting to a SURROGATE
// (google-ima.js, googletagservices_gpt.js...) are different -- the page calls
// into the fake -- so those stay unsupported rather than become a block that
// breaks the player the surrogate was keeping alive.
bool IsEmptyRedirect(std::string_view resource) {
  // uBO appends a priority as ":N".
  resource = resource.substr(0, resource.find(':'));
  static constexpr std::string_view kEmpty[] = {
      "noopjs",       "noop.js",       "nooptext",   "noop.txt",
      "noopframe",    "noop.html",     "noopjson",   "noop.json",
      "noopmp3-0.1s", "noop-0.1s.mp3", "noopmp4-1s", "noop-1s.mp4",
      "1x1.gif",      "2x2.png",       "3x2.png",    "32x32.png",
      "1x1-transparent.gif", "2x2-transparent.png", "3x2-transparent.png",
      "32x32-transparent.png", "empty", "none",
  };
  for (std::string_view name : kEmpty) {
    if (resource == name) {
      return true;
    }
  }
  return false;
}

// For a `$badfilter` rule, the text of the rule it cancels (the same line
// without that option); nullopt for any other line.
std::optional<std::string> BadfilterTarget(std::string_view line) {
  const size_t dollar = line.rfind('$');
  if (dollar == std::string_view::npos) {
    return std::nullopt;
  }
  std::vector<std::string_view> kept;
  bool found = false;
  for (std::string_view opt :
       base::SplitStringPiece(line.substr(dollar + 1), ",",
                              base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (opt == "badfilter") {
      found = true;
    } else {
      kept.push_back(opt);
    }
  }
  if (!found) {
    return std::nullopt;
  }
  std::string target(line.substr(0, dollar));
  if (!kept.empty()) {
    target += '$';
    target += base::JoinString(kept, ",");
  }
  return target;
}

// Whether a pattern is nothing but a hostname: "ads.example.com^" or
// "ads.example.com", with no path, wildcard or query.
bool IsPureHostPattern(std::string_view pattern) {
  if (pattern.ends_with('^')) {
    pattern.remove_suffix(1);
  }
  if (pattern.empty()) {
    return false;
  }
  for (char c : pattern) {
    if (!base::IsAsciiAlphaNumeric(c) && c != '.' && c != '-' && c != '_') {
      return false;
    }
  }
  return true;
}

}  // namespace

AdblockFilterEngine::AdblockFilterEngine() = default;
AdblockFilterEngine::~AdblockFilterEngine() = default;

size_t AdblockFilterEngine::AddRules(std::string_view filter_list_text) {
  const std::vector<std::string_view> lines =
      base::SplitStringPiece(filter_list_text, "\n", base::TRIM_WHITESPACE,
                             base::SPLIT_WANT_NONEMPTY);
  // `$badfilter` cancels a rule wherever it sits in the list, including above
  // itself, so every cancellation is known before any rule is kept.
  for (std::string_view raw : lines) {
    if (raw[0] != '!' && raw.find("badfilter") != std::string_view::npos) {
      if (std::optional<std::string> target = BadfilterTarget(raw)) {
        badfilters_.insert(*std::move(target));
      }
    }
  }
  size_t added = 0;
  for (std::string_view raw : lines) {
    std::string line(raw);
    // Comments and list headers.
    if (line[0] == '!' || line[0] == '[') {
      continue;
    }
    if (!badfilters_.empty() && badfilters_.contains(line)) {
      continue;
    }
    // Cosmetic filters (## #@# #?# #$#) are handled by a later phase.
    if (line.find("##") != std::string::npos ||
        line.find("#@#") != std::string::npos ||
        line.find("#?#") != std::string::npos ||
        line.find("#$#") != std::string::npos) {
      continue;
    }
    // Regex rules (/.../) are not supported yet.
    if (line.size() >= 2 && line.front() == '/' && line.back() == '/') {
      continue;
    }

    FilterRule rule;
    // Exception (allowlist) rule.
    if (base::StartsWith(line, "@@")) {
      rule.is_exception = true;
      line = line.substr(2);
    }

    // Split pattern$options at the first '$'.
    std::string options;
    size_t dollar = line.find('$');
    if (dollar != std::string::npos) {
      options = line.substr(dollar + 1);
      line = line.substr(0, dollar);
    }

    // A regex rule is recognised by its PATTERN, not the whole line. The check
    // above only catches one with no options; `/ad[0-9]+\.js/$script` got
    // past it and was stored as a literal pattern that could never match, and
    // was still tried against every request.
    if (line.size() >= 2 && line.front() == '/' && line.back() == '/') {
      continue;
    }

    // Parse options; bail on any option we don't understand to avoid
    // over-blocking with wrong semantics.
    bool unsupported = false;
    if (!options.empty()) {
      for (std::string_view opt_piece : base::SplitStringPiece(
               options, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
        std::string opt(opt_piece);
        bool negate = false;
        if (!opt.empty() && opt[0] == '~') {
          negate = true;
          opt = opt.substr(1);
        }
        auto set_type = [&](ResourceType t) {
          if (negate) {
            rule.type_mask_not |= t;
          } else {
            rule.type_mask |= t;
          }
        };
        if (opt == "third-party" || opt == "3p") {
          rule.party = negate ? 1 : 2;
        } else if (opt == "first-party" || opt == "1p") {
          rule.party = negate ? 2 : 1;
        } else if (opt == "script") {
          set_type(kTypeScript);
        } else if (opt == "image") {
          set_type(kTypeImage);
        } else if (opt == "stylesheet" || opt == "css") {
          set_type(kTypeStylesheet);
        } else if (opt == "object") {
          set_type(kTypeObject);
        } else if (opt == "xmlhttprequest" || opt == "xhr") {
          set_type(kTypeXhr);
        } else if (opt == "subdocument" || opt == "frame") {
          set_type(kTypeSubdocument);
        } else if (opt == "font") {
          set_type(kTypeFont);
        } else if (opt == "media") {
          set_type(kTypeMedia);
        } else if (opt == "ping") {
          set_type(kTypePing);
        } else if (opt == "websocket") {
          set_type(kTypeWebsocket);
        } else if (opt == "popup" || opt == "popunder") {
          set_type(kTypePopup);
        } else if (opt == "document" || opt == "doc") {
          // On an exception, $document allowlists the pages it matches; on a
          // block rule it blocks the page itself (scam and click-redirect
          // pages).
          if (rule.is_exception && !negate) {
            rule.document_exceptions |= kExceptDocument;
          } else {
            set_type(kTypeDocument);
          }
        } else if (opt == "all" && !negate) {
          rule.type_mask |= kTypeEverything;
        } else if ((opt == "generichide" || opt == "ghide") &&
                   rule.is_exception && !negate) {
          rule.document_exceptions |= kExceptGenericHide;
        } else if ((opt == "elemhide" || opt == "ehide") &&
                   rule.is_exception && !negate) {
          rule.document_exceptions |= kExceptElemHide;
        } else if ((opt == "specifichide" || opt == "shide") &&
                   rule.is_exception && !negate) {
          rule.document_exceptions |= kExceptSpecificHide;
        } else if (opt.starts_with("redirect=") && !rule.is_exception &&
                   IsEmptyRedirect(std::string_view(opt).substr(9))) {
          // Blocking hands the page the same empty response the redirect
          // would have; see IsEmptyRedirect.
        } else if (opt.starts_with("denyallow=") && !negate) {
          for (std::string_view d : base::SplitStringPiece(
                   std::string_view(opt).substr(10), "|",
                   base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
            if (d[0] == '~') {
              unsupported = true;
              break;
            }
            rule.denyallow.push_back(base::ToLowerASCII(d));
          }
          if (unsupported) {
            break;
          }
        } else if (opt == "other") {
          set_type(kTypeOther);
        } else if (opt == "important") {
          rule.important = true;
        } else if (base::StartsWith(opt, "domain=") ||
                   base::StartsWith(opt, "from=")) {
          // uBO spells $domain= as $from= too.
          const size_t eq = opt.find('=');
          for (std::string_view d : base::SplitStringPiece(
                   std::string_view(opt).substr(eq + 1), "|",
                   base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
            std::string ds = base::ToLowerASCII(d);
            if (!ds.empty() && ds[0] == '~') {
              rule.domains_excluded.push_back(ds.substr(1));
            } else {
              rule.domains_included.push_back(ds);
            }
          }
        } else {
          // redirect=, csp=, removeparam, header=, popup, generichide, etc.
          unsupported = true;
          break;
        }
      }
    }
    if (unsupported) {
      continue;
    }

    // Anchors.
    if (base::StartsWith(line, "||")) {
      rule.anchor = FilterRule::kAnchorHostname;
      line = line.substr(2);
    } else if (base::StartsWith(line, "|")) {
      rule.anchor = FilterRule::kAnchorStart;
      line = line.substr(1);
    }
    if (base::EndsWith(line, "|")) {
      rule.anchor_end = true;
      line = line.substr(0, line.size() - 1);
    }

    if (line.empty()) {
      continue;
    }
    // Reject patterns with an excessive number of '*' wildcards. Real filter
    // rules almost never have more than a couple; a pattern with many wildcards
    // and no strong keyword lands in the "test on every request" bucket and can
    // trigger catastrophic backtracking in MatchAt() on long URLs — a UI-thread
    // hang (DoS). This defends against a bad rule slipping in via auto-update.
    if (std::count(line.begin(), line.end(), '*') > 5) {
      continue;
    }
    rule.pattern = base::ToLowerASCII(line);
    rule.pure_host = rule.anchor == FilterRule::kAnchorHostname &&
                     !rule.anchor_end && IsPureHostPattern(rule.pattern);
    AddParsedRule(std::move(rule));
    ++added;
  }
  return added;
}

void AdblockFilterEngine::AddParsedRule(FilterRule rule) {
  const bool exception = rule.is_exception;
  if (exception) {
    ++exception_rule_count_;
  } else {
    ++block_rule_count_;
  }
  std::string keyword =
      FindKeyword(rule.pattern, rule.anchor != FilterRule::kAnchorNone,
                  rule.anchor_end);
  auto file = [&keyword](RuleBuckets& buckets,
                         std::vector<FilterRule>& untokenized, FilterRule r) {
    if (keyword.empty()) {
      untokenized.push_back(std::move(r));
    } else {
      buckets[keyword].push_back(std::move(r));
    }
  };
  if (rule.document_exceptions) {
    // `@@||x^$document,subdocument` both allowlists pages on x AND is an
    // ordinary exception for x's frames: file one copy per role.
    if (rule.type_mask != kTypeAll) {
      FilterRule request_rule = rule;
      request_rule.document_exceptions = 0;
      file(exception_buckets_, untokenized_exception_,
           std::move(request_rule));
    }
    rule.type_mask = kTypeAll;
    rule.type_mask_not = 0;
    file(document_buckets_, untokenized_document_, std::move(rule));
    return;
  }
  if (exception) {
    file(exception_buckets_, untokenized_exception_, std::move(rule));
  } else if (rule.important) {
    file(important_buckets_, untokenized_important_, std::move(rule));
  } else {
    file(block_buckets_, untokenized_block_, std::move(rule));
  }
}

bool AdblockFilterEngine::AnyRuleMatches(
    const RuleBuckets& buckets,
    const std::vector<FilterRule>& untokenized,
    const std::string& url_lower,
    std::string_view initiator_host,
    bool third_party,
    ResourceType type,
    uint32_t* collect_exceptions) const {
  // Host boundaries within `url_lower`, for "||" hostname-anchored matching.
  size_t host_start = url_lower.find("://");
  host_start = (host_start == std::string::npos) ? 0 : host_start + 3;
  size_t host_end = url_lower.find_first_of("/:?", host_start);
  if (host_end == std::string::npos) {
    host_end = url_lower.size();
  }

  const std::string_view request_host =
      std::string_view(url_lower).substr(host_start, host_end - host_start);

  auto rule_matches = [&](const FilterRule& rule) -> bool {
    // Resource-type scoping.
    if (rule.type_mask != kTypeAll && !(rule.type_mask & type)) {
      return false;
    }
    // An untyped block rule reaches a whole page or a pop-up only when it
    // names a host; see FilterRule::pure_host.
    if (rule.type_mask == kTypeAll && !rule.is_exception &&
        (type & (kTypeDocument | kTypePopup)) && !rule.pure_host) {
      return false;
    }
    for (const std::string& d : rule.denyallow) {
      if (HostMatchesDomain(request_host, d)) {
        return false;
      }
    }
    if (rule.type_mask_not & type) {
      return false;
    }
    // First/third-party scoping.
    if (rule.party == 1 && third_party) {
      return false;
    }
    if (rule.party == 2 && !third_party) {
      return false;
    }
    // $domain= scoping (matched against the document/initiator host).
    if (!rule.domains_included.empty()) {
      bool ok = false;
      for (const std::string& d : rule.domains_included) {
        if (HostMatchesDomain(initiator_host, d)) {
          ok = true;
          break;
        }
      }
      if (!ok) {
        return false;
      }
    }
    for (const std::string& d : rule.domains_excluded) {
      if (HostMatchesDomain(initiator_host, d)) {
        return false;
      }
    }
    // Pattern match, within ONE step budget for the whole rule.
    //
    // The budget used to be reset at every candidate position, so the bound
    // it promised was 100000 steps TIMES the URL length. MEASURED: a
    // five-wildcard pattern the parser admits took 721 ms against one
    // 1500-character URL -- on the UI thread, where every request is matched.
    // Exhausting the budget fails safe: no match, so the request is allowed.
    constexpr int kMatchBudget = 100000;
    int budget = kMatchBudget;
    switch (rule.anchor) {
      case FilterRule::kAnchorStart:
        return MatchAt(rule.pattern, 0, url_lower, 0, rule.anchor_end, budget);
      case FilterRule::kAnchorHostname: {
        // Match at the host start or any label boundary within the host.
        for (size_t pos = host_start; pos < host_end; ++pos) {
          if (pos == host_start || url_lower[pos - 1] == '.') {
            if (MatchAt(rule.pattern, 0, url_lower, pos, rule.anchor_end,
                        budget)) {
              return true;
            }
            if (budget < 0) {
              return false;
            }
          }
        }
        return false;
      }
      case FilterRule::kAnchorNone:
      default: {
        if (rule.pattern.empty()) {
          return false;
        }
        const char first = rule.pattern[0];
        // A leading `*` matches from anywhere, so one attempt at 0 covers
        // every start.
        if (first == '*') {
          return MatchAt(rule.pattern, 0, url_lower, 0, rule.anchor_end,
                         budget);
        }
        // A leading literal can only start where that character is, so jump
        // between its occurrences instead of trying every position.
        const bool literal = first != '^';
        for (size_t pos = literal ? url_lower.find(first) : 0;
             pos != std::string::npos && pos <= url_lower.size();
             pos = literal ? url_lower.find(first, pos + 1) : pos + 1) {
          if (MatchAt(rule.pattern, 0, url_lower, pos, rule.anchor_end,
                      budget)) {
            return true;
          }
          if (budget < 0) {
            return false;
          }
        }
        return false;
      }
    }
  };

  bool matched = false;
  auto on_match = [&](const FilterRule& rule) {
    matched = true;
    if (collect_exceptions) {
      *collect_exceptions |= rule.document_exceptions;
    }
  };
  for (std::string_view token : ExtractTokens(url_lower)) {
    auto it = buckets.find(token);
    if (it == buckets.end()) {
      continue;
    }
    for (const FilterRule& rule : it->second) {
      if (rule_matches(rule)) {
        on_match(rule);
        if (!collect_exceptions) {
          return true;
        }
      }
    }
  }
  for (const FilterRule& rule : untokenized) {
    if (rule_matches(rule)) {
      on_match(rule);
      if (!collect_exceptions) {
        return true;
      }
    }
  }
  return matched;
}

bool AdblockFilterEngine::ShouldBlock(const GURL& url,
                                      const GURL& initiator,
                                      ResourceType type) const {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  const std::string url_lower = base::ToLowerASCII(url.spec());

  bool third_party = false;
  std::string initiator_host;
  if (initiator.is_valid() && !initiator.host().empty()) {
    initiator_host = initiator.host();
    third_party = !net::registry_controlled_domains::SameDomainOrHost(
        url, initiator,
        net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  }

  // $important wins over every exception: it is how a list says "even where
  // something else allowlisted this".
  if (AnyRuleMatches(important_buckets_, untokenized_important_, url_lower,
                     initiator_host, third_party, type)) {
    return true;
  }
  if (!AnyRuleMatches(block_buckets_, untokenized_block_, url_lower,
                      initiator_host, third_party, type)) {
    return false;
  }
  // A matching allowlist (@@) rule un-blocks the request.
  if (AnyRuleMatches(exception_buckets_, untokenized_exception_, url_lower,
                     initiator_host, third_party, type)) {
    return false;
  }
  // So does a $document exception for the page making it. Checked last, and
  // only once something would be blocked, so ordinary requests never pay it.
  if (initiator.is_valid() &&
      (GetDocumentExceptions(initiator) & kExceptDocument)) {
    return false;
  }
  return true;
}

uint32_t AdblockFilterEngine::GetDocumentExceptions(
    const GURL& document_url) const {
  if (!document_url.is_valid() || !document_url.SchemeIsHTTPOrHTTPS() ||
      (document_buckets_.empty() && untokenized_document_.empty())) {
    return 0;
  }
  uint32_t exceptions = 0;
  AnyRuleMatches(document_buckets_, untokenized_document_,
                 base::ToLowerASCII(document_url.spec()), document_url.host(),
                 /*third_party=*/false, kTypeDocument, &exceptions);
  return exceptions;
}

}  // namespace zephyrus_adblock
