// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The development model client.
//
// Most of these are about the one rule that matters: this only ever talks to
// loopback. The prompt it sends describes the page the user is looking at, and
// the endpoint comes from a command-line string, so the check has to be here
// rather than in whoever wrote the flag.

#include "chrome/browser/zephyrus/agent/dev_model_client.h"

#include <memory>
#include <string>

#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus::agent {
namespace {

class DevModelClientTest : public testing::Test {
 protected:
  scoped_refptr<network::SharedURLLoaderFactory> factory() {
    return base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
        &test_factory_);
  }

  std::unique_ptr<DevModelClient> Make(const std::string& endpoint) {
    return DevModelClient::Create(GURL(endpoint), "qwen2.5:7b", factory());
  }

  base::test::TaskEnvironment task_environment_;
  network::TestURLLoaderFactory test_factory_;
};

// --- the rule -----------------------------------------------------------

TEST_F(DevModelClientTest, RefusesAnythingThatIsNotLoopback) {
  // Each of these would send the user's page somewhere off their machine. A
  // typo in a command line must not be able to do that.
  EXPECT_FALSE(Make("http://models.example.com:11434"));
  EXPECT_FALSE(Make("https://api.openai.com"));
  EXPECT_FALSE(Make("http://192.168.1.50:11434"));
  EXPECT_FALSE(Make("http://10.0.0.5:11434"));
  // Looks local, is not.
  EXPECT_FALSE(Make("http://localhost.attacker.example:11434"));
}

TEST_F(DevModelClientTest, AcceptsLoopbackInItsUsualSpellings) {
  EXPECT_TRUE(Make("http://127.0.0.1:11434"));
  EXPECT_TRUE(Make("http://localhost:11434"));
  EXPECT_TRUE(Make("http://[::1]:11434"));
}

TEST_F(DevModelClientTest, RefusesNonHttpSchemes) {
  EXPECT_FALSE(Make("file:///tmp/model"));
  EXPECT_FALSE(Make("ws://127.0.0.1:11434"));
  EXPECT_FALSE(Make("not a url"));
}

TEST_F(DevModelClientTest, NeedsAModelName) {
  EXPECT_FALSE(DevModelClient::Create(GURL("http://127.0.0.1:11434"), "",
                                      factory()));
}

// --- talking to it ------------------------------------------------------

TEST_F(DevModelClientTest, SendsThePromptAndReadsTheReply) {
  std::unique_ptr<DevModelClient> client = Make("http://127.0.0.1:11434");
  ASSERT_TRUE(client);

  test_factory_.AddResponse(
      "http://127.0.0.1:11434/api/chat",
      R"({"message":{"role":"assistant","content":"{\"name\":\"tabs.list\"}"}})");

  std::string response;
  base::RunLoop run_loop;
  client->Propose("SYSTEM RULES", "TASK: find the specs",
                  base::BindLambdaForTesting([&](const std::string& got) {
                    response = got;
                    run_loop.Quit();
                  }));
  run_loop.Run();

  EXPECT_EQ(response, R"({"name":"tabs.list"})");

  // The prompt really went out, both halves of it.
  ASSERT_EQ(test_factory_.pending_requests()->size(), 0u);
}

TEST_F(DevModelClientTest, AnUnreachableServerIsAnEmptyReplyNotAHang) {
  // The loop treats a reply with no tool call as a wasted step and carries on,
  // which is the right thing to do about a model server that is not running.
  // Hanging would strand the task.
  std::unique_ptr<DevModelClient> client = Make("http://127.0.0.1:11434");
  ASSERT_TRUE(client);

  test_factory_.AddResponse("http://127.0.0.1:11434/api/chat", "",
                            net::HTTP_INTERNAL_SERVER_ERROR);

  bool answered = false;
  base::RunLoop run_loop;
  client->Propose("SYSTEM", "TASK",
                  base::BindLambdaForTesting([&](const std::string& got) {
                    answered = true;
                    EXPECT_TRUE(got.empty());
                    run_loop.Quit();
                  }));
  run_loop.Run();
  EXPECT_TRUE(answered);
}

TEST_F(DevModelClientTest, GarbageFromTheServerIsAnEmptyReply) {
  std::unique_ptr<DevModelClient> client = Make("http://127.0.0.1:11434");
  ASSERT_TRUE(client);

  for (const char* body : {"not json", "[]", "{}", R"({"message":{}})"}) {
    test_factory_.AddResponse("http://127.0.0.1:11434/api/chat", body);

    base::RunLoop run_loop;
    client->Propose("SYSTEM", "TASK",
                    base::BindLambdaForTesting([&](const std::string& got) {
                      EXPECT_TRUE(got.empty()) << body;
                      run_loop.Quit();
                    }));
    run_loop.Run();
  }
}

}  // namespace
}  // namespace zephyrus::agent
