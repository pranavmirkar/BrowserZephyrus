// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/threat/zephyrus_shield_dns.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "base/base64url.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "chrome/browser/zephyrus/threat/zephyrus_dns_wire.h"
#include "chrome/browser/zephyrus/threat/zephyrus_dns_wire_test_util.h"
#include "net/base/url_util.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_threat {
namespace {

using Verdict = ShieldDnsChecker::Verdict;
using testing_util::AsString;
using testing_util::CleanReply;
using testing_util::FilteredReply;

TEST(ZephyrusShieldDnsTest, OnlyPublicNamesAreAsked) {
  for (const char* ok : {"example.com", "a.b.example.co.uk", "xn--bcher-kva.com",
                         "login-secure.example.top", "1password.com"}) {
    EXPECT_TRUE(ShieldDnsChecker::IsEligibleHost(ok)) << ok;
  }
  for (const char* no :
       {"", "localhost", "intranet", "192.168.1.1", "10.0.0.5", "127.0.0.1",
        "[::1]", "::1", "2001:db8::1", "printer.local", "nas.lan", "x.internal",
        "app.localhost", "site.test", "host.example", "abc.onion", ".example.com",
        "example.com."}) {
    EXPECT_FALSE(ShieldDnsChecker::IsEligibleHost(no)) << no;
  }
  EXPECT_FALSE(ShieldDnsChecker::IsEligibleHost(std::string(300, 'a') + ".com"));
}

TEST(ZephyrusShieldDnsTest, OnlyTheExplicitSinkholeAnswerIsABlock) {
  // What the resolver says for a blocked name.
  EXPECT_EQ(ShieldDnsChecker::ParseAnswer(
                R"({"Status":0,"Answer":[{"name":"x.example","type":1,"TTL":60,"data":"0.0.0.0"}]})"),
            Verdict::kBlocked);
  // A normal answer.
  EXPECT_EQ(ShieldDnsChecker::ParseAnswer(
                R"({"Status":0,"Answer":[{"name":"x.example","type":1,"TTL":60,"data":"93.184.216.34"}]})"),
            Verdict::kClean);
  // No records is not a block.
  EXPECT_EQ(ShieldDnsChecker::ParseAnswer(R"({"Status":0})"), Verdict::kClean);
  // "Does not exist" may be a typo, never a block.
  EXPECT_EQ(ShieldDnsChecker::ParseAnswer(R"({"Status":3})"), Verdict::kUnknown);
  EXPECT_EQ(ShieldDnsChecker::ParseAnswer(R"({"Status":2})"), Verdict::kUnknown);
  // Anything unexpected is unknown, which callers treat as clean.
  for (const char* bad : {"", "not json", "[]", "null", "{}", R"({"Status":"0"})",
                          R"({"Status":0,"Answer":"0.0.0.0"})",
                          R"({"Status":0,"Answer":[1,2,3]})"}) {
    const Verdict v = ShieldDnsChecker::ParseAnswer(bad);
    EXPECT_NE(v, Verdict::kBlocked) << bad;
  }
  // Sinkhole text in a field that is not an answer's data is ignored.
  EXPECT_EQ(ShieldDnsChecker::ParseAnswer(
                R"({"Status":0,"Comment":"0.0.0.0","Answer":[{"data":"1.2.3.4"}]})"),
            Verdict::kClean);
}

constexpr char kCleanJson[] = R"({"Status":0,"Answer":[{"data":"1.2.3.4"}]})";
constexpr char kBlockedJson[] = R"({"Status":0,"Answer":[{"data":"0.0.0.0"}]})";

class ZephyrusShieldDnsCheckerTest : public testing::Test {
 protected:
  ZephyrusShieldDnsCheckerTest()
      : factory_(base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &test_factory_)),
        checker_(factory_) {}

  static GURL CloudflareQuery(const std::string& host) {
    GURL url = net::AppendQueryParameter(
        GURL("https://security.cloudflare-dns.com/dns-query"), "name", host);
    return net::AppendQueryParameter(url, "type", "A");
  }

  static GURL Quad9Query(const std::string& host) {
    std::string encoded;
    base::Base64UrlEncode(BuildDnsQuery(host), base::Base64UrlEncodePolicy::OMIT_PADDING,
                          &encoded);
    return net::AppendQueryParameter(GURL("https://dns.quad9.net/dns-query"), "dns",
                                     encoded);
  }

  // What each resolver says about `host`.
  void Cloudflare(const std::string& host, const std::string& json) {
    test_factory_.AddResponse(CloudflareQuery(host).spec(), json);
  }
  void Quad9(const std::string& host, const std::vector<uint8_t>& packet) {
    test_factory_.AddResponse(Quad9Query(host).spec(), AsString(packet));
  }
  void Quad9Down(const std::string& host) {
    test_factory_.AddResponse(Quad9Query(host).spec(), "oops",
                              net::HTTP_INTERNAL_SERVER_ERROR);
  }
  void BothClean(const std::string& host) {
    Cloudflare(host, kCleanJson);
    Quad9(host, CleanReply(host));
  }

  Verdict Ask(const std::string& host) {
    std::optional<Verdict> verdict;
    base::RunLoop loop;
    checker_.Check(host, base::BindLambdaForTesting([&](Verdict v) {
                     verdict = v;
                     loop.Quit();
                   }));
    EXPECT_FALSE(verdict.has_value()) << "the callback ran before Check() returned";
    loop.Run();
    return *verdict;
  }

  base::test::SingleThreadTaskEnvironment task_environment_;
  network::TestURLLoaderFactory test_factory_;
  scoped_refptr<network::WeakWrapperSharedURLLoaderFactory> factory_;
  ShieldDnsChecker checker_;
};

TEST_F(ZephyrusShieldDnsCheckerTest, ABlockedHostIsBlockedAndNothingElseIsSent) {
  Cloudflare("evil.example.com", kBlockedJson);
  Quad9("evil.example.com", CleanReply("evil.example.com"));
  EXPECT_EQ(Ask("evil.example.com"), Verdict::kBlocked);
}

TEST_F(ZephyrusShieldDnsCheckerTest, EitherResolverBlockingIsEnough) {
  // Quad9 sees what Cloudflare misses...
  Cloudflare("only-quad9.example.com", kCleanJson);
  Quad9("only-quad9.example.com", FilteredReply("only-quad9.example.com"));
  EXPECT_EQ(Ask("only-quad9.example.com"), Verdict::kBlocked);
  // ...and the other way round.
  Cloudflare("only-cf.example.com", kBlockedJson);
  Quad9("only-cf.example.com", CleanReply("only-cf.example.com"));
  EXPECT_EQ(Ask("only-cf.example.com"), Verdict::kBlocked);
  // Both agree it is fine.
  BothClean("fine.example.com");
  EXPECT_EQ(Ask("fine.example.com"), Verdict::kClean);
}

TEST_F(ZephyrusShieldDnsCheckerTest, ABlockDoesNotWaitForTheOtherResolver) {
  // Cloudflare says blocked; Quad9 never answers. The verdict must not wait for it.
  Cloudflare("quick.example.com", kBlockedJson);
  EXPECT_EQ(Ask("quick.example.com"), Verdict::kBlocked);
}

TEST_F(ZephyrusShieldDnsCheckerTest, OneResolverDownStillAnswersFromTheOther) {
  Cloudflare("a.example.com", kBlockedJson);
  Quad9Down("a.example.com");
  EXPECT_EQ(Ask("a.example.com"), Verdict::kBlocked);

  Cloudflare("b.example.com", kCleanJson);
  Quad9Down("b.example.com");
  // Cloudflare says fine, Quad9 could not be reached: not blocked, and not cached as
  // a firm "clean" either.
  EXPECT_EQ(Ask("b.example.com"), Verdict::kUnknown);

  test_factory_.AddResponse(CloudflareQuery("c.example.com").spec(), "oops",
                            net::HTTP_INTERNAL_SERVER_ERROR);
  Quad9("c.example.com", FilteredReply("c.example.com"));
  EXPECT_EQ(Ask("c.example.com"), Verdict::kBlocked);
}

TEST_F(ZephyrusShieldDnsCheckerTest, TheRequestsCarryOnlyTheHostName) {
  std::vector<network::ResourceRequest> seen;
  test_factory_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest& request) { seen.push_back(request); }));
  BothClean("site.example.com");
  EXPECT_EQ(Ask("site.example.com"), Verdict::kClean);
  ASSERT_EQ(seen.size(), 2u);
  std::vector<GURL> urls = {seen[0].url, seen[1].url};
  EXPECT_NE(std::find(urls.begin(), urls.end(), CloudflareQuery("site.example.com")),
            urls.end());
  EXPECT_NE(std::find(urls.begin(), urls.end(), Quad9Query("site.example.com")),
            urls.end());
  for (const network::ResourceRequest& request : seen) {
    EXPECT_EQ(request.method, "GET");
    EXPECT_EQ(request.credentials_mode, network::mojom::CredentialsMode::kOmit);
    EXPECT_FALSE(request.headers.HasHeader("Cookie"));
    EXPECT_FALSE(request.headers.HasHeader("Authorization"));
    EXPECT_FALSE(request.headers.HasHeader("Referer"));
    // The only thing in the URL beyond the fixed resolver address is the name.
    EXPECT_TRUE(request.url.SchemeIs("https"));
    EXPECT_FALSE(request.url.has_ref());
    EXPECT_FALSE(request.url.has_username());
  }
}

TEST_F(ZephyrusShieldDnsCheckerTest, AnAnswerIsCachedAndAskedOnce) {
  int requests = 0;
  test_factory_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest&) { ++requests; }));
  Cloudflare("a.example.com", kBlockedJson);
  Quad9("a.example.com", CleanReply("a.example.com"));
  EXPECT_EQ(Ask("a.example.com"), Verdict::kBlocked);
  EXPECT_EQ(Ask("a.example.com"), Verdict::kBlocked);
  EXPECT_EQ(Ask("a.example.com"), Verdict::kBlocked);
  EXPECT_EQ(requests, 2);  // one to each resolver, then the cache
}

TEST_F(ZephyrusShieldDnsCheckerTest, TwoAskersForTheSameHostShareTheRequests) {
  int requests = 0;
  test_factory_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest&) { ++requests; }));
  std::vector<Verdict> got;
  base::RunLoop loop;
  for (int i = 0; i < 2; ++i) {
    checker_.Check("b.example.com", base::BindLambdaForTesting([&](Verdict v) {
                     got.push_back(v);
                     if (got.size() == 2) {
                       loop.Quit();
                     }
                   }));
  }
  BothClean("b.example.com");
  loop.Run();
  EXPECT_EQ(requests, 2);
  EXPECT_EQ(got, (std::vector<Verdict>{Verdict::kClean, Verdict::kClean}));
}

TEST_F(ZephyrusShieldDnsCheckerTest, AFloodOfMadeUpHostsIsCappedAndFailsOpen) {
  int requests = 0;
  test_factory_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest&) { ++requests; }));
  std::vector<Verdict> answered;
  for (int i = 0; i < 40; ++i) {
    checker_.Check("flood" + std::to_string(i) + ".example.com",
                   base::BindLambdaForTesting(
                       [&](Verdict v) { answered.push_back(v); }));
  }
  base::RunLoop().RunUntilIdle();
  // Eight hosts went out (to both resolvers) and wait for an answer; the other 32
  // were answered at once with "unknown" (clean) and sent nothing.
  EXPECT_EQ(requests, 16);
  EXPECT_EQ(answered.size(), 32u);
  for (const Verdict v : answered) {
    EXPECT_EQ(v, Verdict::kUnknown);
  }
}

TEST_F(ZephyrusShieldDnsCheckerTest, ANetworkFailureIsUnknownNeverABlock) {
  Cloudflare("down.example.com", "");
  Quad9Down("down.example.com");
  test_factory_.AddResponse(CloudflareQuery("err.example.com").spec(), "oops",
                            net::HTTP_INTERNAL_SERVER_ERROR);
  Quad9Down("err.example.com");
  EXPECT_NE(Ask("err.example.com"), Verdict::kBlocked);
  EXPECT_NE(Ask("down.example.com"), Verdict::kBlocked);
}

TEST_F(ZephyrusShieldDnsCheckerTest, GarbageFromAResolverIsNeverABlock) {
  Cloudflare("junk.example.com", kCleanJson);
  test_factory_.AddResponse(Quad9Query("junk.example.com").spec(),
                            std::string(200, '\xFF'));
  EXPECT_NE(Ask("junk.example.com"), Verdict::kBlocked);
  // Quad9's answer to some other name does not count for this one.
  Cloudflare("swap.example.com", kCleanJson);
  Quad9("swap.example.com", FilteredReply("other.example.com"));
  EXPECT_NE(Ask("swap.example.com"), Verdict::kBlocked);
}

TEST_F(ZephyrusShieldDnsCheckerTest, AResolverThatKeepsFailingIsLeftAlone) {
  std::vector<GURL> seen;
  test_factory_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest& request) { seen.push_back(request.url); }));
  for (const char* host : {"d1.example.com", "d2.example.com", "d3.example.com"}) {
    Cloudflare(host, kCleanJson);
    Quad9Down(host);
    EXPECT_EQ(Ask(host), Verdict::kUnknown);
  }
  ASSERT_EQ(seen.size(), 6u);
  // After three failures in a row Quad9 is not asked at all, so a dead or blocked
  // resolver costs three slow pages, not every page.
  seen.clear();
  Cloudflare("d4.example.com", kCleanJson);
  EXPECT_EQ(Ask("d4.example.com"), Verdict::kClean);
  ASSERT_EQ(seen.size(), 1u);
  EXPECT_EQ(seen[0], CloudflareQuery("d4.example.com"));
}

}  // namespace
}  // namespace zephyrus_threat
