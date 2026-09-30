// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_PANEL_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_PANEL_H_

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_task_controller.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_mascot_overlay.h"
#include "chrome/browser/zephyrus/agent/agent_memory.h"
#include "chrome/browser/zephyrus/agent/mascot_rig.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_handler.h"
#include "ui/views/view.h"

class BrowserView;

namespace os_crypt_async {
class Encryptor;
}

namespace views {
class BoxLayoutView;
class ImageButton;
class ImageView;
class Label;
class LabelButton;
class ProgressBar;
class ScrollView;
class Textfield;
}  // namespace views

namespace aura {
class Window;
}

namespace zephyrus::agent {

class JumpToLatestButton;
class VoiceInput;
class ZephyrusHandsFree;
class ZephyrusMascotBubble;

// Where a task lives while it runs.
//
// A right-hand column, mirroring the workspaces sidebar on the left -- an M3
// STANDARD SIDE SHEET: docked beside the page rather than over it, so the page
// the agent is working on stays in view. It exists because a task is not a
// one-shot command: it takes several steps, may stop to ask permission, and
// ends with an answer. A prompt box can start one; only something persistent
// can show one.
//
// The running log is built from what the BROWSER sees, not from a progress
// channel through the kernel. The browser performs every step, so it already
// knows about each one -- a second source of truth for the same events would be
// one more thing to disagree.
class ZephyrusAgentPanel : public views::View,
                           public ZephyrusAgentTaskController::Delegate,
                           public views::TextfieldController {
  METADATA_HEADER(ZephyrusAgentPanel, views::View)

 public:
  static constexpr int kDefaultWidth = 340;
  static constexpr int kMinWidth = 260;

  explicit ZephyrusAgentPanel(BrowserView* browser_view);
  ~ZephyrusAgentPanel() override;

  ZephyrusAgentPanel(const ZephyrusAgentPanel&) = delete;
  ZephyrusAgentPanel& operator=(const ZephyrusAgentPanel&) = delete;

  bool is_open() const { return is_open_; }

  // The width the layout should reserve: the panel's width when open, zero
  // when not. The page gets the space back the moment the panel closes.
  int GetReservedWidth() const;

  void Toggle();
  void Open();

  // Where the model settings bubble anchors. Test-facing as well: the capture
  // harness opens the bubble against it.
  views::View* settings_button();
  // The microphone-and-gear button beside it: the "Hey Zep" popup opens here.
  views::View* voice_button();

  // Runs `task` as if typed into the panel. Dev hook only
  // (--zephyrus-test-agent-task); see BrowserView.
  // Several messages, separated by `|||`, are sent one after another, each once
  // the last has finished: a chat, to exercise memory end to end.
  void StartTaskForTesting(const std::string& task);
  void Close();

  // Starts over: forgets this chat (what was said, so a follow-up no longer
  // refers to it) and clears the log. What the user asked the agent to remember
  // for good is not touched -- that is its own setting.
  void NewChat();

  // Answers the question on screen as the buttons would. Test-facing: it
  // replaces the click, not the decision.
  void AnswerApprovalForTesting(bool approved) { AnswerApproval(approved); }

  bool HasQuestionForTesting() const { return !approval_answer_.is_null(); }

  // The running log, so a test can check its lines are actually laid out
  // inside the panel. Two attempts at a ScrollView here rendered NOTHING, and
  // neither was caught by anything but a person looking at the window.
  const views::BoxLayoutView* log_for_testing() const { return log_; }
  views::ScrollView* log_scroll_for_testing() { return log_scroll_; }
  bool IsJumpToLatestVisibleForTesting() const;
  void JumpToLatestForTesting() { ScrollLogToLatest(); }

  // ZephyrusAgentTaskController::Delegate:
  void OnAgentProgress(const std::string& line) override;
  void OnAgentApprovalNeeded(const std::string& reason,
                             const std::string& risk,
                             const std::string& tool,
                             base::OnceCallback<void(bool)> answer) override;
  void OnAgentActivity(Activity activity, const std::string& tool) override;

  // The empty slot the mascot lives in, and who draws it.
  PointerObserver* GetPointerObserver() override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // Submits whatever is in the input. Enter and the send button both land
  // here, so neither is the only way in.
  void Submit();

  // What "Hey Zep" reports, in the order it happens. All of it shows on the
  // mascot, so a person who never opens this panel sees everything.
  //   OnVoiceWake: the phrase was accepted (`who` is the voice's name).
  //   OnVoiceTranscribing: the command was heard and is being turned to text.
  //   OnVoiceHeard: the command, as text. It becomes an ordinary task -- or, if
  //     it is "stop" while one runs, ends it.
  //   OnVoiceNotice: something the person should know (no key, no microphone).
  //   OnVoiceIdle: the phrase was heard and nothing followed.
  void OnVoiceWake(const std::string& who);
  void OnVoiceTranscribing();
  void OnVoiceHeard(const std::string& text);
  void OnVoiceNotice(const std::string& text);
  void OnVoiceIdle();

  // The hands-free listener of this window; null before the window has a
  // widget.
  ZephyrusHandsFree* hands_free() { return hands_free_.get(); }

  // views::View:
  void OnThemeChanged() override;
  void Layout(PassKey) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;

  // ui::AcceleratorTarget:
  //   Ctrl+Shift+Space: HOLD to talk, let go to send (see KeyWatcher).
  //   Ctrl+Shift+Comma: mute or unmute "Hey Zep" for this window.
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

  // What a line in the log is, which decides how it is drawn.
  enum class LineKind {
    // What the user asked for: a trailing bubble, the way a sent message sits.
    kTask,
    // A step the browser took, or anything else the agent says on the way.
    // Quiet on purpose: a log that shouts every step is a log nobody reads.
    kStep,
    // The answer, or the question the agent stopped to ask. The one line the
    // user came for, so it gets a card.
    kAnswer,
  };

 private:
  void StartTask(const std::string& task);
  // A request to move the mascot, typed or spoken. True when `text` was one
  // and has been dealt with; false leaves it to be a task.
  bool HandleMascotCommand(const std::string& text);
  void OnTaskFinished(mojom::TaskOutcomePtr outcome);

  void AddLine(const std::string& text, LineKind kind);

  // Flips everything that depends on whether a task is running: the progress
  // indicator, the send/stop button, and the input.
  void SetTaskRunning(bool running);

  // The send button's one callback: stop when a task is running, send when not.
  void OnSendOrStop();

  // The exchange that just ended, kept so the next message can refer to it.
  void RecordTurn(const mojom::TaskOutcome& outcome);

  void ShowApprovalCard(const std::string& reason,
                        const std::string& tool,
                        base::OnceCallback<void(bool)> answer);
  void ClearApprovalCard();
  void AnswerApproval(bool approved);

  void ApplyRoles();

  // Puts the mascot in the state that says what the agent is doing.
  void ShowMascot(MascotMood mood,
                  ZephyrusAgentMascotOverlay::Place place =
                      ZephyrusAgentMascotOverlay::Place::kStay,
                  base::TimeDelta budget = base::TimeDelta::Max());
  ZephyrusAgentMascotOverlay* Overlay() const;
  // What the agent says above the mascot; null where there is no mascot.
  ZephyrusMascotBubble* Bubble() const;
  void SayStatus(const std::string& text);
  void SayAnswer(const std::string& text);

  // How long the agent's actions of each kind usually take, learned as it goes:
  // the mascot walks to the address bar only if it can be there before the
  // navigation is over. See ZephyrusAgentMascotOverlay::GoTo.
  base::TimeDelta typical_navigation_ = base::Milliseconds(1500);
  base::TimeDelta typical_tab_action_ = base::Milliseconds(350);
  base::TimeTicks acting_since_;

  // Voice commands. The mic button: press to talk, press again to send. The
  // keys: HOLD Ctrl+Shift+Space while speaking, let go to send -- no "Hey Zep"
  // needed.
  void OnMic();
  // Hold to talk. Key presses cannot say when they were let go of through an
  // accelerator, so the window's key events are watched before the page sees
  // them (the same way the mascot watches the mouse).
  class KeyWatcher : public ui::EventHandler {
   public:
    explicit KeyWatcher(ZephyrusAgentPanel* panel) : panel_(panel) {}
    void OnKeyEvent(ui::KeyEvent* event) override;

   private:
    const raw_ptr<ZephyrusAgentPanel> panel_;
  };
  void HandleKey(ui::KeyEvent* event);
  void BeginHoldToTalk(base::TimeTicks at);
  void EndHoldToTalk(base::TimeTicks at);
  void OnHoldTimeout();
  // Stop and send (or, for a tap too short to have said anything, throw away).
  void FinishHold(bool tap);
  void CancelVoice();
  KeyWatcher key_watcher_{this};
  raw_ptr<aura::Window> watched_window_ = nullptr;
  bool hold_active_ = false;
  // Let go before the microphone had finished opening.
  bool release_pending_ = false;
  bool release_was_tap_ = false;
  base::TimeTicks hold_started_;
  base::OneShotTimer hold_timeout_;
  void OnVoiceKey(scoped_refptr<os_crypt_async::Encryptor> encryptor);
  void OnTranscript(bool ok, const std::string& text);

  // The log follows new lines only while the user is already at its end --
  // the way a chat does. Scrolled back to read something, they stay where they
  // are, and a jump-to-latest button appears instead.
  bool IsLogAtBottom() const;
  void ScrollLogToLatest();
  void OnLogScrolled();
  void UpdateJumpButton();

  const raw_ptr<BrowserView> browser_view_;

  bool is_open_ = false;
  bool task_running_ = false;

  raw_ptr<views::ImageView> header_icon_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageButton> settings_button_ = nullptr;
  raw_ptr<views::ImageButton> voice_button_ = nullptr;
  raw_ptr<views::ImageButton> new_chat_button_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  raw_ptr<views::ProgressBar> progress_ = nullptr;
  raw_ptr<views::View> empty_state_ = nullptr;
  raw_ptr<views::ImageView> empty_icon_ = nullptr;
  raw_ptr<views::Label> empty_label_ = nullptr;
  raw_ptr<views::ScrollView> log_scroll_ = nullptr;
  raw_ptr<views::BoxLayoutView> log_ = nullptr;
  raw_ptr<views::BoxLayoutView> approval_ = nullptr;
  raw_ptr<views::ImageView> approval_icon_ = nullptr;
  raw_ptr<views::Label> approval_title_ = nullptr;
  raw_ptr<views::Label> approval_reason_ = nullptr;
  raw_ptr<views::LabelButton> allow_button_ = nullptr;
  raw_ptr<views::LabelButton> decline_button_ = nullptr;
  raw_ptr<views::View> input_bar_ = nullptr;
  raw_ptr<views::Textfield> input_ = nullptr;
  raw_ptr<views::ImageButton> mic_button_ = nullptr;
  raw_ptr<views::ImageButton> send_button_ = nullptr;
  raw_ptr<JumpToLatestButton> jump_button_ = nullptr;
  raw_ptr<views::Label> mascot_caption_ = nullptr;

  // What this window remembers of the conversation, per workspace: a workspace
  // is a separate context (ADR 0003), so what was said in one is not carried
  // into another. In memory only; the long-term kind is the profile's.
  std::map<int, ConversationMemory> chats_;
  // The message being run and the task it ran as -- see ChatTurn -- kept until
  // the task ends and becomes a turn.
  std::deque<std::string> test_queue_;
  std::string current_message_;
  std::string current_task_;
  int current_workspace_ = 0;
  // The starter tasks shown before anything has been asked.
  std::vector<raw_ptr<views::LabelButton>> chips_;
  base::TimeTicks task_started_;

  // Lines added while the user was scrolled away from the end.
  int unread_ = 0;
  base::CallbackListSubscription log_scrolled_subscription_;

  // Non-null only while a question is on screen. Run exactly once.
  base::OnceCallback<void(bool)> approval_answer_;

  std::unique_ptr<ZephyrusAgentTaskController> controller_;

  std::unique_ptr<ZephyrusHandsFree> hands_free_;
  std::unique_ptr<VoiceInput> voice_;
  // Held only while recording, then handed to the request and dropped.
  std::string voice_key_;
  std::string voice_endpoint_;
  bool listening_ = false;
  bool transcribing_ = false;
  base::WeakPtrFactory<ZephyrusAgentPanel> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_PANEL_H_
