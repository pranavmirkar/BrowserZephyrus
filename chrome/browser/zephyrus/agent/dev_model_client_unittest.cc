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

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/scoped_command_line.h"
#include "base/threading/thread_restrictions.h"
#include "base/run_loop.h"
#include "base/json/json_reader.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
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

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
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
  // The loop interprets an empty reply as a model-server failure.
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

TEST_F(DevModelClientTest, ConstrainsGenerationToBoundedJson) {
  auto client = Make("http://127.0.0.1:11434");
  test_factory_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest& request) {
        auto body = base::JSONReader::ReadDict(network::GetUploadData(request),
                                             base::JSON_PARSE_RFC);
        ASSERT_TRUE(body);
        EXPECT_EQ(*body->FindString("format"), "json");
        ASSERT_TRUE(body->FindDict("options"));
        EXPECT_EQ(body->FindDict("options")->FindInt("num_predict"), 256);
      }));
  test_factory_.AddResponse("http://127.0.0.1:11434/api/chat",
                           R"({"message":{"content":"{}"}})");
  client->Propose("JSON rules", "TASK", base::BindLambdaForTesting(
      [](const std::string&) {}));
  task_environment_.RunUntilIdle();
}

TEST_F(DevModelClientTest, UnresponsiveServerHasAFiniteDeadline) {
  auto client = Make("http://127.0.0.1:11434");
  bool answered = false;
  client->Propose("JSON rules", "TASK", base::BindLambdaForTesting(
      [&](const std::string& reply) {
        answered = true;
        EXPECT_TRUE(reply.empty());
      }));
  task_environment_.FastForwardBy(base::Seconds(91));
  EXPECT_TRUE(answered);
}

// --- replay -------------------------------------------------------------

TEST_F(DevModelClientTest, ReplaysARecordedSessionInOrder) {
  // A model at temperature zero is repeatable in principle and not in practice:
  // it is another process, on a machine whose load moves, behind a server that
  // can truncate a prompt. Debugging the HARNESS against it means every change
  // is measured against a moving input -- which is how a week goes into
  // deciding whether a fix worked.
  //
  // With a recording the model is a constant, so a difference in behaviour is
  // the harness's.
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath path = dir.GetPath().AppendASCII("session.jsonl");
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::WriteFile(
        path,
        "{\"prompt\":\"one\",\"reply\":\"FIRST\"}\n"
        "{\"prompt\":\"two\",\"reply\":\"SECOND\"}\n"));
  }

  base::test::ScopedCommandLine scoped_command_line;
  scoped_command_line.GetProcessCommandLine()->AppendSwitchPath(
      "zephyrus-agent-replay", path);

  // No endpoint and no model name: a recording replaces both.
  std::unique_ptr<DevModelClient> client =
      DevModelClient::CreateIfConfigured(factory());
  ASSERT_TRUE(client) << "a recording should be a complete substitute for a "
                         "model, needing no endpoint";

  std::vector<std::string> got;
  for (int i = 0; i < 3; ++i) {
    base::RunLoop loop;
    client->Propose("rules", "prompt",
                    base::BindLambdaForTesting([&](const std::string& reply) {
                      got.push_back(reply);
                      loop.Quit();
                    }));
    loop.Run();
  }

  ASSERT_EQ(got.size(), 3u);
  EXPECT_EQ(got[0], "FIRST");
  EXPECT_EQ(got[1], "SECOND");
  // Past the end is an empty reply, which the loop already handles as "the
  // model said nothing". A recording that runs out is a shorter task.
  EXPECT_EQ(got[2], "");
}

TEST_F(DevModelClientTest, AskingBeforeTheRecordingLoadsStillAnswersInOrder) {
  // The file is read off the disk, so a Propose can arrive first. Answering it
  // with "nothing recorded" because the read had not finished would make replay
  // depend on disk timing -- and being independent of timing is the whole point.
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath path = dir.GetPath().AppendASCII("session.jsonl");
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::WriteFile(path, "{\"reply\":\"ONLY\"}\n"));
  }

  base::test::ScopedCommandLine scoped_command_line;
  scoped_command_line.GetProcessCommandLine()->AppendSwitchPath(
      "zephyrus-agent-replay", path);

  std::unique_ptr<DevModelClient> client =
      DevModelClient::CreateIfConfigured(factory());
  ASSERT_TRUE(client);

  // Asked immediately, before the read can have finished.
  std::string first;
  base::RunLoop loop;
  client->Propose("rules", "prompt",
                  base::BindLambdaForTesting([&](const std::string& reply) {
                    first = reply;
                    loop.Quit();
                  }));
  loop.Run();
  EXPECT_EQ(first, "ONLY");
}

}  // namespace
}  // namespace zephyrus::agent
