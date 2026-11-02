// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/threat/zephyrus_shield_dns.h"

#include <optional>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "net/base/url_util.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_threat {
namespace {

using Verdict = ShieldDnsChecker::Verdict;

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

class ZephyrusShieldDnsCheckerTest : public testing::Test {
 protected:
  ZephyrusShieldDnsCheckerTest()
      : factory_(base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &test_factory_)),
        checker_(factory_) {}

  GURL Query(const std::string& host) {
    GURL url = net::AppendQueryParameter(
        GURL("https://security.cloudflare-dns.com/dns-query"), "name", host);
    return net::AppendQueryParameter(url, "type", "A");
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
  test_factory_.AddResponse(Query("evil.example.com").spec(),
                            R"({"Status":0,"Answer":[{"data":"0.0.0.0"}]})");
  EXPECT_EQ(Ask("evil.example.com"), Verdict::kBlocked);
}

TEST_F(ZephyrusShieldDnsCheckerTest, TheRequestCarriesOnlyTheHostName) {
  std::optional<network::ResourceRequest> seen;
  test_factory_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest& request) { seen = request; }));
  test_factory_.AddResponse(Query("site.example.com").spec(),
                            R"({"Status":0,"Answer":[{"data":"1.2.3.4"}]})");
  EXPECT_EQ(Ask("site.example.com"), Verdict::kClean);
  ASSERT_TRUE(seen.has_value());
  EXPECT_EQ(seen->url, Query("site.example.com"));
  EXPECT_EQ(seen->method, "GET");
  EXPECT_EQ(seen->credentials_mode, network::mojom::CredentialsMode::kOmit);
  EXPECT_FALSE(seen->headers.HasHeader("Cookie"));
  EXPECT_FALSE(seen->headers.HasHeader("Authorization"));
  EXPECT_FALSE(seen->headers.HasHeader("Referer"));
}

TEST_F(ZephyrusShieldDnsCheckerTest, AnAnswerIsCachedAndAskedOnce) {
  int requests = 0;
  test_factory_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest&) { ++requests; }));
  test_factory_.AddResponse(Query("a.example.com").spec(),
                            R"({"Status":0,"Answer":[{"data":"0.0.0.0"}]})");
  EXPECT_EQ(Ask("a.example.com"), Verdict::kBlocked);
  EXPECT_EQ(Ask("a.example.com"), Verdict::kBlocked);
  EXPECT_EQ(Ask("a.example.com"), Verdict::kBlocked);
  EXPECT_EQ(requests, 1);
}

TEST_F(ZephyrusShieldDnsCheckerTest, TwoAskersForTheSameHostShareOneRequest) {
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
  test_factory_.AddResponse(Query("b.example.com").spec(),
                            R"({"Status":0,"Answer":[{"data":"5.6.7.8"}]})");
  loop.Run();
  EXPECT_EQ(requests, 1);
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
  // Eight went out and wait for an answer; the other 32 were answered at once
  // with "unknown" (clean) and sent nothing.
  EXPECT_EQ(requests, 8);
  EXPECT_EQ(answered.size(), 32u);
  for (const Verdict v : answered) {
    EXPECT_EQ(v, Verdict::kUnknown);
  }
}

TEST_F(ZephyrusShieldDnsCheckerTest, ANetworkFailureIsUnknownNeverABlock) {
  test_factory_.AddResponse(Query("down.example.com").spec(), "");
  test_factory_.AddResponse(Query("err.example.com").spec(), "oops",
                            net::HTTP_INTERNAL_SERVER_ERROR);
  EXPECT_NE(Ask("err.example.com"), Verdict::kBlocked);
  EXPECT_NE(Ask("down.example.com"), Verdict::kBlocked);
}

}  // namespace
}  // namespace zephyrus_threat
