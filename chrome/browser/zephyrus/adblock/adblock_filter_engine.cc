// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"

#include <algorithm>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
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

// Longest run of [a-z0-9] of length >= 3 in `pattern` (already lowercased),
// used as the rule's hash-bucket key. Empty if none.
std::string FindKeyword(const std::string& pattern) {
  std::string best;
  std::string current;
  for (char c : pattern) {
    if (base::IsAsciiAlphaNumeric(c)) {
      current.push_back(c);
    } else {
      if (current.size() > best.size()) {
        best = current;
      }
      current.clear();
    }
  }
  if (current.size() > best.size()) {
    best = current;
  }
  return best.size() >= 3 ? best : std::string();
}

// All [a-z0-9] runs of length >= 3 in `url_lower`.
std::vector<std::string> ExtractTokens(const std::string& url_lower) {
  std::vector<std::string> tokens;
  std::string current;
  for (char c : url_lower) {
    if (base::IsAsciiAlphaNumeric(c)) {
      current.push_back(c);
    } else {
      if (current.size() >= 3) {
        tokens.push_back(current);
      }
      current.clear();
    }
  }
  if (current.size() >= 3) {
    tokens.push_back(current);
  }
  return tokens;
}

// True if `host` is `domain` or a subdomain of it.
bool HostMatchesDomain(std::string_view host, const std::string& domain) {
  if (host == domain) {
    return true;
  }
  return host.size() > domain.size() &&
         base::EndsWith(host, "." + domain) &&
         host[host.size() - domain.size() - 1] == '.';
}

}  // namespace

AdblockFilterEngine::AdblockFilterEngine() = default;
AdblockFilterEngine::~AdblockFilterEngine() = default;

size_t AdblockFilterEngine::AddRules(std::string_view filter_list_text) {
  size_t added = 0;
  for (std::string_view raw :
       base::SplitStringPiece(filter_list_text, "\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    std::string line(raw);
    // Comments and list headers.
    if (line[0] == '!' || line[0] == '[') {
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
        } else if (opt == "popup") {
          set_type(kTypePopup);
        } else if (opt == "other") {
          set_type(kTypeOther);
        } else if (opt == "important") {
          rule.important = true;
        } else if (base::StartsWith(opt, "domain=")) {
          for (std::string_view d : base::SplitStringPiece(
                   opt.substr(7), "|", base::TRIM_WHITESPACE,
                   base::SPLIT_WANT_NONEMPTY)) {
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
  std::string keyword = FindKeyword(rule.pattern);
  auto& buckets = exception ? exception_buckets_ : block_buckets_;
  auto& untokenized = exception ? untokenized_exception_ : untokenized_block_;
  if (keyword.empty()) {
    untokenized.push_back(std::move(rule));
  } else {
    buckets[keyword].push_back(std::move(rule));
  }
}

bool AdblockFilterEngine::AnyRuleMatches(
    const std::unordered_map<std::string, std::vector<FilterRule>>& buckets,
    const std::vector<FilterRule>& untokenized,
    const std::string& url_lower,
    std::string_view initiator_host,
    bool third_party,
    ResourceType type) const {
  // Host boundaries within `url_lower`, for "||" hostname-anchored matching.
  size_t host_start = url_lower.find("://");
  host_start = (host_start == std::string::npos) ? 0 : host_start + 3;
  size_t host_end = url_lower.find_first_of("/:?", host_start);
  if (host_end == std::string::npos) {
    host_end = url_lower.size();
  }

  auto rule_matches = [&](const FilterRule& rule) -> bool {
    // Resource-type scoping.
    if (rule.type_mask != kTypeAll && !(rule.type_mask & type)) {
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
    // Pattern match. A generous per-rule step budget bounds worst-case work.
    constexpr int kMatchBudget = 100000;
    switch (rule.anchor) {
      case FilterRule::kAnchorStart: {
        int budget = kMatchBudget;
        return MatchAt(rule.pattern, 0, url_lower, 0, rule.anchor_end, budget);
      }
      case FilterRule::kAnchorHostname: {
        // Match at the host start or any label boundary within the host.
        for (size_t pos = host_start; pos < host_end; ++pos) {
          if (pos == host_start || url_lower[pos - 1] == '.') {
            int budget = kMatchBudget;
            if (MatchAt(rule.pattern, 0, url_lower, pos, rule.anchor_end,
                        budget)) {
              return true;
            }
          }
        }
        return false;
      }
      case FilterRule::kAnchorNone:
      default:
        for (size_t pos = 0; pos <= url_lower.size(); ++pos) {
          int budget = kMatchBudget;
          if (MatchAt(rule.pattern, 0, url_lower, pos, rule.anchor_end,
                      budget)) {
            return true;
          }
        }
        return false;
    }
  };

  for (const std::string& token : ExtractTokens(url_lower)) {
    auto it = buckets.find(token);
    if (it == buckets.end()) {
      continue;
    }
    for (const FilterRule& rule : it->second) {
      if (rule_matches(rule)) {
        return true;
      }
    }
  }
  for (const FilterRule& rule : untokenized) {
    if (rule_matches(rule)) {
      return true;
    }
  }
  return false;
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

  if (!AnyRuleMatches(block_buckets_, untokenized_block_, url_lower,
                      initiator_host, third_party, type)) {
    return false;
  }
  // A matching allowlist (@@) rule un-blocks the request.
  if (AnyRuleMatches(exception_buckets_, untokenized_exception_, url_lower,
                     initiator_host, third_party, type)) {
    return false;
  }
  return true;
}

}  // namespace zephyrus_adblock
