// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/agent_memory.h"

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

// ---- Secrets ----------------------------------------------------------------

TEST(MaskSecretsTest, ACodeNamedByTheWordBeforeItIsHidden) {
  // The real case from a trace: the agent read a verification code out of an
  // email and would have carried it into the next task.
  const std::string masked = MaskSecrets(
      "Your latest email is \"Confirm your email, your verification code: "
      "150608\" from Text App");
  EXPECT_EQ(masked.find("150608"), std::string::npos) << masked;
  EXPECT_NE(masked.find("Confirm your email"), std::string::npos)
      << "the rest must survive: " << masked;
  EXPECT_EQ(MaskSecrets("My PIN is 4821").find("4821"), std::string::npos);
  EXPECT_EQ(MaskSecrets("OTP 482913 expires soon").find("482913"),
            std::string::npos);
  EXPECT_EQ(MaskSecrets("password hunter2").find("hunter2"), std::string::npos);
}

TEST(MaskSecretsTest, ALongNumberIsHiddenWhateverItIsCalled) {
  EXPECT_EQ(MaskSecrets("card 4111 1111 1111 1111 expires 12/28").find("4111"),
            std::string::npos);
  EXPECT_EQ(MaskSecrets("acct 123456789012345").find("1234567890"),
            std::string::npos);
}

TEST(MaskSecretsTest, OrdinaryNumbersAndWordsAreLeftAlone) {
  const std::string text =
      "The Nifty 50 fell 3.28% to 22,716 over 8 sessions; the code review "
      "took 2 hours.";
  EXPECT_EQ(MaskSecrets(text), text);
  // "encode" and "decoder" are not "code".
  EXPECT_EQ(MaskSecrets("encode 1234 decoder 5678"), "encode 1234 decoder 5678");
}

// ---- The conversation ----------------------------------------------------------

ChatTurn Turn(const std::string& user,
              const std::string& outcome,
              const std::string& agent) {
  ChatTurn turn;
  turn.user = user;
  turn.outcome = outcome;
  turn.agent = agent;
  return turn;
}

TEST(ConversationMemoryTest, AFollowUpIsHandedTheChatSoFar) {
  ConversationMemory chat;
  chat.Record(Turn("make a prototype in figma", "asked",
                   "Should I build a new frame or link the existing ones?"));
  ASSERT_TRUE(chat.AwaitingAnswer());
  mojom::TaskMemoryPtr memory = chat.Snapshot();
  ASSERT_EQ(memory->conversation.size(), 1u);
  EXPECT_EQ(memory->conversation[0]->user, "make a prototype in figma");
  EXPECT_EQ(memory->conversation[0]->outcome, "asked");
  // The reply to a question is the same task going on.
  EXPECT_EQ(memory->continuing, "make a prototype in figma");
}

TEST(ConversationMemoryTest, ACompletedTaskIsNotAQuestionAndNothingContinues) {
  ConversationMemory chat;
  chat.Record(Turn("summarize this page", "done", "It is about tea."));
  EXPECT_FALSE(chat.AwaitingAnswer());
  EXPECT_TRUE(chat.Snapshot()->continuing.empty());
}

TEST(ConversationMemoryTest, AnAnswerCarriesTheWholeTaskForward) {
  ConversationMemory chat;
  ChatTurn asked = Turn("make a prototype in figma", "asked", "Which one?");
  chat.Record(asked);
  // The answer is a turn of its own; the task it ran as is the question's
  // task plus the answer, which is what a second question would continue.
  ChatTurn answered = Turn("a new browser window", "asked", "New tab or window?");
  answered.task = "make a prototype in figma (the user's answer: a new browser window)";
  chat.Record(answered);
  EXPECT_NE(chat.Snapshot()->continuing.find("make a prototype in figma"),
            std::string::npos);
}

TEST(ConversationMemoryTest, NotesSurviveToTheNextTask) {
  ConversationMemory chat;
  ChatTurn turn = Turn("research headphones", "done", "Here is what I found.");
  turn.notes = "Source 1: rtings.com - ANC uses microphones";
  chat.Record(turn);
  EXPECT_EQ(chat.Snapshot()->notes, "Source 1: rtings.com - ANC uses microphones");
}

TEST(ConversationMemoryTest, OldTurnsShrinkAndAreEventuallyDropped) {
  ConversationMemory chat;
  const std::string long_reply(900, 'x');
  for (int i = 0; i < 40; ++i) {
    chat.Record(Turn("task number " + std::to_string(i), "done", long_reply));
  }
  EXPECT_EQ(chat.size(), ConversationMemory::kMaxTurns);
  mojom::TaskMemoryPtr memory = chat.Snapshot();
  ASSERT_EQ(memory->conversation.size(), ConversationMemory::kMaxTurns);
  // The oldest kept is a gist; the newest is whole.
  EXPECT_LT(memory->conversation.front()->agent.size(), 120u);
  EXPECT_GT(memory->conversation.back()->agent.size(), 400u);
  EXPECT_EQ(memory->conversation.back()->user, "task number 39");
}

TEST(ConversationMemoryTest, WhatIsKeptIsMaskedAndLosesItsQueryString) {
  ConversationMemory chat;
  ChatTurn turn =
      Turn("check my mail", "done", "Your verification code: 150608 arrived");
  turn.url = "https://mail.example.com/inbox?token=abc123&u=1#msg";
  chat.Record(turn);
  const ChatTurn* kept = chat.Last();
  ASSERT_TRUE(kept);
  EXPECT_EQ(kept->agent.find("150608"), std::string::npos);
  EXPECT_EQ(kept->url, "https://mail.example.com/inbox");
}

TEST(ConversationMemoryTest, ClearingForgetsTheChat) {
  ConversationMemory chat;
  chat.Record(Turn("hi", "done", "hello"));
  chat.Clear();
  EXPECT_TRUE(chat.empty());
  EXPECT_FALSE(chat.AwaitingAnswer());
  EXPECT_TRUE(chat.Snapshot()->conversation.empty());
}

// ---- Long-term -------------------------------------------------------------------

TEST(LongTermMemoryTest, KeepsWhatItIsToldAndRecallsWhatIsRelevant) {
  LongTermMemory memory{base::FilePath()};
  ASSERT_TRUE(memory.Add("The user shops on Amazon.in", 0, /*persist=*/true));
  ASSERT_TRUE(memory.Add("The user's daughter is Anaya", 0, true));
  for (int i = 0; i < 12; ++i) {
    memory.Add("Unrelated preference number " + std::to_string(i) + " zebra", 0, true);
  }
  const std::vector<std::string> recalled =
      memory.Recall("find a birthday gift for Anaya on amazon", 0);
  ASSERT_FALSE(recalled.empty());
  // Relevant ones first.
  EXPECT_TRUE(recalled[0].find("Anaya") != std::string::npos ||
              recalled[0].find("Amazon") != std::string::npos)
      << recalled[0];
}

TEST(LongTermMemoryTest, SayingTheSameThingTwiceKeepsItOnce) {
  LongTermMemory memory{base::FilePath()};
  memory.Add("The user shops on Amazon.in", 0, true);
  memory.Add("the user shops on amazon.in", 0, true);
  memory.Add("User shops on Amazon.in!", 0, true);
  EXPECT_EQ(memory.Count(0), 1u);
}

TEST(LongTermMemoryTest, WorkspacesDoNotShareWhatTheyKnow) {
  // ADR 0003: a task is bound to one workspace, and so is what it learns.
  LongTermMemory memory{base::FilePath()};
  memory.Add("The user works at Acme Corp", 1, true);
  EXPECT_EQ(memory.Count(1), 1u);
  EXPECT_EQ(memory.Count(2), 0u);
  EXPECT_TRUE(memory.Recall("where do I work at Acme", 2).empty());
  EXPECT_EQ(memory.Recall("where do I work at Acme", 1).size(), 1u);
}

TEST(LongTermMemoryTest, ForgettingRemovesWhatWasAskedAndNotThingsAround) {
  LongTermMemory memory{base::FilePath()};
  memory.Add("The user shops on Amazon.in", 0, true);
  memory.Add("The user shops for gifts every March", 0, true);
  memory.Add("The user prefers dark mode", 0, true);
  EXPECT_EQ(memory.Forget("shops on Amazon.in", 0), 1);
  EXPECT_EQ(memory.Count(0), 2u);
  EXPECT_EQ(memory.Forget("something never said", 0), 0);
}

TEST(LongTermMemoryTest, TheOldestGoesWhenFull) {
  LongTermMemory memory{base::FilePath()};
  for (size_t i = 0; i < LongTermMemory::kMaxFactsPerWorkspace + 25; ++i) {
    memory.Add("fact " + std::string(1, static_cast<char>('a' + i % 26)) +
                   std::to_string(i) + " unique" + std::to_string(i * 7919),
               0, true);
  }
  EXPECT_EQ(memory.Count(0), LongTermMemory::kMaxFactsPerWorkspace);
}

TEST(LongTermMemoryTest, AFactTooLongOrEmptyIsNotKept) {
  LongTermMemory memory{base::FilePath()};
  EXPECT_FALSE(memory.Add("   ", 0, true));
  EXPECT_FALSE(memory.Add(std::string(400, 'a'), 0, true));
  EXPECT_EQ(memory.Count(0), 0u);
}

TEST(LongTermMemoryTest, ItSurvivesARestart) {
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath path = dir.GetPath().AppendASCII("memory.json");
  {
    LongTermMemory memory(path);
    task_environment.RunUntilIdle();
    ASSERT_TRUE(memory.Add("The user shops on Amazon.in", 3, true));
    // Written when it is destroyed, at the latest.
  }
  task_environment.RunUntilIdle();
  LongTermMemory reopened(path);
  // Loading is asynchronous, and the pool needs to run.
  for (int i = 0; i < 100 && !reopened.loaded(); ++i) {
    task_environment.RunUntilIdle();
    base::PlatformThread::Sleep(base::Milliseconds(10));
  }
  ASSERT_TRUE(reopened.loaded());
  EXPECT_EQ(reopened.Count(3), 1u);
  EXPECT_EQ(reopened.All(3)[0], "The user shops on Amazon.in");
}

TEST(LongTermMemoryTest, AWorkspaceThatLeavesNothingBehindNeverWritesToDisk) {
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath path = dir.GetPath().AppendASCII("memory.json");
  {
    LongTermMemory memory(path);
    task_environment.RunUntilIdle();
    // Private Workspace: kept for the session, so a follow-up works, and never
    // persisted.
    ASSERT_TRUE(memory.Add("The user is planning a surprise party", 9, /*persist=*/false));
    EXPECT_EQ(memory.Count(9), 1u);
    memory.Add("The user prefers dark mode", 0, true);
  }
  task_environment.RunUntilIdle();
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  EXPECT_EQ(contents.find("surprise"), std::string::npos) << contents;
  EXPECT_NE(contents.find("dark mode"), std::string::npos);
}

TEST(LongTermMemoryTest, ClearingOneWorkspaceLeavesTheOthers) {
  LongTermMemory memory{base::FilePath()};
  memory.Add("The user likes tea", 1, true);
  memory.Add("The user likes coffee", 2, true);
  memory.Clear(1);
  EXPECT_EQ(memory.Count(1), 0u);
  EXPECT_EQ(memory.Count(2), 1u);
  memory.ClearAll();
  EXPECT_EQ(memory.Count(2), 0u);
}

}  // namespace
}  // namespace zephyrus::agent
