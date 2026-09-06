// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The local vision channel.
//
// The claim this feature makes is that the screenshot never leaves the machine,
// and only a description does. These tests are what makes that claim checkable
// rather than a sentence in a README.

#include "chrome/browser/zephyrus/agent/local_vision_client.h"

#include <string>
#include <vector>

#include "base/base64.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "chrome/browser/zephyrus/agent/observation.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

// Not a real JPEG, and it does not need to be: nothing here decodes it. What
// matters is where these bytes go and where they do not.
std::vector<uint8_t> FakeJpeg() {
  return {0xFF, 0xD8, 0xFF, 0xE0, 0x11, 0x22, 0x33, 0x44};
}

class LocalVisionClientTest : public testing::Test {
 protected:
  scoped_refptr<network::SharedURLLoaderFactory> factory() {
    return base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
        &test_factory_);
  }

  std::string Describe(LocalVisionClient& client,
                       const std::vector<uint8_t>& jpeg) {
    std::string got;
    base::RunLoop loop;
    client.Describe(jpeg, base::BindLambdaForTesting([&](std::string reply) {
                      got = std::move(reply);
                      loop.Quit();
                    }));
    loop.Run();
    return got;
  }

  base::test::TaskEnvironment task_environment_;
  network::TestURLLoaderFactory test_factory_;
};

TEST_F(LocalVisionClientTest, SendsThePictureToTheLocalModelAndReturnsWords) {
  std::string sent;
  test_factory_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest& request) {
        sent = network::GetUploadData(request);
        // One line, because splitting a raw string here produced two
        // juxtaposed JSON strings -- invalid JSON, which the client correctly
        // refused, and the test then blamed the client.
        test_factory_.AddResponse(
            request.url.spec(),
            R"({"message":{"content":"A video site. A cookie banner covers it."}})");
      }));

  LocalVisionClient client(GURL("http://127.0.0.1:11434"), "moondream",
                           factory());
  const std::string described = Describe(client, FakeJpeg());

  EXPECT_NE(described.find("cookie banner"), std::string::npos) << described;

  // The image goes out as base64 on the way to the LOCAL model. That is the
  // one hop it is allowed to make.
  EXPECT_NE(sent.find(base::Base64Encode(FakeJpeg())), std::string::npos)
      << "the picture never reached the local model";
}

TEST_F(LocalVisionClientTest, AsksForShapeRatherThanTranscription) {
  // The instruction matters as much as the plumbing. A model told to describe
  // everything will describe everything, including whatever it can read.
  std::string sent;
  test_factory_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest& request) {
        sent = network::GetUploadData(request);
        test_factory_.AddResponse(request.url.spec(),
                                  R"({"message":{"content":"ok"}})");
      }));

  LocalVisionClient client(GURL("http://127.0.0.1:11434"), "moondream",
                           factory());
  Describe(client, FakeJpeg());

  EXPECT_NE(sent.find("Do NOT transcribe text"), std::string::npos) << sent;
  EXPECT_NE(sent.find("do not read out names"), std::string::npos) << sent;
}

TEST_F(LocalVisionClientTest, AFailureIsAnEmptyDescriptionNotAStuckTask) {
  test_factory_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest& request) {
        test_factory_.AddResponse(request.url.spec(), "not json at all");
      }));

  LocalVisionClient client(GURL("http://127.0.0.1:11434"), "moondream",
                           factory());
  // A page the local model could not describe still has an accessibility tree.
  // Losing the whole step over a missing sentence would be the wrong trade.
  EXPECT_TRUE(Describe(client, FakeJpeg()).empty());
}

TEST_F(LocalVisionClientTest, NothingIsSentWhenThereIsNoPicture) {
  bool asked = false;
  test_factory_.SetInterceptor(
      base::BindLambdaForTesting([&](const network::ResourceRequest&) {
        asked = true;
      }));

  LocalVisionClient client(GURL("http://127.0.0.1:11434"), "moondream",
                           factory());
  EXPECT_TRUE(Describe(client, {}).empty());
  EXPECT_FALSE(asked) << "it made a request with no image to describe";
}

// --- what actually leaves the machine ------------------------------------

TEST(VisionObservationTest, TheDescriptionTravelsAndThePictureDoesNot) {
  // The whole design in one assertion.
  //
  // What the reasoning model receives is JSON. The description has to be in it,
  // because that is the point of looking; the image must NOT be, because the
  // promise is that it never leaves.
  Observation observation;
  observation.url = "https://example.org/";
  observation.title = "Example";
  observation.vision_summary = "A form with two fields and a Submit button.";
  observation.screenshot_jpeg = FakeJpeg();

  const std::string json = observation.ToJson(1);

  EXPECT_NE(json.find("A form with two fields"), std::string::npos) << json;

  // Neither raw nor base64. A screenshot smuggled into the prompt as text would
  // defeat the entire arrangement while looking like it worked.
  EXPECT_EQ(json.find(base::Base64Encode(FakeJpeg())), std::string::npos)
      << "the picture was encoded into the prompt: " << json;
  EXPECT_EQ(json.find("screenshot"), std::string::npos) << json;
}

TEST(VisionObservationTest, NoDescriptionMeansNoFieldAtAll) {
  // With vision off there must be no trace of it in the prompt -- not an empty
  // string, not a null. Every byte here is read by the model on every step.
  Observation observation;
  observation.url = "https://example.org/";

  const std::string json = observation.ToJson(1);
  EXPECT_EQ(json.find("looks_like"), std::string::npos) << json;
}

}  // namespace
}  // namespace zephyrus::agent
