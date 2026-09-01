// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §9.7 requires every eTLD+1 edge case to have a defined behaviour AND a test.
// These are those tests. The behaviours they pin:
//
//   IP literal (v4/v6)  no owner, never folded
//   localhost           no owner, never folded
//   single label        no owner, never folded
//   internal/corporate  no owner unless the suffix is a real registry
//   trailing dot        normalised away before hashing
//   punycode / IDN      the runtime sees ASCII punycode; the converter must
//                       produce the same, which the Python selftest covers
//
// The reason each of these matters is the same: they all fail SILENTLY. A
// mismatch does not crash, it just misses, and a privacy feature that misses
// reports a clean page.

#include "chrome/browser/zephyrus/privacy/privacy_host_canon.h"

#include <string_view>

#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

TEST(PrivacyHostCanonTest, StripsTrailingDots) {
  EXPECT_EQ("example.com", CanonicalHostForHash("example.com."));
  EXPECT_EQ("example.com", CanonicalHostForHash("example.com.."));
  EXPECT_EQ("example.com", CanonicalHostForHash("example.com"));
  // A lone dot is not a host; leaving it alone is fine, it can never resolve.
  EXPECT_EQ(".", CanonicalHostForHash("."));
  EXPECT_EQ("", CanonicalHostForHash(""));
}

// GURL::host() is already lowercased and already punycode, so this must not
// spend an allocation redoing either on the request path.
TEST(PrivacyHostCanonTest, LeavesAlreadyCanonicalHostsUntouched) {
  EXPECT_EQ("ads.example.com", CanonicalHostForHash("ads.example.com"));
  EXPECT_EQ("xn--mnchen-3ya.de", CanonicalHostForHash("xn--mnchen-3ya.de"));
}

TEST(PrivacyHostCanonTest, IpLiteralsHaveNoOwner) {
  EXPECT_FALSE(HostCanHaveOwner("127.0.0.1"));
  EXPECT_FALSE(HostCanHaveOwner("8.8.8.8"));
  EXPECT_FALSE(HostCanHaveOwner("192.168.1.1"));
  // Bracketed IPv6, the form GURL::host() returns.
  EXPECT_FALSE(HostCanHaveOwner("[::1]"));
  EXPECT_FALSE(HostCanHaveOwner("[2001:db8::1]"));
}

TEST(PrivacyHostCanonTest, LocalhostAndSingleLabelsHaveNoOwner) {
  EXPECT_FALSE(HostCanHaveOwner("localhost"));
  EXPECT_FALSE(HostCanHaveOwner("intranet"));
  EXPECT_FALSE(HostCanHaveOwner("build-server"));
  EXPECT_FALSE(HostCanHaveOwner(""));
  // Trailing dot does not turn a single label into two.
  EXPECT_FALSE(HostCanHaveOwner("localhost."));
}

// A multi-label name CAN have an owner as far as this function is concerned;
// whether it actually resolves is the registry's business, and
// GetDomainAndRegistry returning empty is what stops "server.corp" being
// folded to "server.corp".
TEST(PrivacyHostCanonTest, MultiLabelHostsAreEligible) {
  EXPECT_TRUE(HostCanHaveOwner("doubleclick.net"));
  EXPECT_TRUE(HostCanHaveOwner("securepubads.g.doubleclick.net"));
  EXPECT_TRUE(HostCanHaveOwner("xn--mnchen-3ya.de"));
  EXPECT_TRUE(HostCanHaveOwner("example.com."));
  // Eligible here, but has no public registry suffix, so the fold will find no
  // registrable domain and attribute nothing. That is the §9.7 behaviour for
  // internal corporate names: not a guess, and not a crash.
  EXPECT_TRUE(HostCanHaveOwner("server.corp"));
}

// The specific trap: "127.0.0.1" contains dots, so a label-count check alone
// would treat "0.1" as its registrable domain.
TEST(PrivacyHostCanonTest, DottedIpIsNotMistakenForARegistrableDomain) {
  EXPECT_FALSE(HostCanHaveOwner("127.0.0.1"));
  EXPECT_FALSE(HostCanHaveOwner("10.0.0.255"));
  // Not an IP: leading digits are legal in hostnames.
  EXPECT_TRUE(HostCanHaveOwner("1.example.com"));
  EXPECT_TRUE(HostCanHaveOwner("123.net"));
}

}  // namespace
// -- IsSameSiteHost ----------------------------------------------------------
//
// The rule that keeps a page's OWN requests out of its privacy report. It is
// tested here rather than at either call site because the two recording paths
// (blocked, on the network thread; completed, on the UI thread) must agree,
// and the way this breaks is one of them quietly not applying it.

TEST(PrivacyHostCanonTest, SameSiteHostMatchesTheDomainItself) {
  EXPECT_TRUE(IsSameSiteHost("example.com", "example.com"));
}

TEST(PrivacyHostCanonTest, SameSiteHostMatchesASubdomain) {
  EXPECT_TRUE(IsSameSiteHost("analytics.example.com", "example.com"));
  EXPECT_TRUE(IsSameSiteHost("a.b.c.example.com", "example.com"));
}

// The reason this is not a bare ends_with: a different company can register a
// name that merely ENDS in the page's domain, and counting it as first-party
// would hide a genuine third-party tracker.
TEST(PrivacyHostCanonTest, SameSiteHostRejectsASuffixThatIsNotALabel) {
  EXPECT_FALSE(IsSameSiteHost("notexample.com", "example.com"));
  EXPECT_FALSE(IsSameSiteHost("evil-example.com", "example.com"));
}

TEST(PrivacyHostCanonTest, SameSiteHostRejectsADifferentSite) {
  EXPECT_FALSE(IsSameSiteHost("tracker.net", "example.com"));
  // Same brand, different registrable domain, and therefore third-party: this
  // is a real case (a .com property loading a subdomain of its .in property),
  // and it is NOT first-party however much it looks like one.
  EXPECT_FALSE(IsSameSiteHost("analytics.nike.com", "nike.in"));
}

// §9.4: an IP literal, localhost or about: page has no registrable domain, so
// there is nothing for a host to belong to. Returning true would silently
// suppress every request on such a page.
TEST(PrivacyHostCanonTest, SameSiteHostWithNoSiteIsNotFirstParty) {
  EXPECT_FALSE(IsSameSiteHost("analytics.example.com", ""));
  EXPECT_FALSE(IsSameSiteHost("", "example.com"));
  EXPECT_FALSE(IsSameSiteHost("", ""));
}

// A shorter host can never sit beneath a longer domain; guards the index
// arithmetic against underflow.
TEST(PrivacyHostCanonTest, SameSiteHostShorterThanTheSiteIsNotFirstParty) {
  EXPECT_FALSE(IsSameSiteHost("com", "example.com"));
  EXPECT_FALSE(IsSameSiteHost("e.com", "example.com"));
}

}  // namespace zephyrus_privacy
