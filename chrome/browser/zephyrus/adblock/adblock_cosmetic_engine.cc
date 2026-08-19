// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/adblock_cosmetic_engine.h"

#include <optional>

#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace zephyrus_adblock {

namespace {

// Procedural / extended-syntax markers that require JS rather than plain CSS;
// selectors containing any of these are skipped for now. NOTE: plain ":has()"
// is NOT here — Chromium supports native CSS :has() (M105+), so those rules are
// emitted as real CSS (this is what hides whole ad containers, e.g.
// ytd-rich-item-renderer:has(ytd-ad-slot-renderer)). Only the truly procedural
// ":has-text()" variant is skipped.
bool IsProceduralSelector(std::string_view selector) {
  static constexpr std::string_view kMarkers[] = {
      ":has-text(",     ":-abp-",      ":xpath(",
      ":remove(",       ":upward(",    ":matches-css",
      ":matches-path(",":matches-media(",":min-text-length(",
      ":watch-attr(",  ":contains(",     ":if(",        ":if-not(",
      ":nth-ancestor(",":others(",       ":shadow(",    ":remove-attr(",
      ":remove-class(",
  };
  for (std::string_view marker : kMarkers) {
    if (selector.find(marker) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

// The first "#id" or ".class" in `selector`, which any element matching it
// must carry. Returns nullopt when there is none to key on.
//
// A '#' or '.' inside a string or attribute value (`a[href="#top"]`) can be
// picked up as a token. That only ever makes a selector wait for a hook the
// page may not have, so the failure mode is a rule staying dormant — never a
// rule firing on a page it was not written for.
std::optional<std::string> KeyTokenFor(std::string_view selector) {
  for (size_t i = 0; i + 1 < selector.size(); ++i) {
    if (selector[i] != '#' && selector[i] != '.') {
      continue;
    }
    // CSS identifiers cannot start with a digit.
    const char first = selector[i + 1];
    if (!base::IsAsciiAlpha(first) && first != '_' && first != '-') {
      continue;
    }
    size_t j = i + 1;
    while (j < selector.size() &&
           (base::IsAsciiAlphaNumeric(selector[j]) || selector[j] == '-' ||
            selector[j] == '_')) {
      ++j;
    }
    return std::string(selector.substr(i, j - i));
  }
  return std::nullopt;
}

// Splits "selector:style(decls)" into a CSS rule. Returns nullopt when the
// filter is not a style filter.
std::optional<std::string> AsStyleRule(std::string_view selector) {
  constexpr std::string_view kMarker = ":style(";
  const size_t at = selector.rfind(kMarker);
  if (at == std::string_view::npos || selector.back() != ')') {
    return std::nullopt;
  }
  std::string_view target = selector.substr(0, at);
  std::string_view decls = selector.substr(
      at + kMarker.size(), selector.size() - at - kMarker.size() - 1);
  if (target.empty() || decls.empty()) {
    return std::nullopt;
  }
  return base::StrCat({target, "{", decls, "}"});
}

}  // namespace

AdblockCosmeticEngine::AdblockCosmeticEngine() = default;
AdblockCosmeticEngine::~AdblockCosmeticEngine() = default;

size_t AdblockCosmeticEngine::AddRules(std::string_view filter_list_text,
                                       bool inject_generic_selectors) {
  size_t added = 0;
  for (std::string_view raw :
       base::SplitStringPiece(filter_list_text, "\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (raw[0] == '!' || raw[0] == '[') {
      continue;
    }

    // Locate the cosmetic separator: "##" (hide) or "#@#" (unhide). Skip the
    // extended variants "#?#", "#$#", "#%#" (procedural / scriptlet / style).
    bool exception = false;
    size_t sep = std::string_view::npos;
    size_t sep_len = 0;
    if (size_t p = raw.find("#@#"); p != std::string_view::npos) {
      exception = true;
      sep = p;
      sep_len = 3;
    } else if (size_t q = raw.find("##"); q != std::string_view::npos) {
      // Reject "#?#" / "#$#" which also contain "##"? They don't; they use a
      // single '#'. But ensure we didn't land inside "#?#"-style tokens.
      sep = q;
      sep_len = 2;
    } else {
      continue;  // not a cosmetic rule
    }

    std::string_view domains = raw.substr(0, sep);
    std::string_view selector = raw.substr(sep + sep_len);
    // Skip scriptlet injections ("##+js(...)") — those belong to the scriptlet
    // engine, and as CSS they are invalid and would (grouped) invalidate the
    // whole element-hiding stylesheet. Also skip empty/procedural selectors.
    if (selector.empty() || selector.rfind("+js(", 0) == 0 ||
        IsProceduralSelector(selector)) {
      continue;
    }
    std::string sel(selector);

    // A `:style()` filter paints rather than hides, so it leaves the hide
    // pipeline here with its declarations already folded into a CSS rule.
    std::optional<std::string> style_rule = AsStyleRule(sel);
    if (style_rule && exception) {
      continue;  // Unhiding a style override is meaningless.
    }

    if (domains.empty()) {
      // Generic rule.
      if (style_rule) {
        if (!inject_generic_selectors) {
          continue;  // See injected_style_rules_ in the header.
        }
        injected_style_rules_.push_back(*std::move(style_rule));
      } else if (exception) {
        generic_exceptions_.insert(sel);
      } else if (inject_generic_selectors) {
        injected_generic_selectors_.push_back(sel);
      } else {
        generic_selectors_.push_back(sel);
        if (std::optional<std::string> token = KeyTokenFor(sel)) {
          generic_by_token_[*token].push_back(sel);
          ++indexed_generic_count_;
        }
      }
      ++rule_count_;
      ++added;
      continue;
    }

    // Domain-scoped rule (comma-separated list; "~domain" negations skipped).
    for (std::string_view d :
         base::SplitStringPiece(domains, ",", base::TRIM_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY)) {
      if (d[0] == '~') {
        continue;  // exclusion form not supported yet
      }
      std::string domain = base::ToLowerASCII(d);
      if (style_rule) {
        domain_style_rules_[domain].push_back(*style_rule);
      } else if (exception) {
        domain_exceptions_[domain].insert(sel);
      } else {
        domain_selectors_[domain].push_back(sel);
      }
    }
    ++rule_count_;
    ++added;
  }
  return added;
}

std::unordered_set<std::string> AdblockCosmeticEngine::CollectExceptions(
    const std::string& host) const {
  std::unordered_set<std::string> exceptions = generic_exceptions_;
  // Walk host and each parent domain (a.b.c.com -> b.c.com -> c.com).
  for (size_t pos = 0; pos != std::string::npos;) {
    std::string_view candidate(host);
    candidate.remove_prefix(pos);
    auto ex_it = domain_exceptions_.find(std::string(candidate));
    if (ex_it != domain_exceptions_.end()) {
      exceptions.insert(ex_it->second.begin(), ex_it->second.end());
    }
    size_t dot = host.find('.', pos);
    pos = (dot == std::string::npos) ? std::string::npos : dot + 1;
  }
  return exceptions;
}

std::vector<std::string> AdblockCosmeticEngine::GetSelectorsForUrl(
    const GURL& url) const {
  std::vector<std::string> result;
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return result;
  }
  const std::string host(url.host());
  const std::unordered_set<std::string> exceptions = CollectExceptions(host);

  auto add = [&](const std::vector<std::string>& selectors) {
    for (const std::string& s : selectors) {
      if (!exceptions.contains(s)) {
        result.push_back(s);
      }
    }
  };

  // The small curated always-inject set (cookie-banner CMP containers) applies
  // to every page.
  add(injected_generic_selectors_);

  // Mass generic (all-site) selectors are deliberately absent here: they are
  // delivered by GetGenericSelectorsForTokens() once the renderer has surveyed
  // the document, so a page only pays for the ones it can actually match.
  for (size_t pos = 0; pos != std::string::npos;) {
    std::string_view candidate(host);
    candidate.remove_prefix(pos);
    auto it = domain_selectors_.find(std::string(candidate));
    if (it != domain_selectors_.end()) {
      add(it->second);
    }
    size_t dot = host.find('.', pos);
    pos = (dot == std::string::npos) ? std::string::npos : dot + 1;
  }
  return result;
}

std::vector<std::string> AdblockCosmeticEngine::GetGenericSelectorsForTokens(
    const GURL& url,
    const std::vector<std::string>& tokens) const {
  std::vector<std::string> result;
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return result;
  }
  const std::unordered_set<std::string> exceptions =
      CollectExceptions(std::string(url.host()));

  // One selector can be reachable from several of a page's tokens, and the
  // renderer re-surveys as the page mutates, so dedupe within the batch.
  std::unordered_set<std::string> seen;
  for (const std::string& token : tokens) {
    const auto it = generic_by_token_.find(token);
    if (it == generic_by_token_.end()) {
      continue;
    }
    for (const std::string& selector : it->second) {
      if (!exceptions.contains(selector) && seen.insert(selector).second) {
        result.push_back(selector);
      }
    }
  }
  return result;
}

std::vector<std::string> AdblockCosmeticEngine::GetStyleRulesForUrl(
    const GURL& url) const {
  std::vector<std::string> result;
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return result;
  }
  result = injected_style_rules_;
  const std::string host(url.host());
  for (size_t pos = 0; pos != std::string::npos;) {
    std::string_view candidate(host);
    candidate.remove_prefix(pos);
    const auto it = domain_style_rules_.find(std::string(candidate));
    if (it != domain_style_rules_.end()) {
      result.insert(result.end(), it->second.begin(), it->second.end());
    }
    size_t dot = host.find('.', pos);
    pos = (dot == std::string::npos) ? std::string::npos : dot + 1;
  }
  return result;
}

}  // namespace zephyrus_adblock
