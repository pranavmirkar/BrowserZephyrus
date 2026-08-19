// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_HOST_CANON_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_HOST_CANON_H_

#include <stdint.h>

#include <string>
#include <string_view>

namespace zephyrus_privacy {

// §9.7 eTLD+1 edge cases, in one place.
//
// **Why this exists.** A domain is hashed in two independent implementations —
// the Python converter that builds the artifact, and the C++ that looks up a
// request — and the two must agree on the exact bytes. They already agree on
// the hash function (see entity_hash_agreement_unittest.cc). What they also
// have to agree on is what gets fed INTO it, and that is a normalisation
// question: `Example.COM.` and `example.com` are the same host, and
// `münchen.de` and `xn--mnchen-3ya.de` are the same host.
//
// When they disagree, nothing breaks loudly. Every lookup simply misses, the
// page looks clean, and a privacy feature quietly reports "no trackers" on a
// tracked page — a §2 accuracy violation rather than a bug report.

// Returns `host` in the form the artifact is keyed by.
//
// Strips a single trailing dot: `example.com.` is a legal fully-qualified name
// and resolves to the same site as `example.com`, but hashes differently.
//
// Deliberately does NOT lowercase or punycode-encode. GURL::host() is already
// lowercased and already punycode for IDN, so doing it again would allocate on
// the request path to produce the identical string. The converter, whose input
// is arbitrary text out of a JSON file, does have to do both — see
// tools/build_entity_artifact.py, which is the other half of this contract.
std::string_view CanonicalHostForHash(std::string_view host);

// Whether a host is the sort of name that can have a registrable domain and
// therefore an owner. False for IP literals (v4 and v6), `localhost`, and any
// single-label name.
//
// Callers use this to decide whether a miss is INTERESTING. A tracker domain
// that fails to resolve may mean a stale dataset; an IP literal that fails to
// resolve is simply a host nobody can own, and §4.2's "never guess an owner"
// makes not attributing it the correct answer rather than a gap.
bool HostCanHaveOwner(std::string_view host);

// The in-memory site key for a registrable domain.
//
// **Why this is a function and not two lines at each call site.** The emission
// point derives this id on the network thread when it records an event; the
// popup has to derive the SAME id from the URL of the tab it is describing, on
// the UI thread, with no shared state between them. If the two ever compute it
// differently the popup silently shows an empty page analysis — no crash, no
// log, just a privacy feature reporting nothing about a page full of trackers.
// That is the same failure mode CanonicalHostForHash() above exists to prevent,
// so the two live together.
//
// Returns 0 for an empty domain — the synthetic "no site" id, which §9.4
// requires be excluded from per-page views rather than guessed at. A real
// domain never returns 0: a hash that lands there is remapped, so the sentinel
// cannot collide with a genuine site.
uint32_t SiteIdForEtld1(std::string_view etld1);

// The display name the aggregator writes for site id 0, the synthetic "no
// site" (§9.4: IP literal, localhost, about:, single-label intranet name).
//
// It is a PLACEHOLDER, not a site, and every view that counts or names sites
// has to exclude it. Defined here, beside SiteIdForEtld1(), because the writer
// and the readers are in different translation units and a second copy of the
// literal is how they would drift apart.
inline constexpr char kNoSiteName[] = "(no site)";

// Whether `host` belongs to the page whose registrable domain is
// `site_etld1` — i.e. whether a request to it is FIRST-PARTY.
//
// **Why a suffix test rather than GetDomainAndRegistry(host).** The blocked
// path calls this on the network thread, once per intercepted request, under
// §8.1's "zero allocations on the request path". `GetDomainAndRegistry`
// returns a std::string, so using it here would allocate for every request.
// Since `site_etld1` is ALREADY a registrable domain, a host has that same
// registrable domain exactly when it equals it or sits directly beneath it —
// so the cheap test and the expensive one agree.
//
// **Why it exists at all.** A blocked request and a completed request are
// recorded by two different classes on two different threads (see
// PrivacyTabHelper and ZephyrusAdblockProxyingURLLoaderFactory). The
// first-party rule lived only in the completed path, which meant the same
// request was counted when blocked and ignored when allowed. Naming the rule
// once makes that divergence harder to reintroduce.
//
// An empty `site_etld1` means there is no site to compare against (§9.4: IP
// literal, localhost, about:) and returns false — "not first-party" — because
// nothing can be shown to belong to a page we cannot identify.
bool IsSameSiteHost(std::string_view host, std::string_view site_etld1);

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_HOST_CANON_H_
