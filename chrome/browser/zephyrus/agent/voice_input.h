// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_INPUT_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_INPUT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace zephyrus::agent {

// Voice commands for the agent: record the microphone, then transcribe the
// recording with AssemblyAI's sync API using the user's own key.
//
// Push to talk. Nothing is recorded until Start(), and recording stops at
// Stop() or after kMaxSeconds, whichever comes first. The audio exists only in
// memory and only until it has been sent; nothing is written to disk. Only the
// transcript comes back, and it becomes an ordinary typed task.
class VoiceInput {
 public:
  // `ok` false means `text` is a message for the user, not a transcript.
  using TranscriptCallback =
      base::OnceCallback<void(bool ok, const std::string& text)>;

  static constexpr int kSampleRate = 16000;
  static constexpr int kMaxSeconds = 60;

  explicit VoiceInput(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~VoiceInput();

  VoiceInput(const VoiceInput&) = delete;
  VoiceInput& operator=(const VoiceInput&) = delete;

  // Hands the microphone's audio to `on_audio` as it arrives, in chunks, and
  // keeps none of it: for listening for "Hey Zep", where what is heard is looked
  // at as it goes by and dropped. Stops at StopStreaming(). False, having opened
  // nothing, if there is no microphone or Windows denies access to it.
  using AudioCallback = base::RepeatingCallback<void(base::span<const int16_t>)>;
  bool StartStreaming(AudioCallback on_audio);
  void StopStreaming();
  bool streaming() const { return static_cast<bool>(on_audio_); }

  // Sends a recording that was made elsewhere (a command heard by the wake
  // listener) for transcription, exactly as StopAndTranscribe does with its own.
  // `pcm` is 16 kHz mono 16-bit little-endian.
  void Transcribe(std::string pcm,
                  std::string api_key,
                  std::string endpoint,
                  TranscriptCallback done);

  // Starts recording from the default microphone. False, having recorded
  // nothing, if there is no microphone or Windows denies access to it.
  bool Start();
  bool recording() const { return recording_; }

  // Stops recording and sends what was heard. `done` runs exactly once.
  // `endpoint` is empty for AssemblyAI itself. A demo build passes its proxy's
  // address instead, with the demo token as `api_key`.
  void StopAndTranscribe(std::string api_key,
                         std::string endpoint,
                         TranscriptCallback done);

  // Stops and throws the recording away.
  void Cancel();

  // Exposed for tests.
  static std::string MakeWav(const std::string& pcm16, int sample_rate);
  static std::string MultipartBody(const std::string& boundary,
                                   const std::string& wav,
                                   const std::string& config_json);
  // The transcript, or nullopt with `error` set to words for the user.
  static std::optional<std::string> ParseTranscript(int status,
                                                    const std::string& body,
                                                    std::string* error);

 private:
  struct Device;

  bool OpenDevice();
  void Poll();
  void StopRecording();
  void OnTranscribed(std::unique_ptr<network::SimpleURLLoader> loader,
                     TranscriptCallback done,
                     std::optional<std::string> body);

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::unique_ptr<Device> device_;
  bool recording_ = false;
  AudioCallback on_audio_;
  std::string pcm_;
  base::RepeatingTimer poll_timer_;
  base::WeakPtrFactory<VoiceInput> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_INPUT_H_
