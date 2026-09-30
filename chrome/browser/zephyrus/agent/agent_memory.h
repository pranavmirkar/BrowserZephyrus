// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_AGENT_MEMORY_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_AGENT_MEMORY_H_

#include <stddef.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/important_file_writer.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "base/time/time.h"
#include "chrome/services/zephyrus_agent/public/mojom/agent_kernel.mojom.h"

namespace zephyrus::agent {

// What the agent remembers, in three layers. See ADR 0006.
//
//   working memory   the task in progress: its steps, its notes, the page it
//                    last read. Lives in the kernel's TaskLoop and is gone when
//                    the task is.
//   the conversation this chat: what was said and how each task ended, so a
//                    follow-up is understood as a follow-up. Lives here, in
//                    memory only, per workspace.
//   long-term        things the user asked it to keep. Persisted, per workspace,
//                    and the only memory that outlives the browser.
//
// Everything in this file runs in the browser process, which owns the user's
// data. The kernel process is handed a copy of what it needs for one task
// (mojom::TaskMemory) and can neither read the rest nor write any of it: what it
// wants kept goes through the memory.remember tool, and the tool's rules are the
// kernel's (policy.rs) while its effect is here.

// Masks what should not be carried forward in text: a verification code, a
// password after the word "password", any run of digits long enough to be a
// card or an ID. Over-matches on purpose -- a hidden harmless number costs the
// next task a little context, and a kept code costs the user something.
std::string MaskSecrets(std::string_view text);

// One exchange in a chat, as it is kept.
struct ChatTurn {
  ChatTurn();
  ChatTurn(const ChatTurn&);
  ChatTurn(ChatTurn&&);
  ChatTurn& operator=(const ChatTurn&);
  ChatTurn& operator=(ChatTurn&&);
  ~ChatTurn();

  // What the user said, verbatim.
  std::string user;
  // The whole task as it was run: the same as `user`, except when this message
  // answered a question, when it is what was first asked plus the answer. What
  // a follow-up "carries on" with.
  std::string task;
  // "done", "asked", "stopped" or "failed".
  std::string outcome;
  // What the agent answered or asked. Page-derived, so DATA; secrets masked.
  std::string agent;
  // Where the page was when it ended.
  std::string url;
  // The notes the agent had written, for a follow-up to start from.
  std::string notes;
};

// This chat, in memory. Bounded, and compacted rather than truncated: the most
// recent turns are kept whole and older ones shrink to a line, so a long chat
// still remembers that something happened without the prompt growing with it.
class ConversationMemory {
 public:
  static constexpr size_t kMaxTurns = 24;
  // Turns shown whole; the rest are one line each.
  static constexpr size_t kWholeTurns = 5;

  ConversationMemory();
  ConversationMemory(const ConversationMemory&);
  ConversationMemory& operator=(const ConversationMemory&);
  ~ConversationMemory();

  void Record(ChatTurn turn);
  void Clear() { turns_.clear(); }
  bool empty() const { return turns_.empty(); }
  size_t size() const { return turns_.size(); }
  const ChatTurn* Last() const;

  // The last thing the agent did was ask a question, so the next message is
  // very probably its answer.
  bool AwaitingAnswer() const;

  // What to hand a task: the chat so far, whether this message answers a
  // question, and the notes to start from. `facts` is added by the caller.
  mojom::TaskMemoryPtr Snapshot() const;

 private:
  std::vector<ChatTurn> turns_;
};

// What the user asked it to keep.
class LongTermMemory : public base::SupportsUserData::Data,
                       public base::ImportantFileWriter::DataSerializer {
 public:
  static constexpr size_t kMaxFactsPerWorkspace = 200;
  static constexpr size_t kMaxFactLength = 300;

  // `path` empty means memory-only, which is what tests and a profile without a
  // directory get.
  explicit LongTermMemory(base::FilePath path);
  ~LongTermMemory() override;

  // Keeps `text` for `workspace`. `persist` is false for a workspace that must
  // leave nothing behind (Private): the fact is kept for this session and never
  // written. A fact that says what an existing one says refreshes it instead of
  // being kept twice. Returns false for something not worth keeping (empty).
  bool Add(const std::string& text, int workspace, bool persist);

  // Forgets what matches `query` (at least most of its words) in `workspace`.
  // Returns how many were forgotten.
  int Forget(const std::string& query, int workspace);

  // The facts most relevant to `context` (the task and the chat), best first,
  // within `max_chars`. Marks them used.
  std::vector<std::string> Recall(const std::string& context,
                                  int workspace,
                                  size_t max_chars = 1500);

  size_t Count(int workspace) const;
  // Everything kept for `workspace`, newest first, for showing to the user.
  std::vector<std::string> All(int workspace) const;
  void Clear(int workspace);
  // Everything, in every workspace. "Forget everything."
  void ClearAll();

  // Loading is asynchronous; until it finishes there are no facts, which is the
  // safe way to be wrong.
  bool loaded() const { return loaded_; }

  // base::ImportantFileWriter::DataSerializer:
  std::optional<std::string> SerializeData() override;

 private:
  struct Fact {
    std::string text;
    int workspace = 0;
    base::Time created;
    base::Time last_used;
    int uses = 0;
    // Never written to disk.
    bool ephemeral = false;
  };

  void Load();
  void OnLoaded(std::optional<std::string> contents);
  void Save();

  base::FilePath path_;
  std::unique_ptr<base::ImportantFileWriter> writer_;
  std::vector<Fact> facts_;
  bool loaded_ = false;
  base::WeakPtrFactory<LongTermMemory> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_AGENT_MEMORY_H_
