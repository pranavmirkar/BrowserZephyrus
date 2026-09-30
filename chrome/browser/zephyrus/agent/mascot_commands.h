// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_MASCOT_COMMANDS_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_MASCOT_COMMANDS_H_

#include <optional>
#include <string_view>

namespace zephyrus::agent {

// Where the person can ask the mascot to stand.
enum class MascotSpot {
  kHome,  // back where it started: the bottom right of the page
  kLeft,
  kRight,
  kTop,
  kBottom,
  kTopLeft,
  kTopRight,
  kBottomLeft,
  kBottomRight,
  kCenter,
  kAway,  // out of the way: the corner farthest from where it stands
};

// Reads "move to the left", "Zep, get out of the way", "go to the top right
// corner", "come back" -- a request to move the mascot, as typed or spoken.
//
// Deliberately narrow: the message must be made ENTIRELY of words about moving
// and places, once the politeness is stripped. Anything else in it ("move the
// slider to the left", "go to the top of the page and click login") is a task
// for the agent, not a request to this parser, and a message that mixes the
// two is left alone. A false negative costs the person a repeat; a false
// positive would swallow a task.
std::optional<MascotSpot> ParseMascotMove(std::string_view text);

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_MASCOT_COMMANDS_H_
