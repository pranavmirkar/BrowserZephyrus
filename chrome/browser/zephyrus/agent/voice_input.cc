// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/voice_input.h"

#include <windows.h>

#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace zephyrus::agent {
namespace {

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("zephyrus_agent_voice", R"(
        semantics {
          sender: "Zephyrus Agent voice commands"
          description:
            "Sends a short recording of the user speaking a command to "
            "AssemblyAI with the user's own API key, and reads back the text. "
            "The text becomes an agent task, as if typed."
          trigger:
            "Only when the user presses the microphone button in the agent "
            "panel, speaks, and presses it again to stop."
          data:
            "The recorded audio of that one command, up to sixty seconds."
          destination: OTHER
          destination_other: "AssemblyAI (sync.assemblyai.com)."
          internal { contacts { email: "pranavmirkar@gmail.com" } }
          user_data { type: USER_CONTENT }
          last_reviewed: "2026-09-28"
        }
        policy {
          cookies_allowed: NO
          setting:
            "Off until the user adds an AssemblyAI key in the agent's Model "
            "settings; nothing is recorded until the microphone is pressed."
          policy_exception_justification:
            "No enterprise policy yet; the feature is off by default."
        })");

constexpr char kEndpoint[] = "https://sync.assemblyai.com/transcribe";

// 100 ms per buffer, eight in flight: enough slack that a busy UI thread
// polling every 50 ms never lets the device run dry.
constexpr int kBufferMs = 100;
constexpr size_t kBuffers = 8;
constexpr size_t kBufferBytes = VoiceInput::kSampleRate * 2 * kBufferMs / 1000;

void AppendLittleEndian(std::string& out, uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i) {
    out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
  }
}

}  // namespace

// The Windows capture device and its buffers. Kept out of the header so
// <windows.h> stays out of everything that includes it.
struct VoiceInput::Device {
  HWAVEIN handle = nullptr;
  std::array<WAVEHDR, kBuffers> headers = {};
  std::array<std::vector<char>, kBuffers> data;
};

VoiceInput::VoiceInput(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : url_loader_factory_(std::move(url_loader_factory)) {}

VoiceInput::~VoiceInput() {
  StopRecording();
}

bool VoiceInput::Start() {
  if (recording_) {
    return true;
  }
  pcm_.clear();
  return OpenDevice();
}

bool VoiceInput::StartStreaming(AudioCallback on_audio) {
  if (recording_) {
    return false;  // one thing at a time on the microphone
  }
  on_audio_ = std::move(on_audio);
  if (!OpenDevice()) {
    on_audio_.Reset();
    return false;
  }
  return true;
}

void VoiceInput::StopStreaming() {
  on_audio_.Reset();
  StopRecording();
}

bool VoiceInput::OpenDevice() {
  auto device = std::make_unique<Device>();

  WAVEFORMATEX format = {};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 1;
  format.nSamplesPerSec = kSampleRate;
  format.wBitsPerSample = 16;
  format.nBlockAlign = 2;
  format.nAvgBytesPerSec = kSampleRate * 2;
  if (waveInOpen(&device->handle, WAVE_MAPPER, &format, 0, 0,
                 CALLBACK_NULL) != MMSYSERR_NOERROR) {
    return false;
  }
  for (size_t i = 0; i < kBuffers; ++i) {
    device->data[i].assign(kBufferBytes, 0);
    WAVEHDR& header = device->headers[i];
    header.lpData = device->data[i].data();
    header.dwBufferLength = static_cast<DWORD>(kBufferBytes);
    if (waveInPrepareHeader(device->handle, &header, sizeof(WAVEHDR)) !=
            MMSYSERR_NOERROR ||
        waveInAddBuffer(device->handle, &header, sizeof(WAVEHDR)) !=
            MMSYSERR_NOERROR) {
      waveInReset(device->handle);
      waveInClose(device->handle);
      return false;
    }
  }
  if (waveInStart(device->handle) != MMSYSERR_NOERROR) {
    waveInReset(device->handle);
    waveInClose(device->handle);
    return false;
  }
  device_ = std::move(device);
  recording_ = true;
  poll_timer_.Start(FROM_HERE, base::Milliseconds(50),
                    base::BindRepeating(&VoiceInput::Poll,
                                        base::Unretained(this)));
  return true;
}

void VoiceInput::Poll() {
  if (!device_) {
    return;
  }
  for (WAVEHDR& header : device_->headers) {
    if (!(header.dwFlags & WHDR_DONE)) {
      continue;
    }
    if (on_audio_) {
      // Handed on and dropped. A copy, so what the callback is given is aligned
      // and stays valid however long it looks at it.
      // The driver reports how much it wrote; a value past the buffer is a driver
      // bug, and copying that much would read beyond what was allocated.
      header.dwBytesRecorded =
          std::min<DWORD>(header.dwBytesRecorded, header.dwBufferLength);
      std::vector<int16_t> samples(header.dwBytesRecorded / 2);
      if (!samples.empty()) {
        base::as_writable_byte_span(samples).copy_from(
            UNSAFE_BUFFERS(base::span(
                reinterpret_cast<const uint8_t*>(header.lpData),
                samples.size() * 2)));
      }
      header.dwFlags &= ~WHDR_DONE;
      header.dwBytesRecorded = 0;
      if (recording_) {
        waveInAddBuffer(device_->handle, &header, sizeof(WAVEHDR));
      }
      if (!samples.empty()) {
        // The callback may stop the stream; nothing after this touches it.
        AudioCallback callback = on_audio_;
        callback.Run(samples);
      }
      if (!device_) {
        return;
      }
      continue;
    }
    pcm_.append(header.lpData,
                std::min<DWORD>(header.dwBytesRecorded, header.dwBufferLength));
    header.dwFlags &= ~WHDR_DONE;
    header.dwBytesRecorded = 0;
    if (recording_) {
      waveInAddBuffer(device_->handle, &header, sizeof(WAVEHDR));
    }
  }
  // A forgotten button must not record forever.
  if (!on_audio_ &&
      pcm_.size() >= static_cast<size_t>(kSampleRate * 2 * kMaxSeconds)) {
    StopRecording();
  }
}

void VoiceInput::StopRecording() {
  poll_timer_.Stop();
  if (!device_) {
    recording_ = false;
    return;
  }
  recording_ = false;
  waveInStop(device_->handle);
  // Reset returns every queued buffer marked done, holding whatever was
  // recorded into it: the end of the sentence.
  waveInReset(device_->handle);
  for (WAVEHDR& header : device_->headers) {
    if ((header.dwFlags & WHDR_DONE) && header.dwBytesRecorded > 0) {
      pcm_.append(header.lpData, std::min<DWORD>(header.dwBytesRecorded,
                                                 header.dwBufferLength));
    }
    waveInUnprepareHeader(device_->handle, &header, sizeof(WAVEHDR));
  }
  waveInClose(device_->handle);
  device_.reset();
}

void VoiceInput::Cancel() {
  StopRecording();
  pcm_.clear();
}

void VoiceInput::StopAndTranscribe(std::string api_key,
                                   std::string endpoint,
                                   TranscriptCallback done) {
  StopRecording();
  std::string pcm = std::move(pcm_);
  pcm_.clear();
  Transcribe(std::move(pcm), std::move(api_key), std::move(endpoint),
             std::move(done));
}

void VoiceInput::Transcribe(std::string pcm,
                            std::string api_key,
                            std::string endpoint,
                            TranscriptCallback done) {
  // The sync API's floor is 80 ms; under a quarter of a second is a click on
  // the button, not a command.
  if (pcm.size() < static_cast<size_t>(kSampleRate * 2 / 4)) {
    std::move(done).Run(false, "That was too short to hear anything.");
    return;
  }
  if (api_key.empty() || api_key.find_first_of("\r\n") != std::string::npos ||
      !url_loader_factory_) {
    std::move(done).Run(false,
                        "Add an AssemblyAI key in Model settings to use voice.");
    return;
  }

  const std::string boundary =
      "zephyrus" + base::NumberToString(base::RandUint64());
  base::DictValue config;
  // No language_code: the model code-switches between its languages, which is
  // how people in India actually speak -- Hindi and English in one sentence.
  config.Set("prompt",
             "A spoken command to a web browser's AI agent, such as opening a "
             "site, searching, or filling something in.");
  const std::string body =
      MultipartBody(boundary, MakeWav(pcm, kSampleRate),
                    base::WriteJson(config).value_or("{}"));

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(endpoint.empty() ? std::string(kEndpoint) : endpoint);
  // Speech and a credential go over this: https, or plain http to this machine
  // only (a proxy under test).
  if (!request->url.is_valid() ||
      !(request->url.SchemeIs("https") ||
        (request->url.SchemeIs("http") && net::IsLocalhost(request->url)))) {
    std::move(done).Run(false, "The voice address is not usable.");
    return;
  }
  request->method = "POST";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE;
  // AssemblyAI takes the raw key, no "Bearer".
  request->headers.SetHeader("Authorization", api_key);
  request->headers.SetHeader("X-AAI-Model", "universal-3-5-pro");

  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->SetTimeoutDuration(base::Seconds(60));
  loader->SetAllowHttpErrorResults(true);
  loader->AttachStringForUpload(
      body, "multipart/form-data; boundary=" + boundary);
  network::SimpleURLLoader* raw = loader.get();
  raw->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&VoiceInput::OnTranscribed, weak_factory_.GetWeakPtr(),
                     std::move(loader), std::move(done)),
      1024 * 1024);
}

void VoiceInput::OnTranscribed(std::unique_ptr<network::SimpleURLLoader> loader,
                               TranscriptCallback done,
                               std::optional<std::string> body) {
  int status = 0;
  if (loader->ResponseInfo() && loader->ResponseInfo()->headers) {
    status = loader->ResponseInfo()->headers->response_code();
  }
  if (!body) {
    std::move(done).Run(false, "Could not reach AssemblyAI.");
    return;
  }
  std::string error;
  std::optional<std::string> text = ParseTranscript(status, *body, &error);
  if (!text) {
    std::move(done).Run(false, error);
    return;
  }
  std::move(done).Run(true, *text);
}

// static
std::string VoiceInput::MakeWav(const std::string& pcm16, int sample_rate) {
  std::string wav;
  const uint32_t data_size = static_cast<uint32_t>(pcm16.size());
  wav.append("RIFF");
  AppendLittleEndian(wav, 36 + data_size, 4);
  wav.append("WAVEfmt ");
  AppendLittleEndian(wav, 16, 4);                  // fmt chunk size
  AppendLittleEndian(wav, 1, 2);                   // PCM
  AppendLittleEndian(wav, 1, 2);                   // mono
  AppendLittleEndian(wav, sample_rate, 4);
  AppendLittleEndian(wav, sample_rate * 2, 4);     // byte rate
  AppendLittleEndian(wav, 2, 2);                   // block align
  AppendLittleEndian(wav, 16, 2);                  // bits per sample
  wav.append("data");
  AppendLittleEndian(wav, data_size, 4);
  wav.append(pcm16);
  return wav;
}

// static
std::string VoiceInput::MultipartBody(const std::string& boundary,
                                      const std::string& wav,
                                      const std::string& config_json) {
  const std::string crlf = "\r\n";
  return base::StrCat(
      {"--", boundary, crlf,
       "Content-Disposition: form-data; name=\"audio\"; filename=\"voice.wav\"",
       crlf, "Content-Type: audio/wav", crlf, crlf, wav, crlf, "--", boundary,
       crlf, "Content-Disposition: form-data; name=\"config\"", crlf,
       "Content-Type: application/json", crlf, crlf, config_json, crlf, "--",
       boundary, "--", crlf});
}

// static
std::optional<std::string> VoiceInput::ParseTranscript(int status,
                                                       const std::string& body,
                                                       std::string* error) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  const base::DictValue* dict =
      parsed && parsed->is_dict() ? &parsed->GetDict() : nullptr;
  if (status == 200 && dict) {
    if (const std::string* text = dict->FindString("text")) {
      if (text->empty()) {
        *error = "Nothing was heard. Try again a little closer to the mic.";
        return std::nullopt;
      }
      return *text;
    }
  }
  // RFC 9457 problem details: `detail`, else `title`, else the status.
  std::string why;
  if (dict) {
    if (const std::string* detail = dict->FindString("detail")) {
      why = *detail;
    } else if (const std::string* title = dict->FindString("title")) {
      why = *title;
    } else if (const std::string* message = dict->FindString("error")) {
      why = *message;
    }
  }
  if (status == 401 || status == 403) {
    *error = "AssemblyAI did not accept the key. Check it in Model settings.";
  } else {
    *error = "AssemblyAI could not transcribe that (HTTP " +
             base::NumberToString(status) + (why.empty() ? "" : ": ") +
             why.substr(0, 200) + ").";
  }
  return std::nullopt;
}

}  // namespace zephyrus::agent
