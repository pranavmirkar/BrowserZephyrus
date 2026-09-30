// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_HANDS_FREE_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_HANDS_FREE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "chrome/browser/zephyrus/agent/hands_free.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;

namespace os_crypt_async {
class Encryptor;
}

namespace zephyrus::agent {

class VoiceInput;
class VoiceLibrary;
class ZephyrusAgentPanel;

// "Hey Zep" for one browser window.
//
// It opens the microphone -- only while it should be listening -- and hands the
// audio to the wake engine, which decides on this computer whether anyone said
// the phrase and in whose voice. Only then is the command that follows sent for
// transcription, and the result becomes an ordinary task, run in front of the
// person with the mascot as its cursor and the answer above the mascot.
//
// WHEN IT LISTENS. All of these, or the microphone is closed:
//   * the person turned "Hey Zep" on (off by default);
//   * at least one voice is recorded, because the detector is trained from them;
//   * this window is the active one, so a browser in the background is not
//     listening to the room;
//   * it is not muted, and the panel's own mic button does not have the
//     microphone;
//   * it is not a Private Workspace, which keeps nothing and so has no voices.
// The mascot shows a small light whenever it is listening.
class ZephyrusHandsFree : public voice::HandsFreeEngine::Delegate,
                          public views::WidgetObserver {
 public:
  ZephyrusHandsFree(BrowserView* browser_view, ZephyrusAgentPanel* panel);
  ~ZephyrusHandsFree() override;

  ZephyrusHandsFree(const ZephyrusHandsFree&) = delete;
  ZephyrusHandsFree& operator=(const ZephyrusHandsFree&) = delete;

  // Once the window has a widget: starts following its activation, the settings
  // and the voices.
  void Start();

  // Opens or closes the microphone to match all of the above. Cheap; called
  // whenever any of it may have changed.
  void Refresh();

  // The panel's mic button, and the enrolment dialog, need the microphone: the
  // wake listener steps aside until they are done.
  void Pause();
  void Resume();

  base::WeakPtr<ZephyrusHandsFree> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // Dev only (--zephyrus-test-compact=voice-hands-free): drives the whole
  // hands-free path from files instead of the microphone. `dir` holds
  // zira_wake_0..4.wav (the voice to enrol) and cmd.wav (the phrase and a
  // command, spoken). The wake engine, the lock, the transcription and the task
  // are the real ones; only the audio source is a file.
  void RunTestScenario(const std::string& dir);

  // One-click mute, for now and not remembered.
  void SetMuted(bool muted);
  bool muted() const { return muted_; }

  // The microphone is open for the wake phrase.
  bool listening() const { return listening_; }

  // voice::HandsFreeEngine::Delegate:
  void OnWake(const voice::HandsFreeEngine::WakeInfo& info) override;
  void OnCommand(std::vector<int16_t> pcm,
                 const voice::HandsFreeEngine::WakeInfo& info) override;
  void OnTimedOut() override;
  void OnIgnored(voice::HandsFreeEngine::Ignored why,
                 const voice::MatchResult& match) override;

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  void SyncVoices();
  void FeedNextTestChunk();
  void OnPrefChanged();
  void SetListening(bool listening);
  void OnAudio(base::span<const int16_t> pcm);
  void OnEncryptor(std::string pcm,
                   scoped_refptr<os_crypt_async::Encryptor> encryptor);
  void OnTranscript(bool ok, const std::string& text);

  const raw_ptr<BrowserView> browser_view_;
  const raw_ptr<ZephyrusAgentPanel> panel_;
  raw_ptr<VoiceLibrary> library_ = nullptr;
  std::unique_ptr<VoiceInput> mic_;
  std::unique_ptr<voice::HandsFreeEngine> engine_;
  // Whose voice is which, as the engine was last given them.
  std::vector<std::string> names_;

  base::ScopedObservation<views::Widget, views::WidgetObserver> widget_{this};
  base::CallbackListSubscription library_subscription_;
  PrefChangeRegistrar prefs_;
  bool started_ = false;
  bool active_ = false;
  bool muted_ = false;
  int paused_ = 0;
  bool failed_ = false;
  bool listening_ = false;
  // A test scenario is feeding the engine: the real microphone stays closed.
  // The audio is fed at the speed it was spoken, so what is on screen and what
  // would be heard line up.
  bool test_scenario_ = false;
  std::vector<int16_t> test_audio_;
  size_t test_position_ = 0;
  base::RepeatingTimer test_timer_;
  std::string last_settings_key_;

  base::WeakPtrFactory<ZephyrusHandsFree> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_HANDS_FREE_H_
