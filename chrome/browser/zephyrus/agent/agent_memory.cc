// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/agent_memory.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/values.h"

namespace zephyrus::agent {

namespace {

bool IsDigit(char c) {
  return c >= '0' && c <= '9';
}

bool IsWordChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

// The first `limit` characters, cut at a character boundary, with an ellipsis
// when something was cut.
std::string Bounded(std::string_view text, size_t limit) {
  if (text.size() <= limit) {
    return std::string(text);
  }
  size_t end = limit;
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
    --end;
  }
  return std::string(text.substr(0, end)) + "...";
}

// An address without its query or fragment: where the page was, not what the
// address was carrying.
std::string StripQuery(std::string_view url) {
  const size_t cut = url.find_first_of("?#");
  return std::string(cut == std::string_view::npos ? url : url.substr(0, cut));
}

// The words that identify a fact: lowercase, letters and digits, three or more
// of them, cut to five letters so "prefers" meets "prefer". Not a search
// engine; a way to tell whether two short sentences are about the same thing.
std::set<std::string> Tokens(std::string_view text) {
  std::set<std::string> out;
  std::string word;
  const auto flush = [&] {
    if (word.size() >= 3) {
      out.insert(word.substr(0, 5));
    }
    word.clear();
  };
  for (char c : text) {
    if (IsWordChar(c)) {
      word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    } else {
      flush();
    }
  }
  flush();
  // Words that carry nothing about WHAT a fact is.
  for (const char* noise : {"the", "user", "users", "and", "for", "that", "with",
                            "his", "her", "their", "they", "has", "have", "are"}) {
    out.erase(std::string(noise).substr(0, 5));
  }
  return out;
}

size_t Overlap(const std::set<std::string>& a, const std::set<std::string>& b) {
  size_t shared = 0;
  for (const std::string& token : a) {
    shared += b.count(token);
  }
  return shared;
}

std::string Normalise(std::string_view text) {
  std::string out;
  for (char c : text) {
    if (IsWordChar(c)) {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
  }
  return out;
}

}  // namespace

std::string MaskSecrets(std::string_view text) {
  std::string out(text);
  // Spans to replace, found first and applied last-to-first so the positions
  // stay true.
  std::vector<std::pair<size_t, size_t>> spans;

  // 1. Long numbers: a card, an account, an ID. Spaces and dashes inside a
  //    number do not break it, because that is how they are written.
  for (size_t i = 0; i < out.size();) {
    if (!IsDigit(out[i])) {
      ++i;
      continue;
    }
    size_t j = i;
    int digits = 0;
    while (j < out.size() &&
           (IsDigit(out[j]) || ((out[j] == ' ' || out[j] == '-') &&
                                j + 1 < out.size() && IsDigit(out[j + 1]) &&
                                digits > 0))) {
      if (IsDigit(out[j])) {
        ++digits;
      }
      ++j;
    }
    if (digits >= 12) {
      spans.emplace_back(i, j);
    }
    i = j;
  }

  // 2. A secret named by the word before it: "verification code: 150608",
  //    "password hunter2", "PIN is 4821".
  const std::string lower = base::ToLowerASCII(out);
  struct Keyword {
    const char* word;
    bool needs_digit;
  };
  for (const Keyword& keyword :
       {Keyword{"password", false}, Keyword{"passcode", false},
        Keyword{"passwd", false}, Keyword{"otp", true}, Keyword{"pin", true},
        Keyword{"code", true}, Keyword{"cvv", true}, Keyword{"token", true}}) {
    const std::string_view word(keyword.word);
    for (size_t at = lower.find(word); at != std::string::npos;
         at = lower.find(word, at + 1)) {
      const bool starts = at == 0 || !IsWordChar(lower[at - 1]);
      const size_t after = at + word.size();
      if (!starts || (after < lower.size() && IsWordChar(lower[after]))) {
        continue;
      }
      // The first token within reach.
      const size_t limit = std::min(lower.size(), after + 24);
      for (size_t i = after; i < limit;) {
        if (!IsWordChar(lower[i])) {
          ++i;
          continue;
        }
        size_t j = i;
        bool has_digit = false;
        while (j < lower.size() && IsWordChar(lower[j])) {
          has_digit |= IsDigit(lower[j]);
          ++j;
        }
        const bool filler = j - i < 4 || lower.substr(i, j - i) == "this" ||
                            lower.substr(i, j - i) == "that";
        if (!filler && (has_digit || !keyword.needs_digit)) {
          spans.emplace_back(i, j);
          break;
        }
        i = j;
      }
    }
  }

  std::sort(spans.begin(), spans.end());
  // Merge overlaps, then replace from the end.
  std::vector<std::pair<size_t, size_t>> merged;
  for (const auto& span : spans) {
    if (!merged.empty() && span.first <= merged.back().second) {
      merged.back().second = std::max(merged.back().second, span.second);
    } else {
      merged.push_back(span);
    }
  }
  for (auto it = merged.rbegin(); it != merged.rend(); ++it) {
    out.replace(it->first, it->second - it->first, "[hidden]");
  }
  return out;
}

// ---- The conversation ---------------------------------------------------------

ChatTurn::ChatTurn() = default;
ChatTurn::ChatTurn(const ChatTurn&) = default;
ChatTurn::ChatTurn(ChatTurn&&) = default;
ChatTurn& ChatTurn::operator=(const ChatTurn&) = default;
ChatTurn& ChatTurn::operator=(ChatTurn&&) = default;
ChatTurn::~ChatTurn() = default;

ConversationMemory::ConversationMemory() = default;
ConversationMemory::ConversationMemory(const ConversationMemory&) = default;
ConversationMemory& ConversationMemory::operator=(const ConversationMemory&) = default;
ConversationMemory::~ConversationMemory() = default;

void ConversationMemory::Record(ChatTurn turn) {
  turn.user = Bounded(turn.user, 600);
  turn.task = Bounded(turn.task.empty() ? turn.user : turn.task, 900);
  turn.outcome = Bounded(turn.outcome, 16);
  turn.agent = MaskSecrets(Bounded(turn.agent, 800));
  turn.url = Bounded(StripQuery(turn.url), 300);
  turn.notes = MaskSecrets(Bounded(turn.notes, 2000));
  turns_.push_back(std::move(turn));
  if (turns_.size() > kMaxTurns) {
    turns_.erase(turns_.begin(),
                 turns_.begin() + static_cast<ptrdiff_t>(turns_.size() - kMaxTurns));
  }
}

const ChatTurn* ConversationMemory::Last() const {
  return turns_.empty() ? nullptr : &turns_.back();
}

bool ConversationMemory::AwaitingAnswer() const {
  return !turns_.empty() && turns_.back().outcome == "asked";
}

mojom::TaskMemoryPtr ConversationMemory::Snapshot() const {
  auto memory = mojom::TaskMemory::New();
  const size_t whole_from =
      turns_.size() > kWholeTurns ? turns_.size() - kWholeTurns : 0;
  for (size_t i = 0; i < turns_.size(); ++i) {
    const ChatTurn& turn = turns_[i];
    auto out = mojom::ConversationTurn::New();
    const bool whole = i >= whole_from;
    // Older turns shrink to the gist: what was asked, how it ended.
    out->user = Bounded(turn.user, whole ? 400 : 80);
    out->outcome = turn.outcome;
    out->agent = Bounded(turn.agent, whole ? 500 : 90);
    out->url = whole ? turn.url : std::string();
    memory->conversation.push_back(std::move(out));
  }
  if (!turns_.empty()) {
    // The last message was a question, so this one is its answer: the task goes
    // on rather than starting over.
    if (AwaitingAnswer()) {
      memory->continuing = turns_.back().task;
    }
    memory->notes = turns_.back().notes;
  }
  return memory;
}

// ---- Long-term ---------------------------------------------------------------------

LongTermMemory::LongTermMemory(base::FilePath path) : path_(std::move(path)) {
  if (path_.empty()) {
    loaded_ = true;
    return;
  }
  writer_ = std::make_unique<base::ImportantFileWriter>(
      path_,
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN}),
      "ZephyrusAgentMemory");
  Load();
}

LongTermMemory::~LongTermMemory() {
  if (writer_ && writer_->HasPendingWrite()) {
    writer_->DoScheduledWrite();
  }
}

void LongTermMemory::Load() {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(
          [](base::FilePath path) -> std::optional<std::string> {
            std::string contents;
            if (!base::ReadFileToStringWithMaxSize(path, &contents, 1 << 20)) {
              return std::nullopt;
            }
            return contents;
          },
          path_),
      base::BindOnce(&LongTermMemory::OnLoaded, weak_factory_.GetWeakPtr()));
}

void LongTermMemory::OnLoaded(std::optional<std::string> contents) {
  loaded_ = true;
  if (!contents) {
    return;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(*contents, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  const base::ListValue* list = parsed->GetDict().FindList("facts");
  if (!list) {
    return;
  }
  std::vector<Fact> loaded;
  for (const base::Value& entry : *list) {
    const base::DictValue* dict = entry.GetIfDict();
    const std::string* text = dict ? dict->FindString("t") : nullptr;
    if (!text || text->empty() || text->size() > kMaxFactLength) {
      continue;
    }
    Fact fact;
    fact.text = *text;
    fact.workspace = dict->FindInt("w").value_or(0);
    fact.uses = dict->FindInt("n").value_or(0);
    fact.created = base::Time::FromSecondsSinceUnixEpoch(
        dict->FindDouble("c").value_or(0));
    fact.last_used = base::Time::FromSecondsSinceUnixEpoch(
        dict->FindDouble("u").value_or(0));
    loaded.push_back(std::move(fact));
  }
  // What was said this session, before the file arrived, stays and comes last.
  loaded.insert(loaded.end(), std::make_move_iterator(facts_.begin()),
                std::make_move_iterator(facts_.end()));
  facts_ = std::move(loaded);
}

std::optional<std::string> LongTermMemory::SerializeData() {
  base::ListValue list;
  for (const Fact& fact : facts_) {
    if (fact.ephemeral) {
      continue;
    }
    base::DictValue dict;
    dict.Set("t", fact.text);
    dict.Set("w", fact.workspace);
    dict.Set("n", fact.uses);
    dict.Set("c", fact.created.InSecondsFSinceUnixEpoch());
    dict.Set("u", fact.last_used.InSecondsFSinceUnixEpoch());
    list.Append(std::move(dict));
  }
  base::DictValue root;
  root.Set("version", 1);
  root.Set("facts", std::move(list));
  return base::WriteJson(root);
}

void LongTermMemory::Save() {
  if (writer_) {
    writer_->ScheduleWrite(this);
  }
}

bool LongTermMemory::Add(const std::string& text, int workspace, bool persist) {
  std::string clean(base::TrimWhitespaceASCII(text, base::TRIM_ALL));
  if (clean.empty() || clean.size() > kMaxFactLength) {
    return false;
  }
  const std::set<std::string> tokens = Tokens(clean);
  const base::Time now = base::Time::Now();

  // The same thing said again refreshes the old one rather than being kept
  // twice: nearly every word shared, in both directions.
  for (Fact& existing : facts_) {
    if (existing.workspace != workspace) {
      continue;
    }
    const std::set<std::string> other = Tokens(existing.text);
    const size_t shared = Overlap(tokens, other);
    const size_t larger = std::max(tokens.size(), other.size());
    if (Normalise(existing.text) == Normalise(clean) ||
        (larger > 0 && shared * 10 >= larger * 8)) {
      existing.text = clean;  // the newer wording
      existing.last_used = now;
      existing.ephemeral = existing.ephemeral && !persist;
      if (persist) {
        Save();
      }
      return true;
    }
  }

  // Full: the one least recently useful goes.
  size_t in_workspace = 0;
  for (const Fact& fact : facts_) {
    in_workspace += fact.workspace == workspace;
  }
  if (in_workspace >= kMaxFactsPerWorkspace) {
    auto oldest = facts_.end();
    for (auto it = facts_.begin(); it != facts_.end(); ++it) {
      if (it->workspace == workspace &&
          (oldest == facts_.end() || it->last_used < oldest->last_used)) {
        oldest = it;
      }
    }
    if (oldest != facts_.end()) {
      facts_.erase(oldest);
    }
  }

  Fact fact;
  fact.text = std::move(clean);
  fact.workspace = workspace;
  fact.created = now;
  fact.last_used = now;
  fact.ephemeral = !persist;
  facts_.push_back(std::move(fact));
  if (persist) {
    Save();
  }
  return true;
}

int LongTermMemory::Forget(const std::string& query, int workspace) {
  const std::set<std::string> wanted = Tokens(query);
  if (wanted.empty()) {
    return 0;
  }
  int removed = 0;
  bool touched_disk = false;
  for (auto it = facts_.begin(); it != facts_.end();) {
    // At least most of the words asked about, so "forget that I shop on
    // Amazon.in" finds "shops on Amazon.in" and not everything about shopping.
    if (it->workspace == workspace &&
        Overlap(wanted, Tokens(it->text)) * 10 >= wanted.size() * 6) {
      touched_disk |= !it->ephemeral;
      it = facts_.erase(it);
      ++removed;
    } else {
      ++it;
    }
  }
  if (touched_disk) {
    Save();
  }
  return removed;
}

std::vector<std::string> LongTermMemory::Recall(const std::string& context,
                                                int workspace,
                                                size_t max_chars) {
  const std::set<std::string> wanted = Tokens(context);
  const base::Time now = base::Time::Now();
  struct Scored {
    Fact* fact;
    double score;
    size_t shared;
  };
  std::vector<Scored> scored;
  for (Fact& fact : facts_) {
    if (fact.workspace != workspace) {
      continue;
    }
    const size_t shared = Overlap(wanted, Tokens(fact.text));
    const double days = (now - fact.last_used).InSecondsF() / 86400.0;
    const double recency = days < 30 ? 2.0 * (1.0 - days / 30.0) : 0.0;
    scored.push_back({&fact, shared * 4.0 + recency + std::min(fact.uses, 3) * 0.3, shared});
  }
  std::stable_sort(scored.begin(), scored.end(),
                   [](const Scored& a, const Scored& b) { return a.score > b.score; });

  std::vector<std::string> out;
  size_t used = 0;
  for (size_t rank = 0; rank < scored.size() && out.size() < 10; ++rank) {
    // Facts about something else are worth having only when there are few
    // enough that they cost nothing: who the user is, how they like things.
    if (scored[rank].shared == 0 && rank >= 6) {
      break;
    }
    const std::string& text = scored[rank].fact->text;
    if (used + text.size() > max_chars) {
      break;
    }
    used += text.size();
    out.push_back(text);
    scored[rank].fact->last_used = now;
    ++scored[rank].fact->uses;
  }
  return out;
}

size_t LongTermMemory::Count(int workspace) const {
  size_t count = 0;
  for (const Fact& fact : facts_) {
    count += fact.workspace == workspace;
  }
  return count;
}

std::vector<std::string> LongTermMemory::All(int workspace) const {
  std::vector<const Fact*> mine;
  for (const Fact& fact : facts_) {
    if (fact.workspace == workspace) {
      mine.push_back(&fact);
    }
  }
  std::stable_sort(mine.begin(), mine.end(), [](const Fact* a, const Fact* b) {
    return a->created > b->created;
  });
  std::vector<std::string> out;
  for (const Fact* fact : mine) {
    out.push_back(fact->text);
  }
  return out;
}

void LongTermMemory::Clear(int workspace) {
  std::erase_if(facts_, [&](const Fact& fact) { return fact.workspace == workspace; });
  Save();
}

void LongTermMemory::ClearAll() {
  facts_.clear();
  Save();
}

}  // namespace zephyrus::agent
