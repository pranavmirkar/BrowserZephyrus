// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/adblock_cosmetic_engine.h"

#include <optional>

#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"

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

// Whether a filter's CSS can be emitted without escaping its rule.
//
// A filter list is data from the network -- it auto-updates -- and a cosmetic
// filter is pasted into a style sheet as text. So a selector that closes the
// rule it sits in (`x{} *{...}`) writes arbitrary CSS: a `url()` that tells a
// third party which pages you read, or a rule that hides every element. A
// `/*` swallows the rest of the sheet, and with it every rule after it. The
// hiding sheet is injected at USER origin, where the page cannot override it,
// which is exactly why what goes into it has to be kept to what a filter is
// for.
//
// `declarations` is the body of a `:style()` filter, where the same rules
// apply and loading anything is refused outright: no `url(`, `image-set(` or
// `@`, and no backslash, since a CSS escape can spell `url(` without the
// letters (`u\72l(`).
bool IsSafeFilterCss(std::string_view css, bool declarations) {
  if (css.find_first_of("{}") != std::string_view::npos ||
      css.find("/*") != std::string_view::npos ||
      css.find('<') != std::string_view::npos) {
    return false;
  }
  // Brackets and quotes must balance. An unclosed `(` or string does not end
  // at the rule's `{`: the CSS tokenizer carries it on through the following
  // rules, so one bad filter would void every hide rule injected after it.
  int parens = 0;
  int brackets = 0;
  char quote = 0;
  for (char c : css) {
    if (quote) {
      if (c == quote) {
        quote = 0;
      }
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '(') {
      ++parens;
    } else if (c == ')' && --parens < 0) {
      return false;
    } else if (c == '[') {
      ++brackets;
    } else if (c == ']' && --brackets < 0) {
      return false;
    }
  }
  if (quote || parens || brackets) {
    return false;
  }
  if (!declarations) {
    return true;
  }
  if (css.find('\\') != std::string_view::npos ||
      css.find('@') != std::string_view::npos) {
    return false;
  }
  const std::string lowered = base::ToLowerASCII(css);
  for (std::string_view banned : {"url(", "image-set(", "image(",
                                  "expression(", "src("}) {
    if (lowered.find(banned) != std::string::npos) {
      return false;
    }
  }
  return true;
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
  if (target.empty() || decls.empty() || !IsSafeFilterCss(target, false) ||
      !IsSafeFilterCss(decls, true)) {
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
    // AsStyleRule vets both halves; nullopt from an unsafe one drops it below.
    std::optional<std::string> style_rule = AsStyleRule(sel);
    if (style_rule && exception) {
      continue;  // Unhiding a style override is meaningless.
    }
    const bool looks_like_style =
        sel.find(":style(") != std::string::npos;
    if (!style_rule && (looks_like_style || !IsSafeFilterCss(sel, false))) {
      continue;  // See IsSafeFilterCss.
    }

    // "a.com,~b.a.com" -- the sites a rule is for, and the ones carved out.
    // Entity names ("google.*") are stored as written; DomainLookupKeys
    // produces the same form for a page's host.
    std::vector<std::string> included;
    std::vector<std::string> excluded;
    for (std::string_view d :
         base::SplitStringPiece(domains, ",", base::TRIM_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY)) {
      if (d[0] == '~') {
        if (d.size() > 1) {
          excluded.push_back(base::ToLowerASCII(d.substr(1)));
        }
      } else {
        included.push_back(base::ToLowerASCII(d));
      }
    }

    if (included.empty()) {
      // Generic rule -- or one with only exclusions ("~a.com##.ad"), which is
      // generic everywhere but there. Those used to be dropped outright.
      if (style_rule) {
        if (!inject_generic_selectors || !excluded.empty()) {
          continue;  // See injected_style_rules_ in the header.
        }
        injected_style_rules_.push_back(*std::move(style_rule));
      } else if (exception) {
        if (!excluded.empty()) {
          continue;  // "Unhide everywhere except" has no use; not worth a path.
        }
        generic_exceptions_.insert(sel);
      } else if (inject_generic_selectors) {
        injected_generic_selectors_.push_back(sel);
      } else {
        generic_selectors_.push_back(sel);
        if (std::optional<std::string> token = KeyTokenFor(sel)) {
          generic_by_token_[*token].push_back(sel);
          ++indexed_generic_count_;
        } else {
          keyless_generic_selectors_.push_back(sel);
        }
      }
      if (!style_rule && !exception) {
        for (const std::string& domain : excluded) {
          domain_exceptions_[domain].insert(sel);
        }
      }
      ++rule_count_;
      ++added;
      continue;
    }

    for (const std::string& domain : included) {
      if (style_rule) {
        domain_style_rules_[domain].push_back(*style_rule);
      } else if (exception) {
        domain_exceptions_[domain].insert(sel);
      } else {
        domain_selectors_[domain].push_back(sel);
      }
    }
    // A carve-out ("a.com,~shop.a.com##.ad") is an unhide on the carved-out
    // host. It also unhides the same selector from any OTHER rule there,
    // which is the rare over-reach of storing exceptions by selector text;
    // ignoring the carve-out instead hid content on a site the list author
    // explicitly exempted.
    if (!style_rule && !exception) {
      for (const std::string& domain : excluded) {
        domain_exceptions_[domain].insert(sel);
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
  for (const std::string& key : DomainLookupKeys(host)) {
    auto ex_it = domain_exceptions_.find(key);
    if (ex_it != domain_exceptions_.end()) {
      exceptions.insert(ex_it->second.begin(), ex_it->second.end());
    }
  }
  return exceptions;
}

std::vector<std::string> AdblockCosmeticEngine::GetSelectorsForUrl(
    const GURL& url,
    bool include_generic,
    bool include_specific) const {
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

  if (include_generic) {
    // The small curated always-inject set (cookie-banner CMP containers)
    // applies to every page.
    add(injected_generic_selectors_);
    // Keyless generic selectors have no id or class for the survey to find,
    // so this is their only way onto a page. There are ~120 of them, which is
    // cheap next to what a single news site's own list carries.
    add(keyless_generic_selectors_);
  }

  // The keyed mass generic selectors are deliberately absent here: they are
  // delivered by GetGenericSelectorsForTokens() once the renderer has surveyed
  // the document, so a page only pays for the ones it can actually match.
  if (include_specific) {
    for (const std::string& key : DomainLookupKeys(host)) {
      auto it = domain_selectors_.find(key);
      if (it != domain_selectors_.end()) {
        add(it->second);
      }
    }
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
    const GURL& url,
    bool include_generic,
    bool include_specific) const {
  std::vector<std::string> result;
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return result;
  }
  if (include_generic) {
    result = injected_style_rules_;
  }
  if (include_specific) {
    for (const std::string& key : DomainLookupKeys(std::string(url.host()))) {
      const auto it = domain_style_rules_.find(key);
      if (it != domain_style_rules_.end()) {
        result.insert(result.end(), it->second.begin(), it->second.end());
      }
    }
  }
  return result;
}

}  // namespace zephyrus_adblock
