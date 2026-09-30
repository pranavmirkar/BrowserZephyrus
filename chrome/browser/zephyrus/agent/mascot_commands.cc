// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/mascot_commands.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/strings/string_util.h"

namespace zephyrus::agent {

namespace {

template <size_t N>
bool Contains(const std::string_view (&list)[N], const std::string& w) {
  return std::find(std::begin(list), std::end(list), w) != std::end(list);
}

// Politeness and grammar that carry no meaning here.
constexpr std::string_view kFiller[] = {
    "please", "pls",  "hey",   "hi",     "zep",   "zeb",    "can",   "could",
    "would",  "will", "you",   "your",   "yourself", "a",   "an",    "the",
    "to",     "of",   "bit",   "little", "slightly", "just", "over", "on",
    "at",     "side", "screen", "kindly", "now",  "for",    "me",    "okay",
    "ok",     "thanks", "thank", "so",   "i",     "my",     "re",    "m",
    "s",      "ll",   "there", "here",   "it",    "be",     "lets",  "let",
    "us",     "want", "need",  "like",   "should", "is",    "are",   "in",
    "from",   "off",  "way",   "out",    "corner",
};

// Things a mascot can be blocking. Only counted as filler when something is
// said to be covering them, so "go to the top of the page" stays a task.
constexpr std::string_view kCovered[] = {
    "page", "text", "content", "view", "stuff", "everything", "something",
    "website", "site", "video", "article", "words", "button",
};

constexpr std::string_view kVerbs[] = {
    "move", "go", "scoot", "shift", "step", "walk", "stand", "sit", "get",
    "relocate", "come", "return", "run", "hop",
};

std::vector<std::string> Words(std::string_view text) {
  std::vector<std::string> words;
  std::string current;
  for (char c : text) {
    if (base::IsAsciiAlpha(c)) {
      current.push_back(base::ToLowerASCII(c));
    } else if (!current.empty()) {
      words.push_back(std::move(current));
      current.clear();
    }
  }
  if (!current.empty()) {
    words.push_back(std::move(current));
  }
  return words;
}

}  // namespace

std::optional<MascotSpot> ParseMascotMove(std::string_view text) {
  const std::vector<std::string> all = Words(text);
  if (all.empty() || all.size() > 14) {
    return std::nullopt;
  }

  bool covering = false;
  for (const std::string& w : all) {
    covering = covering || w == "blocking" || w == "covering" ||
               w == "hiding" || w == "obstructing";
  }
  bool verb = false;
  bool home = false;
  bool away = false;
  bool top = false;
  bool bottom = false;
  bool left = false;
  bool right = false;
  bool center = false;
  bool out = false;
  bool way = false;
  bool come = false;
  bool back = false;
  for (const std::string& w : all) {
    if (Contains(kFiller, w)) {
      out = out || w == "out";
      way = way || w == "way";
      continue;
    }
    if (Contains(kVerbs, w)) {
      verb = true;
      come = come || w == "come" || w == "return";
    } else if (w == "home") {
      home = true;
    } else if (w == "back") {
      back = true;
    } else if (w == "away" || w == "aside" || w == "blocking" ||
               w == "covering" || w == "hiding" || w == "obstructing") {
      away = true;
    } else if (w == "top" || w == "upper") {
      top = true;
    } else if (w == "bottom" || w == "lower") {
      bottom = true;
    } else if (w == "left") {
      left = true;
    } else if (w == "right") {
      right = true;
    } else if (w == "center" || w == "centre" || w == "middle") {
      center = true;
    } else if (covering && Contains(kCovered, w)) {
      continue;
    } else {
      // A word about something else: this is a task, not a request to move.
      return std::nullopt;
    }
  }
  // "out of the way" / "in the way".
  if (out && way) {
    away = true;
  }

  if (home || (come && back) || (come && !top && !bottom && !left && !right &&
                                 !center && !away)) {
    return MascotSpot::kHome;
  }
  if (back && !verb) {
    return std::nullopt;
  }
  if (away && !top && !bottom && !left && !right && !center) {
    return MascotSpot::kAway;
  }
  const bool place = top || bottom || left || right || center;
  // A bare "left" is not a request; it needs something asking for a move.
  if (!place || !verb) {
    return std::nullopt;
  }
  if (center && !top && !bottom && !left && !right) {
    return MascotSpot::kCenter;
  }
  if (top && left) {
    return MascotSpot::kTopLeft;
  }
  if (top && right) {
    return MascotSpot::kTopRight;
  }
  if (bottom && left) {
    return MascotSpot::kBottomLeft;
  }
  if (bottom && right) {
    return MascotSpot::kBottomRight;
  }
  if (top) {
    return MascotSpot::kTop;
  }
  if (bottom) {
    return MascotSpot::kBottom;
  }
  if (left) {
    return MascotSpot::kLeft;
  }
  if (right) {
    return MascotSpot::kRight;
  }
  return std::nullopt;
}

}  // namespace zephyrus::agent
