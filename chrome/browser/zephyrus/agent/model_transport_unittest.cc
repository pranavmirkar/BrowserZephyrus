// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/model_transport.h"

#include <memory>
#include <optional>
#include <string>

#include "base/containers/flat_map.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

using Headers = base::flat_map<std::string, std::string>;

class ModelTransportTest : public testing::Test {
 protected:
  scoped_refptr<network::SharedURLLoaderFactory> Factory() {
    return base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
        &factory_);
  }

  std::unique_ptr<ModelTransport> Make(const std::string& kind,
                                       const std::string& base,
                                       const std::string& key = "sk-test") {
    return ModelTransport::Create(kind, GURL(base), key, Factory());
  }

  // Sends and waits. `seen` gets the request that reached the network, if any.
  std::pair<int, std::string> SendAndWait(
      ModelTransport& transport,
      const std::string& path,
      const Headers& headers = {{"content-type", "application/json"}},
      const std::string& body = "{}") {
    std::pair<int, std::string> result;
    base::RunLoop run_loop;
    transport.Send(path, headers, body,
                   base::BindLambdaForTesting([&](int status,
                                                  const std::string& reply) {
                     result = {status, reply};
                     run_loop.Quit();
                   }));
    run_loop.Run();
    return result;
  }

  void CaptureRequests() {
    factory_.SetInterceptor(base::BindLambdaForTesting(
        [&](const network::ResourceRequest& request) { seen_ = request; }));
  }

  base::test::TaskEnvironment task_environment_;
  network::TestURLLoaderFactory factory_;
  std::optional<network::ResourceRequest> seen_;
};

TEST_F(ModelTransportTest, RefusesToBeBuiltForAnythingUnsafe) {
  EXPECT_FALSE(Make("smtp", "https://api.example.com"));
  // Page content and a key must not travel in the clear to another machine.
  EXPECT_FALSE(Make("openai", "http://api.example.com/v1"));
  EXPECT_FALSE(Make("anthropic", "https://api.anthropic.com", ""));
  EXPECT_FALSE(Make("openai", "https://api.example.com/v1?x=1"));
  EXPECT_FALSE(Make("openai", "https://user:pw@api.example.com/v1"));
  // A line break in a key would inject a header.
  EXPECT_FALSE(Make("anthropic", "https://api.anthropic.com", "sk\r\nX-Evil: 1"));
  // A local server may be plain http and keyless.
  EXPECT_TRUE(Make("openai", "http://127.0.0.1:11434/v1", ""));
  EXPECT_TRUE(Make("anthropic", "https://api.anthropic.com"));
}

TEST_F(ModelTransportTest, OnlyTheProvidersOwnEndpointIsAllowed) {
  auto anthropic = Make("anthropic", "https://api.anthropic.com");
  EXPECT_TRUE(anthropic->IsAllowedPath("/v1/messages"));
  EXPECT_FALSE(anthropic->IsAllowedPath("/v1/files"));
  EXPECT_FALSE(anthropic->IsAllowedPath("/v1/messages/../files"));

  auto gemini = Make("gemini", "https://generativelanguage.googleapis.com");
  EXPECT_TRUE(gemini->IsAllowedPath("/v1beta/models/gemini-x:generateContent"));
  EXPECT_FALSE(gemini->IsAllowedPath("/v1beta/models/../files:generateContent"));
  EXPECT_FALSE(gemini->IsAllowedPath("/v1beta/models/a/b:generateContent"));
  EXPECT_FALSE(gemini->IsAllowedPath("/v1beta/models/:generateContent"));
  EXPECT_FALSE(gemini->IsAllowedPath("/v1beta/models/x?key=1:generateContent"));
}

TEST_F(ModelTransportTest, ARefusedRequestNeverReachesTheNetwork) {
  CaptureRequests();
  auto transport = Make("anthropic", "https://api.anthropic.com");
  auto [status, body] = SendAndWait(*transport, "/v1/files");
  EXPECT_EQ(status, 0);
  EXPECT_NE(body.find("refused"), std::string::npos);
  EXPECT_FALSE(seen_);

  // A header the kernel may not set is refused too, however it is spelled.
  std::tie(status, body) = SendAndWait(
      *transport, "/v1/messages", {{"X-Api-Key", "sk-attacker"}});
  EXPECT_EQ(status, 0);
  EXPECT_FALSE(seen_);
  std::tie(status, body) = SendAndWait(
      *transport, "/v1/messages", {{"anthropic-version", "1\r\nX-Evil: 1"}});
  EXPECT_EQ(status, 0);
  EXPECT_FALSE(seen_);
}

TEST_F(ModelTransportTest, SendsTheKeyTheProviderWantsAndNoCredentials) {
  CaptureRequests();
  factory_.AddResponse("https://api.anthropic.com/v1/messages", "{\"ok\":1}");
  auto transport = Make("anthropic", "https://api.anthropic.com", "sk-ant");
  auto [status, body] = SendAndWait(
      *transport, "/v1/messages",
      {{"content-type", "application/json"}, {"anthropic-version", "2023-06-01"}});
  EXPECT_EQ(status, 200);
  EXPECT_EQ(body, "{\"ok\":1}");
  ASSERT_TRUE(seen_);
  EXPECT_EQ(seen_->headers.GetHeader("x-api-key"), "sk-ant");
  EXPECT_FALSE(seen_->headers.HasHeader("Authorization"));
  EXPECT_EQ(seen_->credentials_mode, network::mojom::CredentialsMode::kOmit);
  EXPECT_EQ(seen_->method, "POST");
}

TEST_F(ModelTransportTest, OpenAiCompatibleUsesBearerOnTheUsersBasePath) {
  CaptureRequests();
  factory_.AddResponse("https://openrouter.ai/api/v1/chat/completions", "{}");
  auto transport = Make("openai", "https://openrouter.ai/api/v1/", "sk-or");
  auto [status, body] = SendAndWait(*transport, "/chat/completions");
  EXPECT_EQ(status, 200);
  ASSERT_TRUE(seen_);
  EXPECT_EQ(seen_->url.spec(), "https://openrouter.ai/api/v1/chat/completions");
  EXPECT_EQ(seen_->headers.GetHeader("Authorization"), "Bearer sk-or");
}

TEST_F(ModelTransportTest, AProvidersErrorBodyIsHandedBack) {
  factory_.AddResponse("https://api.anthropic.com/v1/messages",
                       R"({"error":{"message":"invalid x-api-key"}})",
                       net::HTTP_UNAUTHORIZED);
  auto transport = Make("anthropic", "https://api.anthropic.com");
  auto [status, body] = SendAndWait(*transport, "/v1/messages");
  EXPECT_EQ(status, 401);
  EXPECT_NE(body.find("invalid x-api-key"), std::string::npos);
}

TEST_F(ModelTransportTest, ANetworkFailureIsReportedAsTransient) {
  factory_.AddResponse(GURL("https://api.anthropic.com/v1/messages"),
                       network::mojom::URLResponseHead::New(), "",
                       network::URLLoaderCompletionStatus(net::ERR_CONNECTION_RESET));
  auto transport = Make("anthropic", "https://api.anthropic.com");
  auto [status, body] = SendAndWait(*transport, "/v1/messages");
  EXPECT_EQ(status, -1);
  EXPECT_NE(body.find("network error"), std::string::npos);
}

TEST_F(ModelTransportTest, AnOversizedRequestIsNotSent) {
  CaptureRequests();
  auto transport = Make("anthropic", "https://api.anthropic.com");
  auto [status, body] = SendAndWait(
      *transport, "/v1/messages", {{"content-type", "application/json"}},
      std::string(ModelTransport::kMaxRequestBytes + 1, 'x'));
  EXPECT_EQ(status, 0);
  EXPECT_FALSE(seen_);
}

}  // namespace
}  // namespace zephyrus::agent
