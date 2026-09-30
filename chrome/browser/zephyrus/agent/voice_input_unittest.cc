// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/voice_input.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

TEST(VoiceInputTest, AWavHeaderDescribesSixteenKilohertzMonoPcm) {
  const std::string pcm(3200, '\x01');  // 100 ms
  const std::string wav = VoiceInput::MakeWav(pcm, 16000);
  ASSERT_EQ(wav.size(), 44u + pcm.size());
  EXPECT_EQ(wav.substr(0, 4), "RIFF");
  EXPECT_EQ(wav.substr(8, 8), "WAVEfmt ");
  EXPECT_EQ(wav.substr(36, 4), "data");
  // Sample rate, little-endian, at byte 24: 16000 = 0x3E80.
  EXPECT_EQ(static_cast<unsigned char>(wav[24]), 0x80);
  EXPECT_EQ(static_cast<unsigned char>(wav[25]), 0x3E);
  // Data size at byte 40: 3200 = 0x0C80.
  EXPECT_EQ(static_cast<unsigned char>(wav[40]), 0x80);
  EXPECT_EQ(static_cast<unsigned char>(wav[41]), 0x0C);
}

TEST(VoiceInputTest, TheMultipartBodyCarriesTheAudioAndTheConfig) {
  const std::string body =
      VoiceInput::MultipartBody("B", "WAVBYTES", "{\"prompt\":\"x\"}");
  EXPECT_NE(body.find("name=\"audio\"; filename=\"voice.wav\""),
            std::string::npos);
  EXPECT_NE(body.find("Content-Type: audio/wav\r\n\r\nWAVBYTES\r\n"),
            std::string::npos);
  EXPECT_NE(body.find("name=\"config\""), std::string::npos);
  EXPECT_TRUE(body.ends_with("--B--\r\n"));
}

TEST(VoiceInputTest, ReadsTheTranscript) {
  std::string error;
  EXPECT_EQ(VoiceInput::ParseTranscript(
                200, R"({"text":"open youtube","confidence":0.9})", &error),
            "open youtube");
}

TEST(VoiceInputTest, SaysWhyWhenItCannotTranscribe) {
  std::string error;
  EXPECT_FALSE(VoiceInput::ParseTranscript(401, R"({"title":"Unauthorized"})",
                                           &error));
  EXPECT_NE(error.find("did not accept the key"), std::string::npos);
  EXPECT_FALSE(VoiceInput::ParseTranscript(
      400, R"({"error_code":"audio_too_short","detail":"Audio is too short"})",
      &error));
  EXPECT_NE(error.find("Audio is too short"), std::string::npos);
  EXPECT_FALSE(VoiceInput::ParseTranscript(200, R"({"text":""})", &error));
  EXPECT_NE(error.find("Nothing was heard"), std::string::npos);
  EXPECT_FALSE(VoiceInput::ParseTranscript(502, "<html>", &error));
  EXPECT_NE(error.find("HTTP 502"), std::string::npos);
}

}  // namespace
}  // namespace zephyrus::agent
