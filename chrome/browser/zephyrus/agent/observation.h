// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_OBSERVATION_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_OBSERVATION_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree_id.h"
#include "ui/accessibility/ax_tree_update_forward.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

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

  // How long ago this was posted, in the page's own words, or empty.
  //
  // Pulled out of the surrounding text as its own field rather than left inside
  // it. MEASURED on a real results page: thirty elements, sixteen carrying
  // surrounding text, and only TWO where an upload age survived -- because the
  // text spent its budget repeating the title the name already gave. Asked for
  // "the latest sidemen video", the agent had nothing to compare and opened one
  // a month old while two-day-old videos sat beside it.
  //
  // A separate field because it answers a question a blob cannot: newest is a
  // comparison, and a comparison needs the same fact in the same place for
  // every candidate.
  std::string posted;

  // The text that sits AROUND this element, or empty.
  //
  // An accessible name is the element's own label and nothing else. On a
  // YouTube results page a video link is called "SIDEMEN LAST TO FALL ASLEEP
  // CHALLENGE (USA EDITION) 1 hour, 45 minutes" -- title and duration -- while
  // "6.4M views" and "8 days ago" live in SIBLING elements and never reached
  // the model at all.
  //
  // So "play the LATEST sidemen video" was unanswerable: nothing in front of
  // the model said which one was newest. It is the question a person answers by
  // glancing at the line under the title, and we were not showing that line.
  std::string detail;

  // What private thing this element holds, as a bare word, or empty.
  //
  // Worked out ONCE, by the sanitizer, before it redacts. Anything downstream
  // that needs to know reads this rather than classifying again: a second
  // classifier would run on a value that has already been replaced and quietly
  // decide the field was harmless -- the exact ordering trap that makes a
  // security rule true in a test and false in the browser.
  std::string sensitivity;

  // True if something is drawn ON TOP of this element.
  //
  // The question vision was going to answer, answered as a fact instead. A
  // cookie banner covering a button is the commonest reason a click does
  // nothing, and a screenshot-driven agent has to squint at pixels to notice.
  // We are inside the browser: we can ask what is actually at that point.
  //
  // Being inside also means this is knowable BEFORE acting rather than
  // afterwards, which turns "the click did nothing" into "that is covered,
  // deal with the thing on top first".
  bool obscured = false;

  // A label shared by elements that are repetitions of the same thing, or
  // empty. `group_size` is how many are in that set.
  //
  // A results page is a TABLE -- a dozen cards with the same shape -- and
  // flattening it into loose links threw that away. A model then cannot tell a
  // search result from the filter chip sitting next to it, which is exactly the
  // failure watched on youtube.com: it clicked "Latest" and "Videos" repeatedly
  // while the videos themselves sat in the same undifferentiated list.
  std::string group;
  size_t group_size = 0;

  // The node this element hangs off, used only to work out the groups above.
  // Never shown, and meaningless outside this Observation like every other id.
  ui::AXNodeID parent_ax_id = ui::kInvalidAXNodeID;

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

  // What changed since the agent last looked, in plain words, or empty.
  //
  // The general form of "did that work". Reporting a flag per kind of state --
  // media playing, then downloads, then dialogs, then cart counts -- is a list
  // that never ends, because a page can be doing anything. What is finite is
  // the DIFFERENCE between two consecutive looks, and that answers the question
  // for cases nobody enumerated: a confirmation appearing, results loading, a
  // basket total moving, a login completing.
  //
  // It exists because a task was finished and the agent could not tell. It
  // opened the right video twice and carried on hunting, because nothing said
  // anything had happened.
  std::string changed;

  // True if this page is playing sound right now.
  //
  // The missing half of "did it work". We told the model WHERE it arrived and
  // never what the page was DOING, so a task literally about playing a video
  // finished successfully and the model had no way to tell -- it opened the
  // right video twice and carried on hunting both times.
  //
  // A browser knows this. A screenshot-driven agent has to guess from a pause
  // button's shape, which is exactly the kind of inference being inside the
  // browser lets us replace with a fact.
  bool media_playing = false;
  // The address of the DOCUMENT that was last actually loaded, which is not
  // always the address the page is currently showing.
  //
  // A page may rewrite its own address without loading anything: canonicalising
  // a link, dropping a tracking parameter, or a single-page app moving to its
  // next view. `url` follows that rewrite, because it is what the user would
  // see. This does not.
  //
  // Kept because the two answer different questions, and one of them was being
  // asked with the wrong field. "Did the address I asked for open?" is about
  // the document; a page that loaded correctly and then tidied its own URL was
  // being reported to the model as "that address did not open -- addresses
  // cannot be guessed", which is both false and the exact advice most likely
  // to send it somewhere else.
  //
  // Not serialised: this is for the browser's own check, not for the model.
  std::string document_url;

  bool loading = false;

  // A picture of the page, as JPEG bytes. Empty unless vision is switched on.
  //
  // The SECOND channel, not a replacement for the first. Measured across the
  // field: a page costs roughly ten times more as an image than as an
  // accessibility tree, and the strongest agents layer a screenshot OVER the
  // tree rather than choosing between them. The tree stays the precise,
  // cheap way to name and reach an element; the picture is for what the tree
  // gets wrong -- which on a modern app is plenty, since it was designed for
  // screen readers and most sites expose it poorly.
  //
  // Kept out of ToJson deliberately. This never becomes prompt text; it
  // travels as an image or not at all.
  std::vector<uint8_t> screenshot_jpeg;

  // What a local model said the page looks like, or empty.
  //
  // This is what the vision channel actually delivers. The screenshot is
  // described on this machine and only the DESCRIPTION travels onward -- the
  // picture itself never leaves, so there is no image to leak rather than a
  // filter hoping to catch one.
  //
  // Kept short on purpose. It covers what the accessibility tree cannot say --
  // a dialog covering the page, a video playing, a region that is an image of a
  // form rather than a form -- and nothing the tree already says precisely.
  std::string vision_summary;

  // The viewport the element bounds were measured in.
  //
  // The picture is downscaled to bound its cost, so masking it needs to know
  // what the bounds were relative to. Without this the black rectangles land in
  // the wrong place -- which on a form means covering the label and leaving the
  // value beside it in plain sight.
  gfx::Size viewport;

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

  // Fills in `changed` by comparing against what was seen last.
  //
  // Deliberately factual and short. It reports that eight things appeared and
  // names two of them; it does not decide whether that means the task is done,
  // because that is the model's judgement and a browser guessing at it would be
  // confidently wrong on the cases that matter.
  void DescribeChangeFrom(const Observation& previous);

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
// Measured on a real run: 8 minutes 40 seconds for twelve steps, which is
// FORTY-THREE SECONDS PER STEP. A tool call costs about 3ms, so all of that is
// the model reading. What it reads is this Observation, every single step.
//
// Sixty elements at roughly a hundred bytes each, plus 1500 characters of page
// text, plus the growing history, is several thousand tokens per turn -- and a
// local 7B pays for every one of them. Halving what it reads roughly halves the
// wall-clock cost of the whole task, which matters far more than any code here.
//
// Thirty is enough because content now comes before navigation chrome, so the
// cut falls on the site's furniture rather than on what the task is about.
inline constexpr size_t kMaxObservedElements = 30;
inline constexpr size_t kMaxObservedTextLength = 700;

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_OBSERVATION_H_
