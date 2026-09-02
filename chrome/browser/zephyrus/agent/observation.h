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

  // The real node. Never leaves the browser and is never shown to the model:
  // the model works in issued ids so that a made-up one resolves to nothing.
  ui::AXNodeID ax_id = ui::kInvalidAXNodeID;
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

// Defaults used by the browser. Named so tests can pick smaller ones.
inline constexpr size_t kMaxObservedElements = 100;
inline constexpr size_t kMaxObservedTextLength = 4000;

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_OBSERVATION_H_
