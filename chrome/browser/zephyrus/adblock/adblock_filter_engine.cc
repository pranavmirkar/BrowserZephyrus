// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"

#include <algorithm>
#include <optional>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_redirect_resources.h"
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

// Whether `line` is a cosmetic, procedural or scriptlet filter rather than a
// network rule. Those join their domain list to their body with `#`, an
// optional `@` (an exception), an optional run of `?`, `$` or `%`, and `#`:
//
//   ##   #@#   #?#   #@?#   #$#   #@$#   #$?#   #@$?#   #%#   #@%#
//
// This used to test four of the ten spellings. The other six were admitted as
// network rules with a pattern like `filecrypt.co#@?#form > .protection`, which
// no request can match: dead weight in the index, and the real filter they
// carried was never applied by anything.
bool HasCosmeticSeparator(std::string_view line) {
  for (size_t at = line.find('#'); at != std::string_view::npos;
       at = line.find('#', at + 1)) {
    size_t i = at + 1;
    if (i < line.size() && line[i] == '@') {
      ++i;
    }
    while (i < line.size() &&
           (line[i] == '?' || line[i] == '$' || line[i] == '%')) {
      ++i;
    }
    if (i < line.size() && line[i] == '#') {
      return true;
    }
  }
  return false;
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

// The longest domain list that is simply searched end to end; past this the
// list is sorted and searched by host suffix.
constexpr size_t kLinearListMax = 6;

bool IsEntityDomain(const std::string& domain) {
  return domain.size() > 2 && domain.ends_with(".*");
}

// Sorts a domain list for HostInDomainList: plain domains in order, then the
// `name.*` entities (which need their own, slower, comparison).
void SortDomainList(std::vector<std::string>& list) {
  std::sort(list.begin(), list.end(),
            [](const std::string& a, const std::string& b) {
              const bool ea = IsEntityDomain(a);
              const bool eb = IsEntityDomain(b);
              return ea != eb ? eb : a < b;
            });
  list.erase(std::unique(list.begin(), list.end()), list.end());
}

// Whether `host` is one of the domains in `list`, or a subdomain of one.
bool HostInDomainList(std::string_view host,
                      const std::vector<std::string>& list,
                      bool sorted) {
  if (!sorted || list.size() <= kLinearListMax) {
    for (const std::string& d : list) {
      if (HostMatchesDomain(host, d)) {
        return true;
      }
    }
    return false;
  }
  const auto entities = std::partition_point(
      list.begin(), list.end(),
      [](const std::string& d) { return !IsEntityDomain(d); });
  // `host` is in the list if it, or any suffix that starts after a dot, is.
  for (std::string_view suffix = host;;) {
    const auto it = std::lower_bound(
        list.begin(), entities, suffix,
        [](const std::string& a, std::string_view b) {
          return std::string_view(a) < b;
        });
    if (it != entities && std::string_view(*it) == suffix) {
      return true;
    }
    const size_t dot = suffix.find('.');
    if (dot == std::string_view::npos) {
      break;
    }
    suffix.remove_prefix(dot + 1);
  }
  for (auto it = entities; it != list.end(); ++it) {
    if (HostMatchesDomain(host, *it)) {
      return true;
    }
  }
  return false;
}

// Reads `redirect=name[:priority]` / `redirect-rule=name[:priority]` (and the
// bare `redirect-rule` of an exception) into `rule`. False when the option is
// one this engine does not act on, which drops the whole line:
//  * a resource there is no stand-in for (the lists name them; an unknown name is
//    discarded, as uBO does),
//  * a `redirect=` exception (only `redirect-rule` exceptions are defined),
//  * a negated option, or a priority that is not a number.
// `redirect=none` is a plain block: the name that says "no stand-in".
bool ParseRedirectOption(std::string_view opt, bool negate, FilterRule* rule) {
  if (negate) {
    return false;
  }
  const bool only = opt.starts_with("redirect-rule");
  std::string_view value = opt.substr(only ? 13 : 8);
  if (!value.empty() && value.front() != '=') {
    return false;  // `redirect-ruleX`, `redirectX`
  }
  if (rule->is_exception && !only) {
    return false;
  }
  if (value.empty()) {
    // A bare `redirect-rule` cancels every redirect the exception matches.
    if (!rule->is_exception) {
      return false;
    }
    rule->redirect_any = true;
    return true;
  }
  value.remove_prefix(1);
  std::string_view name = value.substr(0, value.find(':'));
  int priority = 0;
  if (value.size() > name.size() &&
      !base::StringToInt(value.substr(name.size() + 1), &priority)) {
    return false;
  }
  if (name == "none") {
    return !only && !rule->is_exception;  // blocks, with nothing to serve
  }
  const RedirectResource* resource = FindRedirectResource(name);
  if (!resource) {
    return false;
  }
  rule->redirect_resource = resource->id;
  rule->redirect_priority = static_cast<int8_t>(std::clamp(priority, -100, 100));
  rule->redirect_only = only;
  return true;
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

// The longest regex source accepted. The longest in the shipped lists is a few
// hundred bytes; the cap is for a list that tries to make the compiler work.
constexpr size_t kMaxRegexLength = 2048;

}  // namespace

bool RuleOptionsMatch(const FilterRule& rule,
                      ResourceType type,
                      bool third_party,
                      std::string_view initiator_host,
                      std::string_view request_host) {
  // The cheap tests first (integers), the ones that compare host names last;
  // every one has to pass, so the order changes the cost and nothing else.
  // Resource-type scoping.
  if (rule.type_mask != kTypeAll && !(rule.type_mask & type)) {
    return false;
  }
  // An untyped block rule reaches a whole page or a pop-up only when it names
  // a host; see FilterRule::pure_host.
  if (rule.type_mask == kTypeAll && !rule.is_exception &&
      (type & (kTypeDocument | kTypePopup)) && !rule.pure_host) {
    return false;
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
  const bool sorted = rule.domain_lists_sorted;
  // $domain= scoping (matched against the document/initiator host).
  if (!rule.domains_included.empty() &&
      !HostInDomainList(initiator_host, rule.domains_included, sorted)) {
    return false;
  }
  if (!rule.domains_excluded.empty() &&
      HostInDomainList(initiator_host, rule.domains_excluded, sorted)) {
    return false;
  }
  if (!rule.denyallow.empty() &&
      HostInDomainList(request_host, rule.denyallow, sorted)) {
    return false;
  }
  // $to= scoping (matched against the request's own host).
  if (!rule.to_included.empty() &&
      !HostInDomainList(request_host, rule.to_included, sorted)) {
    return false;
  }
  if (!rule.to_excluded.empty() &&
      HostInDomainList(request_host, rule.to_excluded, sorted)) {
    return false;
  }
  return true;
}

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
    // Cosmetic, procedural and scriptlet filters are handled by their own
    // engines; none of them is a network rule.
    if (HasCosmeticSeparator(line)) {
      continue;
    }
    FilterRule rule;
    // Exception (allowlist) rule.
    if (base::StartsWith(line, "@@")) {
      rule.is_exception = true;
      line = line.substr(2);
    }

    // A regex rule is `/.../`, optionally followed by `$options`. It is
    // recognised BEFORE the options are split off, because the regex may hold a
    // `$` of its own (an end anchor), and the split would cut it there.
    std::string options;
    std::string regex_source;
    bool is_regex = false;
    if (line.size() >= 2 && line[0] == '/') {
      const size_t close = line.rfind('/');
      if (close > 0 && (close + 1 == line.size() || line[close + 1] == '$')) {
        is_regex = true;
        regex_source = line.substr(1, close - 1);
        if (close + 1 < line.size()) {
          options = line.substr(close + 2);
        }
      }
    }

    if (!is_regex) {
      // Split pattern$options at the first '$'.
      size_t dollar = line.find('$');
      if (dollar != std::string::npos) {
        options = line.substr(dollar + 1);
        line = line.substr(0, dollar);
      }
      // Whatever still looks like a regex here was not one by the test above
      // (its options hold a `/`): skip it. It used to be stored as a literal
      // pattern that could never match and was still tried against every
      // request.
      if (line.size() >= 2 && line.front() == '/' && line.back() == '/') {
        continue;
      }
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
        } else if (opt == "redirect" || opt.starts_with("redirect=") ||
                   opt == "redirect-rule" || opt.starts_with("redirect-rule=")) {
          if (!ParseRedirectOption(opt, negate, &rule)) {
            unsupported = true;
            break;
          }
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
        } else if (opt == "match-case" && !negate) {
          rule.match_case = true;
        } else if (base::StartsWith(opt, "to=") && !negate) {
          // $to= is $domain= for the request's destination: the rule applies
          // only to requests whose host is one of these (or a subdomain),
          // leaving out the ones written with a leading `~`.
          for (std::string_view d : base::SplitStringPiece(
                   std::string_view(opt).substr(3), "|", base::TRIM_WHITESPACE,
                   base::SPLIT_WANT_NONEMPTY)) {
            std::string ds = base::ToLowerASCII(d);
            if (!ds.empty() && ds[0] == '~') {
              rule.to_excluded.push_back(ds.substr(1));
            } else {
              rule.to_included.push_back(ds);
            }
          }
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
    for (std::vector<std::string>* list :
         {&rule.domains_included, &rule.domains_excluded, &rule.denyallow,
          &rule.to_included, &rule.to_excluded}) {
      SortDomainList(*list);
    }
    rule.domain_lists_sorted = true;

    if (is_regex) {
      // A tagged rule is found again by ForEachMatchingRule, which does not see
      // regexes, so one added that way would never apply. A page-level
      // exception granted by a regex is not supported either.
      if (pending_tag_ != 0 || rule.document_exceptions != 0 ||
          regex_source.empty() || regex_source.size() > kMaxRegexLength) {
        continue;
      }
      if (rule.redirect_only || rule.redirect_any ||
          (rule.is_exception && rule.redirect_resource != kNoRedirectResource)) {
        continue;  // a regex cannot carry a stand-in directive here
      }
      // `redirect=` on a regex blocks, as before; its stand-in is not served.
      rule.redirect_resource = kNoRedirectResource;
      rule.redirect_priority = 0;
      rule.is_regex = true;
      rule.pattern = std::move(regex_source);
      regex_rules_.push_back(std::move(rule));
      ++added;
      continue;
    }
    // Only a regex can be case-sensitive; see FilterRule::match_case.
    if (rule.match_case) {
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
  if (pending_tag_ != 0) {
    rule.tag = pending_tag_;
    if (rule.type_mask == kTypeAll) {
      // An untyped block rule is kept off page loads (see
      // FilterRule::pure_host). A tagged rule is not a block; name every type
      // so a navigation is one.
      rule.type_mask = kTypeEverything;
    }
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
  if (rule.redirect_resource != kNoRedirectResource || rule.redirect_any) {
    // A stand-in directive. An exception for one only cancels directives; it
    // never unblocks. `redirect=` is a block rule AND a directive: the directive
    // is filed here and the rest of this function files the block half.
    if (rule.is_exception) {
      file(redirect_exception_buckets_, untokenized_redirect_exception_,
           std::move(rule));
      return;
    }
    ++redirect_rule_count_;
    const bool only = rule.redirect_only;
    FilterRule directive = rule;
    file(redirect_buckets_, untokenized_redirect_, std::move(directive));
    if (only) {
      return;  // `redirect-rule=` blocks nothing itself
    }
    rule.redirect_resource = kNoRedirectResource;
    rule.redirect_priority = 0;
  }
  const bool exception = rule.is_exception;
  if (exception) {
    ++exception_rule_count_;
  } else {
    ++block_rule_count_;
  }
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
    uint32_t* collect_exceptions,
    const base::FunctionRef<void(const FilterRule&)>* on_each_match) const {
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
    if (!RuleOptionsMatch(rule, type, third_party, initiator_host,
                          request_host)) {
      return false;
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
  // Collecting document exceptions or visiting every match means looking at
  // every candidate; otherwise the first match is the answer.
  const bool visit_all = collect_exceptions || on_each_match;
  auto on_match = [&](const FilterRule& rule) {
    matched = true;
    if (collect_exceptions) {
      *collect_exceptions |= rule.document_exceptions;
    }
    if (on_each_match) {
      (*on_each_match)(rule);
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
        if (!visit_all) {
          return true;
        }
      }
    }
  }
  for (const FilterRule& rule : untokenized) {
    if (rule_matches(rule)) {
      on_match(rule);
      if (!visit_all) {
        return true;
      }
    }
  }
  return matched;
}

bool AdblockFilterEngine::ShouldBlock(const GURL& url,
                                      const GURL& initiator,
                                      ResourceType type,
                                      const RegexHits* regex) const {
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
  if ((regex && regex->important) ||
      AnyRuleMatches(important_buckets_, untokenized_important_, url_lower,
                     initiator_host, third_party, type)) {
    return true;
  }
  if (!(regex && regex->block) &&
      !AnyRuleMatches(block_buckets_, untokenized_block_, url_lower,
                      initiator_host, third_party, type)) {
    return false;
  }
  // A matching allowlist (@@) rule un-blocks the request.
  if ((regex && regex->exception) ||
      AnyRuleMatches(exception_buckets_, untokenized_exception_, url_lower,
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

bool AdblockFilterEngine::AddTaggedRule(std::string_view line, uint32_t tag) {
  pending_tag_ = tag;
  const size_t added = AddRules(line);
  pending_tag_ = 0;
  return added > 0;
}

std::optional<uint16_t> AdblockFilterEngine::ResolveRedirect(
    const GURL& url,
    const GURL& initiator,
    ResourceType type) const {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
      (redirect_buckets_.empty() && untokenized_redirect_.empty())) {
    return std::nullopt;
  }
  std::string_view initiator_host;
  bool third_party = false;
  if (initiator.is_valid() && !initiator.host().empty()) {
    initiator_host = initiator.host();
    third_party = !net::registry_controlled_domains::SameDomainOrHost(
        url, initiator,
        net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  }
  const std::string url_lower = base::ToLowerASCII(url.spec());
  std::vector<RedirectCandidate> candidates;
  const auto add_candidate = [&](const FilterRule& rule) {
    candidates.push_back({rule.redirect_resource, rule.redirect_priority});
  };
  const base::FunctionRef<void(const FilterRule&)> add_candidate_ref(
      add_candidate);
  AnyRuleMatches(redirect_buckets_, untokenized_redirect_, url_lower,
                 initiator_host, third_party, type, nullptr,
                 &add_candidate_ref);
  if (candidates.empty()) {
    return std::nullopt;
  }
  std::vector<uint16_t> exceptions;
  const auto add_exception = [&](const FilterRule& rule) {
    exceptions.push_back(rule.redirect_any ? kNoRedirectResource
                                           : rule.redirect_resource);
  };
  const base::FunctionRef<void(const FilterRule&)> add_exception_ref(
      add_exception);
  AnyRuleMatches(redirect_exception_buckets_, untokenized_redirect_exception_,
                 url_lower, initiator_host, third_party, type, nullptr,
                 &add_exception_ref);
  return ChooseRedirect(candidates, exceptions, type);
}

void AdblockFilterEngine::ForEachMatchingRule(
    const GURL& url,
    const GURL& initiator,
    ResourceType type,
    base::FunctionRef<void(const FilterRule&)> visit) const {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return;
  }
  std::string_view initiator_host;
  bool third_party = false;
  if (initiator.is_valid() && !initiator.host().empty()) {
    initiator_host = initiator.host();
    third_party = !net::registry_controlled_domains::SameDomainOrHost(
        url, initiator,
        net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  }
  const std::string url_lower = base::ToLowerASCII(url.spec());
  AnyRuleMatches(block_buckets_, untokenized_block_, url_lower, initiator_host,
                 third_party, type, nullptr, &visit);
  AnyRuleMatches(exception_buckets_, untokenized_exception_, url_lower,
                 initiator_host, third_party, type, nullptr, &visit);
}

void AdblockFilterEngine::ForEachRule(
    base::FunctionRef<void(RuleGroup, const FilterRule&, std::string_view)>
        visit) const {
  auto walk = [&](RuleGroup group, const RuleBuckets& buckets,
                  const std::vector<FilterRule>& untokenized) {
    for (const auto& [keyword, rules] : buckets) {
      for (const FilterRule& rule : rules) {
        visit(group, rule, keyword);
      }
    }
    for (const FilterRule& rule : untokenized) {
      visit(group, rule, std::string_view());
    }
  };
  walk(RuleGroup::kBlock, block_buckets_, untokenized_block_);
  walk(RuleGroup::kException, exception_buckets_, untokenized_exception_);
  walk(RuleGroup::kImportant, important_buckets_, untokenized_important_);
  walk(RuleGroup::kDocument, document_buckets_, untokenized_document_);
  walk(RuleGroup::kRedirect, redirect_buckets_, untokenized_redirect_);
  walk(RuleGroup::kRedirectException, redirect_exception_buckets_,
       untokenized_redirect_exception_);
}

}  // namespace zephyrus_adblock
