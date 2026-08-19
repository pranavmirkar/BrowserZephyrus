// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The filter lists are fetched over HTTPS from third parties who publish no
// signatures, so TLS is the only integrity guarantee — and TLS attests to WHO
// served the bytes, never to WHAT they are. Before this check existed, any
// response over the 1 KB floor was adopted verbatim as filter rules.
//
// These tests pin the shapes that must be refused. They cannot cover a hostile
// but well-formed list; only a signed project-hosted mirror can.

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_updater.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_adblock {
namespace {

// The real openings of the lists actually fetched.
TEST(ZephyrusAdblockUpdaterTest, AcceptsRealFilterListHeaders) {
  EXPECT_TRUE(LooksLikeFilterList(
      "[Adblock Plus 2.0]\n! Title: EasyList\n||ads.example^\n"));
  EXPECT_TRUE(LooksLikeFilterList(
      "! Title: uBlock filters - Ads\n! Expires: 1d\n##.ad-banner\n"));
  // Leading blank lines and CRLF are normal from a web server.
  EXPECT_TRUE(LooksLikeFilterList("\r\n\r\n[Adblock Plus 2.0]\r\n||x^\r\n"));
  // A UTF-8 BOM is legal and must not cause a rejection.
  EXPECT_TRUE(LooksLikeFilterList("\xEF\xBB\xBF[Adblock Plus 2.0]\n||x^\n"));
}

// The case this exists for: the host served something, it was big, and it was
// not a filter list.
TEST(ZephyrusAdblockUpdaterTest, RejectsHtmlErrorPages) {
  EXPECT_FALSE(LooksLikeFilterList(
      "<!DOCTYPE html>\n<html><body>404 Not Found</body></html>"));
  EXPECT_FALSE(LooksLikeFilterList(
      "<html><head><title>Sign in</title></head><body>portal</body></html>"));
  // A captive portal that opens with something comment-shaped, then serves
  // markup. The prefix check alone would pass this.
  EXPECT_FALSE(LooksLikeFilterList(
      "! redirecting\n<!DOCTYPE html><html><body>login</body></html>"));
}

TEST(ZephyrusAdblockUpdaterTest, RejectsNonListContent) {
  EXPECT_FALSE(LooksLikeFilterList(""));
  EXPECT_FALSE(LooksLikeFilterList("   \n\t\r\n  "));
  EXPECT_FALSE(LooksLikeFilterList("{\"error\": \"rate limited\"}"));
  EXPECT_FALSE(LooksLikeFilterList("Not Found"));
  // Rules without the header line: every list we fetch has one, and accepting
  // headerless content would readmit most of what this check rejects.
  EXPECT_FALSE(LooksLikeFilterList("||ads.example^\n##.banner\n"));
  // Binary junk.
  EXPECT_FALSE(LooksLikeFilterList(std::string("\x00\x01\x02\xff", 4)));
}

// Markup far into a genuine list must not trip the check: filter rules
// legitimately contain '<' in places (element hiding, regex rules), so the
// HTML scan is deliberately limited to the head of the response.
TEST(ZephyrusAdblockUpdaterTest, AcceptsListWithMarkupLikeRulesFurtherDown) {
  std::string list = "[Adblock Plus 2.0]\n";
  list.append(8192, 'x');          // push past the 4 KB scan window
  list += "\nexample.com##div[data-ad] > span\n";
  list += "\n! <html> mentioned in a comment\n";
  EXPECT_TRUE(LooksLikeFilterList(list));
}

}  // namespace
}  // namespace zephyrus_adblock
