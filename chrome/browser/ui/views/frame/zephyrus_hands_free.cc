// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_hands_free.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/logging.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/functional/bind.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_memory_access.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_mascot_overlay.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_panel.h"
#include "chrome/browser/ui/views/frame/zephyrus_voice_access.h"
#include "chrome/browser/zephyrus/agent/model_settings.h"
#include "chrome/browser/zephyrus/agent/voice_input.h"
#include "chrome/browser/zephyrus/agent/voice_library.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/pref_service.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace zephyrus::agent {

namespace {

voice::Sensitivity ToSensitivity(int value) {
  switch (value) {
    case 0:
      return voice::Sensitivity::kStrict;
    case 2:
      return voice::Sensitivity::kRelaxed;
    default:
      return voice::Sensitivity::kBalanced;
  }
}

// 16 kHz mono 16-bit samples out of a WAV file; empty when it is not one.
std::vector<int16_t> ReadWav(const base::FilePath& path) {
  std::string bytes;
  if (!base::ReadFileToString(path, &bytes) || bytes.size() < 44) {
    return {};
  }
  size_t pos = 12;
  while (pos + 8 <= bytes.size()) {
    const std::string id = bytes.substr(pos, 4);
    const uint32_t size =
        static_cast<uint8_t>(bytes[pos + 4]) |
        (static_cast<uint8_t>(bytes[pos + 5]) << 8) |
        (static_cast<uint8_t>(bytes[pos + 6]) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(bytes[pos + 7])) << 24);
    if (id == "data") {
      const size_t available = bytes.size() - (pos + 8);
      std::vector<int16_t> pcm(std::min<size_t>(size, available) / 2);
      for (size_t i = 0; i < pcm.size(); ++i) {
        pcm[i] = static_cast<int16_t>(
            static_cast<uint8_t>(bytes[pos + 8 + 2 * i]) |
            (static_cast<uint8_t>(bytes[pos + 9 + 2 * i]) << 8));
      }
      return pcm;
    }
    pos += 8 + size;
  }
  return {};
}

// Faint room noise: the engine measures the room before it listens.
std::vector<int16_t> Room(int milliseconds) {
  std::vector<int16_t> out(static_cast<size_t>(milliseconds) * 16);
  uint32_t state = 7;
  for (int16_t& s : out) {
    state = state * 1664525u + 1013904223u;
    s = static_cast<int16_t>(static_cast<int>((state >> 16) % 41) - 20);
  }
  return out;
}

}  // namespace

ZephyrusHandsFree::ZephyrusHandsFree(BrowserView* browser_view,
                                     ZephyrusAgentPanel* panel)
    : browser_view_(browser_view),
      panel_(panel),
      engine_(std::make_unique<voice::HandsFreeEngine>(
          this,
          voice::HandsFreeEngine::Options())) {}

ZephyrusHandsFree::~ZephyrusHandsFree() {
  // Only the microphone: the window's views may already be going.
  if (mic_) {
    mic_->StopStreaming();
  }
}

void ZephyrusHandsFree::Start() {
  if (started_ || !browser_view_ || !browser_view_->browser() ||
      !g_browser_process) {
    return;
  }
  Profile* profile = browser_view_->browser()->profile();
  started_ = true;
  mic_ = std::make_unique<VoiceInput>(
      g_browser_process->shared_url_loader_factory());
  library_ = GetVoiceLibrary(profile);
  if (library_) {
    library_subscription_ = library_->Subscribe(base::BindRepeating(
        &ZephyrusHandsFree::SyncVoices, base::Unretained(this)));
    library_->WhenLoaded(base::BindOnce(&ZephyrusHandsFree::SyncVoices,
                                        weak_factory_.GetWeakPtr()));
  }
  if (views::Widget* widget = browser_view_->GetWidget()) {
    widget_.Observe(widget);
    active_ = widget->IsActive();
  }
  prefs_.Init(profile->GetPrefs());
  const auto on_pref = base::BindRepeating(&ZephyrusHandsFree::OnPrefChanged,
                                           base::Unretained(this));
  prefs_.Add(kHandsFreePref, on_pref);
  prefs_.Add(kVoiceLockPref, on_pref);
  prefs_.Add(kVoiceSensitivityPref, on_pref);
  prefs_.Add(kMicDevicePref, on_pref);
  Refresh();
}

void ZephyrusHandsFree::RunTestScenario(const std::string& dir) {
  if (!started_ || !library_) {
    return;
  }
  test_scenario_ = true;
  Refresh();
  const base::FilePath root = base::FilePath::FromUTF8Unsafe(dir);
  library_->WhenLoaded(base::BindOnce(
      [](base::WeakPtr<ZephyrusHandsFree> self, base::FilePath root) {
        if (!self || !self->library_) {
          return;
        }
        if (self->library_->profiles().empty()) {
          voice::VoiceProfile profile;
          profile.id = VoiceLibrary::NewId();
          profile.name = "Tester";
          for (int i = 0; i < 5; ++i) {
            voice::SampleCheck check = voice::AnalyseEnrollmentSample(
                ReadWav(root.AppendASCII("zira_wake_" + base::NumberToString(i) +
                                         ".wav")),
                profile.templates);
            if (check.problem == voice::SampleProblem::kNone) {
              profile.templates.push_back(std::move(check.sample));
            }
          }
          self->library_->Add(std::move(profile));
        }
        self->SyncVoices();
        LOG(INFO) << "ZEPHVOICE scenario start profiles="
                  << self->library_->profiles().size();
        std::vector<int16_t> audio = Room(700);
        const std::vector<int16_t> cmd = ReadWav(root.AppendASCII("cmd.wav"));
        audio.insert(audio.end(), cmd.begin(), cmd.end());
        const std::vector<int16_t> tail = Room(1500);
        audio.insert(audio.end(), tail.begin(), tail.end());
        self->test_audio_ = std::move(audio);
        self->test_position_ = 0;
        self->test_timer_.Start(
            FROM_HERE, base::Milliseconds(100),
            base::BindRepeating(&ZephyrusHandsFree::FeedNextTestChunk,
                                base::Unretained(self.get())));
      },
      weak_factory_.GetWeakPtr(), root));
}

void ZephyrusHandsFree::FeedNextTestChunk() {
  constexpr size_t kChunk = 1600;  // 100 ms at 16 kHz
  if (test_position_ >= test_audio_.size()) {
    test_timer_.Stop();
    return;
  }
  const size_t n = std::min(kChunk, test_audio_.size() - test_position_);
  engine_->Feed(base::span(test_audio_).subspan(test_position_, n));
  test_position_ += n;
}

void ZephyrusHandsFree::OnPrefChanged() {
  // A change in the settings is another try, after a microphone that would not
  // open.
  failed_ = false;
  // A different microphone was chosen: close the open one so Refresh() opens the
  // new one.
  if (listening_ && browser_view_ && browser_view_->browser() &&
      browser_view_->browser()->profile()->GetPrefs()->GetString(
          kMicDevicePref) != mic_name_) {
    mic_->StopStreaming();
    engine_->Reset();
    SetListening(false);
  }
  Refresh();
}

void ZephyrusHandsFree::SyncVoices() {
  if (!library_ || !library_->loaded()) {
    return;
  }
  names_.clear();
  for (const voice::VoiceProfile& p : library_->profiles()) {
    names_.push_back(p.name);
  }
  engine_->SetProfiles(library_->profiles());
  Refresh();
}

void ZephyrusHandsFree::Refresh() {
  if (!started_ || !browser_view_ || !browser_view_->browser()) {
    return;
  }
  Profile* profile = browser_view_->browser()->profile();
  PrefService* prefs = profile->GetPrefs();
  engine_->SetOptions(
      {.lock = prefs->GetBoolean(kVoiceLockPref),
       .sensitivity = ToSensitivity(prefs->GetInteger(kVoiceSensitivityPref))});

  const bool want = !test_scenario_ && prefs->GetBoolean(kHandsFreePref) &&
                    !profile->IsOffTheRecord() && !muted_ && paused_ == 0 &&
                    active_ && library_ && library_->loaded() &&
                    engine_->has_profiles();
  if (want && !listening_) {
    if (failed_) {
      return;
    }
    mic_name_ = prefs->GetString(kMicDevicePref);
    mic_->SetDeviceName(mic_name_);
    if (!mic_->StartStreaming(base::BindRepeating(
            &ZephyrusHandsFree::OnAudio, base::Unretained(this)))) {
      failed_ = true;
      panel_->OnVoiceNotice(
          "I could not open the microphone. Check that Windows lets desktop "
          "apps use it (Settings > Privacy > Microphone).");
      return;
    }
    SetListening(true);
  } else if (!want && listening_) {
    mic_->StopStreaming();
    engine_->Reset();
    SetListening(false);
  }
}

void ZephyrusHandsFree::SetListening(bool listening) {
  listening_ = listening;
  if (browser_view_) {
    if (ZephyrusAgentMascotOverlay* overlay =
            browser_view_->zephyrus_agent_mascot()) {
      overlay->SetListening(listening);
    }
  }
}

void ZephyrusHandsFree::Pause() {
  ++paused_;
  Refresh();
}

void ZephyrusHandsFree::Resume() {
  if (paused_ > 0) {
    --paused_;
  }
  Refresh();
}

void ZephyrusHandsFree::SetMuted(bool muted) {
  muted_ = muted;
  Refresh();
}

void ZephyrusHandsFree::OnAudio(base::span<const int16_t> pcm) {
  engine_->Feed(pcm);
}

void ZephyrusHandsFree::OnWidgetActivationChanged(views::Widget* widget,
                                                  bool active) {
  active_ = active;
  Refresh();
}

void ZephyrusHandsFree::OnWidgetDestroying(views::Widget* widget) {
  widget_.Reset();
  active_ = false;
  if (mic_) {
    mic_->StopStreaming();
  }
  listening_ = false;
}

// ---- What the engine hears ----------------------------------------------------

void ZephyrusHandsFree::OnWake(const voice::HandsFreeEngine::WakeInfo& info) {
  VLOG(1) << "ZEPHVOICE wake profile=" << info.profile;
  std::string who;
  if (info.profile >= 0 && static_cast<size_t>(info.profile) < names_.size()) {
    who = names_[info.profile];
  }
  panel_->OnVoiceWake(who);
}

void ZephyrusHandsFree::OnCommand(std::vector<int16_t> pcm,
                                  const voice::HandsFreeEngine::WakeInfo&) {
  VLOG(1) << "ZEPHVOICE command samples=" << pcm.size();
  panel_->OnVoiceTranscribing();
  // The command as bytes, for the transcription request. It leaves this
  // computer only from here on, and only this: what came before "Hey Zep", and
  // everything not addressed to Zep, was dropped where it was heard.
  std::string raw(base::as_string_view(base::as_chars(base::as_byte_span(pcm))));
  if (!g_browser_process || !g_browser_process->os_crypt_async()) {
    panel_->OnVoiceNotice("Voice needs the system keystore, which is not "
                          "available.");
    return;
  }
  g_browser_process->os_crypt_async()->GetInstance(
      base::BindOnce(&ZephyrusHandsFree::OnEncryptor,
                     weak_factory_.GetWeakPtr(), std::move(raw)));
}

void ZephyrusHandsFree::OnEncryptor(
    std::string pcm,
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  Profile* profile = browser_view_ && browser_view_->browser()
                         ? browser_view_->browser()->profile()
                         : nullptr;
  std::optional<VoiceCredentials> credentials =
      encryptor && profile
          ? LoadVoiceCredentials(*profile->GetPrefs(), *encryptor)
          : std::nullopt;
  if (!credentials) {
    panel_->OnVoiceNotice("Add an AssemblyAI key in the agent settings to use "
                          "voice.");
    return;
  }
  mic_->Transcribe(std::move(pcm), std::move(credentials->key),
                   std::move(credentials->endpoint),
                   base::BindOnce(&ZephyrusHandsFree::OnTranscript,
                                  weak_factory_.GetWeakPtr()));
}

void ZephyrusHandsFree::OnTranscript(bool ok, const std::string& text) {
  VLOG(1) << "ZEPHVOICE transcript ok=" << ok << " chars=" << text.size();
  if (ok) {
    panel_->OnVoiceHeard(text);
  } else {
    panel_->OnVoiceNotice(text);
  }
}

void ZephyrusHandsFree::OnTimedOut() {
  VLOG(1) << "ZEPHVOICE timed out";
  panel_->OnVoiceIdle();
}

void ZephyrusHandsFree::OnIgnored(voice::HandsFreeEngine::Ignored why,
                                  const voice::MatchResult&) {
  // Speech that was not for Zep is dropped in silence. Only a phrase said in a
  // voice that is not enrolled is answered, so a person whose voice is not
  // recognised is not left wondering whether it is broken.
  VLOG(1) << "ZEPHVOICE ignored why=" << static_cast<int>(why);
  if (why == voice::HandsFreeEngine::Ignored::kNotYourVoice ||
      why == voice::HandsFreeEngine::Ignored::kWrongVoiceFollowUp) {
    panel_->OnVoiceNotice("That does not sound like a voice I know.");
  }
}

}  // namespace zephyrus::agent
