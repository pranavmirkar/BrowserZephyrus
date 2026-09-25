// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_LIST_UTIL_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_LIST_UTIL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zephyrus_adblock {

// Applies uBlock Origin's `!#if <expr>` / `!#else` / `!#endif` directives,
// dropping every line in a branch that does not hold for Zephyrus.
//
// The uBO lists fence platform-specific rules this way: Firefox-only HTML
// filtering, mobile layouts, the MV3 "Lite" extension. Read as plain comments,
// the directives vanish and BOTH branches of every block get applied, so a
// Chromium page received the Firefox and mobile variants of a fix alongside
// its own. Unknown tokens evaluate false, as in uBO.
std::string PreprocessFilterList(std::string_view text);

// Evaluates one `!#if` expression (`env_chromium && !env_mobile`, with
// parentheses). Exposed for tests.
bool EvaluateListCondition(std::string_view expression);

// The names of the files a list pulls in with `!#include <name>`, in order.
// Only bare same-directory `.txt` names are returned: anything with a path
// separator, a scheme or `..` is dropped, so an include can never point the
// updater at another host.
std::vector<std::string> FindListIncludes(std::string_view text);

// Every key a per-domain rule for `host` can be filed under, most specific
// first: the host and each parent domain ("a.b.example.co.in",
// "b.example.co.in", ...) and then the entity forms uBO writes as `name.*`
// ("a.b.example.*", "b.example.*", "example.*"), which match a site under any
// public suffix. `host` must be lowercase.
std::vector<std::string> DomainLookupKeys(std::string_view host);

// Whether `host` is covered by a filter's `domain`: the domain itself, a
// subdomain of it, or -- for an entity `name.*` -- the same under any public
// suffix. Both must be lowercase.
bool HostMatchesFilterDomain(std::string_view host, std::string_view domain);

// ---- The combined list file the updater writes ----------------------------
//
// One file holding every upstream list, each under a marker line naming its
// URL, after a short header:
//
//   ! Zephyrus combined filter lists (auto-updated). Do not edit.
//   ! Zephyrus-Format: 2
//   ! Zephyrus-Full-Update: <unix seconds of the last full refresh>
//   ! ===== https://easylist.to/easylist/easylist.txt =====
//   ...

// Bumped whenever the updater learns to produce a materially different file
// (format 2: `!#include`s resolved). A file in an older format is refreshed at
// once instead of when it ages out, so an upgrade takes effect on first run.
inline constexpr int kCombinedListFormat = 2;

struct CombinedListHeader {
  int format = 0;                   // 0 for a file that predates the header
  int64_t full_update_seconds = 0;  // 0 when unknown
};

// Reads the header from the start of a combined file.
CombinedListHeader ParseCombinedListHeader(std::string_view head);

// The header for a file whose last full refresh was at `full_update_seconds`.
std::string CombinedListHeaderText(int64_t full_update_seconds);

// The marker line that opens `url`'s section, without its trailing newline.
std::string ListSectionMarker(std::string_view url);

// Whether a combined-list section marker line ("! ===== <label> =====")
// introduces a list allowed to use uBO's TRUSTED scriptlets (trusted-*): the
// ones that can rewrite fetch/XHR responses, set cookies and storage, and click
// elements on the page. Only uBlock Origin's own lists (uAssets) are, as in uBO
// itself; EasyList, Fanboy, IndianList and every other third-party list are
// not. Returns false for any line that is not a section marker.
bool IsTrustedScriptletSectionMarker(std::string_view line);

// `url`'s section of a combined file, or nullopt when it has none. Lets an
// update carry forward a list whose download failed instead of dropping it.
std::optional<std::string_view> FindListSection(std::string_view combined,
                                                std::string_view url);

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_LIST_UTIL_H_
