// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_OBSERVATION_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_OBSERVATION_H_

#include <string>
#include <string_view>
#include <vector>

#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree_id.h"
#include "ui/accessibility/ax_tree_update_forward.h"
#include "ui/gfx/geometry/rect.h"

namespace zephyrus::agent {

// One thing on the page the agent may act on.
struct ObservedNode {
  // The id the model sees: "e1", "e2". Opaque and issued here. It is not the
  // node's own identity and it means nothing outside the Observation that
  // produced it.
  std::string id;
  // A short, stable word from the table in the .cc file. If a role is not in
  // that table it is not offered at all, so this doubles as the definition of
  // "interactive".
  std::string role;
  std::string name;
  std::string value;

  // The node's id WITHIN THIS SNAPSHOT. Never leaves the browser, and never
  // shown to the model: the model works in issued ids so a made-up one resolves
  // to nothing.
  //
  // DO NOT SEND THIS TO AN ACCESSIBILITY ACTION. It is not the renderer's node
  // id. RequestAXTreeSnapshot runs its result through ui::AXTreeCombiner, whose
  // MapId renumbers every node sequentially (`next_id_++`), so this is a
  // counter that means nothing outside the snapshot that produced it. Acting on
  // it addresses whatever node happens to hold the same number: asking to click
  // "Search" on youtube.com activated the Copyright link in the footer.
  //
  // It cost weeks, because on a small page the renumbering comes out as the
  // identity -- so every browsertest passed while real sites behaved at random,
  // and it read as a bad model rather than a bug. Every element action goes
  // through `bounds` instead. This is kept only for matching the snapshot's own
  // focus_id, which came from the same renumbering and so agrees with it.
  ui::AXNodeID ax_id = ui::kInvalidAXNodeID;

  // Where the element is, in the viewport's coordinate space.
  //
  // Internal for the same reason as ax_id, and more so: this is the point the
  // pointer is actually sent to, so a caller that could name one could click
  // anywhere on the page regardless of what it claimed to be clicking.
  gfx::Rect bounds;

  // True if the element is scrolled out of view.
  //
  // Its bounds have been clipped to the edge of an ancestor, which means they
  // no longer name a point on the element -- they name the edge it disappeared
  // behind. Anything aiming a pointer has to check this first.
  bool offscreen = false;
};

// What the browser saw, at one moment, on one page.
//
// The contract calls element ids "per-Observation", expiring when the page
// changes. That is enforced by structure rather than by bookkeeping: taking a
// new Observation replaces the old one, and with it every id it issued.
struct Observation {
  Observation();
  Observation(const Observation&) = delete;
  Observation& operator=(const Observation&) = delete;
  Observation(Observation&&);
  Observation& operator=(Observation&&);
  ~Observation();

  std::string url;
  std::string title;
  std::vector<ObservedNode> elements;
  std::string text;

  // The tree this came from. Compared before acting: if the page has been
  // replaced, every id in here refers to something that no longer exists, and
  // acting on a matching id in the new tree would act on the wrong thing.
  ui::AXTreeID tree_id = ui::AXTreeIDUnknown();

  // The issued id of the element the page currently has focused, or empty.
  //
  // Worth reporting for its own sake -- a model typing into a form should know
  // where the caret is -- and it is how "focus did not happen" was told apart
  // from "focus happened but the event did not fire".
  std::string focused_id;

  // True if the page had more to offer than the cap allowed. Reported to the
  // model, because "these are the elements" and "these are the first hundred
  // elements" lead to different next steps.
  bool truncated = false;

  // Null if no such id was issued here. This is the grounding check.
  const ObservedNode* Find(std::string_view element_id) const;

  // What the model is shown. `level` follows the tool contract: 0 is url and
  // title only, 1 adds elements and text.
  std::string ToJson(int level) const;

  // The elements whose name or role contains `query`, case-insensitively.
  std::vector<const ObservedNode*> Matching(std::string_view query) const;
};

// Reads a snapshot into an Observation.
//
// Pure: no browser, no renderer, no I/O. That is what makes the interesting
// decisions here -- which roles count, what a password field looks like, where
// the caps fall -- testable against a handful of synthetic trees instead of
// against a live page.
Observation BuildObservation(const ui::AXTreeUpdate& update,
                             const std::string& url,
                             const std::string& title,
                             size_t max_elements,
                             size_t max_text_length);

// True for the roles that actually hold typed text.
//
// Shared with the executor so that "can this be typed into" has ONE answer.
// The Observation decides what to call a role; anything else deciding
// separately is a second opinion waiting to disagree.
bool IsTextEntryRole(std::string_view role);

// Defaults used by the browser. Named so tests can pick smaller ones.
//
// These are a SPEED setting as much as a size one. Every step sends the whole
// Observation to the model, so on a page like a long encyclopedia article the
// prompt dominates the time per step -- measured at roughly four seconds with
// qwen2.5:7b, and worse as the page grows. Cut hard enough that a task feels
// like it is moving; a model that needs more can call page.find.
inline constexpr size_t kMaxObservedElements = 60;
inline constexpr size_t kMaxObservedTextLength = 1500;

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_OBSERVATION_H_
