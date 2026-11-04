// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_csp_filter.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "base/environment.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_adblock {
namespace {

const GURL kNoInitiator;

TEST(CspFilterTest, AddsThePolicyForTheSiteTheRuleNames) {
  AdblockCspFilter filter;
  EXPECT_EQ(filter.AddRules("||ads-site.example^$csp=frame-src 'self'\n"), 1u);
  const std::vector<std::string> policies = filter.PoliciesFor(
      GURL("https://ads-site.example/watch"), kNoInitiator, kTypeDocument);
  ASSERT_EQ(policies.size(), 1u);
  EXPECT_EQ(policies[0], "frame-src 'self'");
  EXPECT_TRUE(filter
                  .PoliciesFor(GURL("https://other.example/"), kNoInitiator,
                               kTypeDocument)
                  .empty());
}

TEST(CspFilterTest, OnlyPageAndFrameLoadsGetOne) {
  AdblockCspFilter filter;
  filter.AddRules("||ads-site.example^$csp=frame-src 'self'\n");
  const GURL url("https://ads-site.example/x.js");
  EXPECT_TRUE(filter.PoliciesFor(url, kNoInitiator, kTypeScript).empty());
  EXPECT_TRUE(filter.PoliciesFor(url, kNoInitiator, kTypeImage).empty());
  EXPECT_EQ(filter.PoliciesFor(url, GURL("https://page.example/"),
                               kTypeSubdocument)
                .size(),
            1u);
  EXPECT_TRUE(filter
                  .PoliciesFor(GURL("ftp://ads-site.example/"), kNoInitiator,
                               kTypeDocument)
                  .empty());
}

TEST(CspFilterTest, DomainOptionAndTheDocumentOption) {
  AdblockCspFilter filter;
  filter.AddRules(
      "$csp=script-src 'self',domain=thumbs.example\n"
      "||player.example^$document,csp=worker-src 'none'\n");
  EXPECT_EQ(filter
                .PoliciesFor(GURL("https://thumbs.example/a"), kNoInitiator,
                             kTypeDocument)
                .size(),
            1u);
  EXPECT_TRUE(filter
                  .PoliciesFor(GURL("https://elsewhere.example/a"),
                               kNoInitiator, kTypeDocument)
                  .empty());
  EXPECT_EQ(filter
                .PoliciesFor(GURL("https://player.example/a"), kNoInitiator,
                             kTypeDocument)
                .size(),
            1u);
}

TEST(CspFilterTest, ExceptionsCancelOnePolicyOrAll) {
  AdblockCspFilter filter;
  filter.AddRules(
      "||site.example^$csp=frame-src 'none'\n"
      "||site.example^$csp=worker-src 'none'\n"
      "@@||site.example/ok/$csp=frame-src 'none'\n"
      "@@||free.example^$csp\n"
      "||free.example^$csp=frame-src 'none'\n");
  EXPECT_EQ(filter
                .PoliciesFor(GURL("https://site.example/a"), kNoInitiator,
                             kTypeDocument)
                .size(),
            2u);
  const std::vector<std::string> ok = filter.PoliciesFor(
      GURL("https://site.example/ok/a"), kNoInitiator, kTypeDocument);
  ASSERT_EQ(ok.size(), 1u);
  EXPECT_EQ(ok[0], "worker-src 'none'");
  EXPECT_TRUE(filter
                  .PoliciesFor(GURL("https://free.example/a"), kNoInitiator,
                               kTypeDocument)
                  .empty());
}

TEST(CspFilterTest, RefusesWhatAListMustNotBeAbleToDo) {
  AdblockCspFilter filter;
  EXPECT_EQ(filter.AddRules(
                "||a.example^$csp=default-src 'self'; report-uri "
                "https://evil.example/r\n"
                "||b.example^$csp=default-src 'self'; Report-To grp\n"
                "$csp=script-src 'none'\n"       // every page on the web
                "||c.example^$csp\n"              // a block with nothing to add
                "||d.example^$csp=a b,csp=c d\n"  // two in one line
                ),
            0u);
  EXPECT_FALSE(AdblockCspFilter::IsAcceptablePolicy(""));
  EXPECT_FALSE(AdblockCspFilter::IsAcceptablePolicy("a\nb"));
  EXPECT_FALSE(AdblockCspFilter::IsAcceptablePolicy("a, b"));
  EXPECT_FALSE(AdblockCspFilter::IsAcceptablePolicy(std::string(5000, 'a')));
  EXPECT_TRUE(AdblockCspFilter::IsAcceptablePolicy(
      "sandbox allow-forms allow-same-origin allow-scripts"));
}

TEST(CspFilterTest, CosmeticAndOtherLinesAreNotMistakenForRules) {
  AdblockCspFilter filter;
  EXPECT_EQ(filter.AddRules(
                "! csp is mentioned here\n"
                "example.com##.csp-banner\n"
                "||cspcdn.example^$script\n"
                "||x.example^$csp=frame-src 'none'\n"),
            1u);
}

// Env-gated: how many `$csp` lines of a real list this filter keeps.
TEST(CspFilterTest, RealListCoverage) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  const std::optional<std::string> path = env->GetVar("ZEPHYRUS_FULL_LIST");
  if (!path) {
    GTEST_SKIP() << "set ZEPHYRUS_FULL_LIST";
  }
  std::ifstream file(*path);
  std::stringstream text;
  text << file.rdbuf();
  AdblockCspFilter filter;
  filter.AddRules(text.str());
  std::cout << "csp rules kept " << filter.rule_count() << " skipped "
            << filter.skipped_count() << std::endl;
}

}  // namespace
}  // namespace zephyrus_adblock
