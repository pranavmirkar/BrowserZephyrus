// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_host_canon.h"

#include <string>

#include "base/hash/hash.h"
#include "net/base/ip_address.h"

namespace zephyrus_privacy {

uint32_t SiteIdForEtld1(std::string_view etld1) {
  if (etld1.empty()) {
    return 0;
  }
  const uint32_t hash = base::PersistentHash(CanonicalHostForHash(etld1));
  // 0 is the synthetic "no site" id. A real domain that hashes to it is nudged
  // to 1 rather than left to masquerade as "no site", which would silently
  // merge that one unlucky domain's events into the excluded bucket.
  return hash == 0 ? 1u : hash;
}

std::string_view CanonicalHostForHash(std::string_view host) {
  // ALL trailing dots, not just one. GURL never produces "a.b..", so in the
  // browser this only ever strips the single legal FQDN dot — but the Python
  // converter applies the same rule to arbitrary text out of a dataset, and
  // the two sides must reduce identical names to identical bytes. A rule that
  // differs in a case neither side can currently reach is still a rule that
  // will differ once one of them can.
  while (host.size() > 1 && host.back() == '.') {
    host.remove_suffix(1);
  }
  return host;
}

bool HostCanHaveOwner(std::string_view host) {
  const std::string_view canonical = CanonicalHostForHash(host);
  if (canonical.empty()) {
    return false;
  }

  // A bracketed literal is IPv6 by construction; an unbracketed one is caught
  // by the IPv4 check below.
  if (canonical.front() == '[') {
    return false;
  }

  // Single-label: "localhost", an intranet short name, a machine name. No
  // registrable domain exists, so no owner can be inferred without guessing.
  if (canonical.find('.') == std::string_view::npos) {
    return false;
  }

  // IPv4 literal. Checked AFTER the dot test on purpose: "127.0.0.1" does
  // contain dots, so the label test alone would let it through and the last two
  // labels would be mistaken for a registrable domain.
  net::IPAddress address;
  if (address.AssignFromIPLiteral(canonical)) {
    return false;
  }
  return true;
}

bool IsSameSiteHost(std::string_view host, std::string_view site_etld1) {
  if (host.empty() || site_etld1.empty()) {
    return false;
  }
  if (host == site_etld1) {
    return true;
  }
  // Strictly beneath it: the character before the suffix must be the label
  // separator, or "notexample.com" would count as part of "example.com".
  return host.size() > site_etld1.size() && host.ends_with(site_etld1) &&
         host[host.size() - site_etld1.size() - 1] == '.';
}

}  // namespace zephyrus_privacy
