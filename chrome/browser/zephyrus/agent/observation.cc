// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/observation.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <vector>
#include <utility>

#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_update.h"
#include "ui/gfx/geometry/rect_conversions.h"

namespace zephyrus::agent {
namespace {

// The roles the agent is offered, and what each is called.
//
// This table is the definition of "interactive" for the whole feature: a role
// that is not here is not put in front of the model, so no tool can name it.
// Widening the agent's reach over a page is an edit to this list, which is the
// same property ToolSurface gives its capabilities.
//
// Deliberately narrow. Chromium has well over a hundred roles and most of them
// are structure, not controls; offering them would bury the handful that matter
// under a page's worth of divs.
struct RoleName {
  ax::mojom::Role role;
  const char* name;
};

constexpr RoleName kOfferedRoles[] = {
    {ax::mojom::Role::kButton, "button"},
    {ax::mojom::Role::kToggleButton, "button"},
    {ax::mojom::Role::kPopUpButton, "combobox"},
    {ax::mojom::Role::kDisclosureTriangle, "button"},
    {ax::mojom::Role::kLink, "link"},
    {ax::mojom::Role::kTextField, "textbox"},
    {ax::mojom::Role::kTextFieldWithComboBox, "combobox"},
    {ax::mojom::Role::kSearchBox, "searchbox"},
    {ax::mojom::Role::kComboBoxSelect, "combobox"},
    {ax::mojom::Role::kCheckBox, "checkbox"},
    {ax::mojom::Role::kRadioButton, "radio"},
    {ax::mojom::Role::kListBox, "listbox"},
    {ax::mojom::Role::kListBoxOption, "option"},
    {ax::mojom::Role::kMenuItem, "menuitem"},
    {ax::mojom::Role::kMenuItemCheckBox, "menuitem"},
    {ax::mojom::Role::kMenuItemRadio, "menuitem"},
    {ax::mojom::Role::kTab, "tab"},
    {ax::mojom::Role::kSwitch, "switch"},
    {ax::mojom::Role::kSlider, "slider"},
    {ax::mojom::Role::kSpinButton, "spinbutton"},
};



// The word for this node's role, or empty if it is not something we offer.
std::string RoleNameFor(const ui::AXNode& node) {
  const ax::mojom::Role role = node.GetRole();
  for (const RoleName& entry : kOfferedRoles) {
    if (entry.role != role) {
      continue;
    }
    // A protected text field is a password field. It is called out by name
    // rather than left looking like an ordinary textbox because the policy
    // engine refuses to type into one, and it can only do that if it can tell
    // them apart. There is no distinct role for this in Chromium; the state is
    // the only signal.
    if (node.HasState(ax::mojom::State::kProtected) &&
        std::string_view(entry.name) == "textbox") {
      return "password";
    }
    return entry.name;
  }
  return std::string();
}

// The longest name the model is shown.
//
// Long enough to keep the ANSWER, not just the label.
//
// 80 was a mistake and it cost a task. YouTube writes a video's whole card into
// the link's name -- "SIDEMEN SLEEPOVER (USA EDITION) by Sidemen 5.8M views 4
// days ago" -- so cutting at 80 threw away the channel, the view count and the
// upload age. Asked to play the LATEST video, the model was handed titles with
// the dates trimmed off, which is the one field the question turns on.
//
// Still bounded, because every element is read by the model on every step. The
// element cap came down from sixty to thirty to pay for this: fewer things,
// each actually worth reading.
constexpr size_t kMaxNameLength = 140;

// The title gets its own, larger cap.
//
// Bounding it at the ELEMENT cap was too tight, and the same mistake as capping
// names at eighty: it truncated the one field that says what page this is. A
// title is a single string read once, not one of thirty read every step, so it
// can afford more room. Three hundred still turns a hostile 4096-character
// title -- Chromium's own ceiling -- into something that cannot crowd out the
// page it is describing.
constexpr size_t kMaxTitleLength = 300;

// A name a person could act on.
//
// An accessible name is often computed from everything inside the element, so a
// link wrapping a card comes back as its entire contents run together --
// measured on youtube.com: "Sidemen Verified @Sidemen*23.4M subscribers Welcome
// to the official Sidemen channel. The home of #SidemenSundays - We post new
// Sidemen videos every single Sunday!" for a single link. A model asked to pick
// between a dozen of those is reading paragraphs, not labels, and it picked
// badly.
//
// Truncation is safe here because the model acts on issued ids, never on names.
// The name is only how it chooses, and the first few words are what a person
// reads too.
std::string Shorten(std::string name, size_t limit = kMaxNameLength) {
  // Newlines and runs of spaces come from the page's own layout and carry no
  // meaning once the text is on one line.
  //
  // false, NOT true: the flag asks whether a whitespace run containing a line
  // break should be removed ENTIRELY rather than collapsed to one space. True
  // turns a newline between two words into nothing at all, welding them
  // together --
  // caught by CollapsesTheWhitespaceAPageLaidOutWith, which is why that test
  // asserts the exact string rather than merely that it got shorter.
  name = base::CollapseWhitespaceASCII(
      name, /*trim_sequences_with_line_breaks=*/false);
  if (name.size() <= limit) {
    return name;
  }

  // On a character boundary, not a byte one. Cutting a UTF-8 sequence in half
  // would produce a string the JSON writer cannot encode, and the model would
  // lose the whole Observation rather than one long name.
  std::string cut;
  base::TruncateUTF8ToByteSize(name, limit, &cut);

  // Back up to the last space so the label ends on a word. A name that stops
  // mid-word reads as corruption and invites the model to distrust it.
  const size_t space = cut.find_last_of(' ');
  if (space != std::string::npos && space > limit / 2) {
    cut.resize(space);
  }
  return cut + "...";
}

// What to call this control.
//
// A field with no accessible name is one the model cannot reason about, and on
// a real page that is not hypothetical: YouTube offers an unnamed text input
// alongside its named search box, and a model given both picked the anonymous
// one and typed into it eight times running.
//
// So fall back the way a person reads a form -- the placeholder, then any
// description. Chromium only populates kPlaceholder when it is NOT already the
// name, so this adds information rather than repeating it.
// "8 days ago" and friends, found anywhere in `text`.
//
// Relative ages are what results pages actually print, and they are directly
// comparable without knowing today's date. Deliberately narrow: a NUMBER, a
// unit, then "ago". Anything else is prose that happens to contain a number.
std::string PostedAgeIn(std::string_view text) {
  // Structure, not a vocabulary.
  //
  // The first version listed the unit words -- second, minute, hour and so on
  // -- which read as general and was not. The very page it was written for
  // prints "13d ago" and "7h ago" in its sidebar, and none of those matched. A
  // list of words is a list of the spellings someone happened to think of.
  //
  // What every one of them shares is shape: a NUMBER, then some letters, then
  // "ago". That covers "8 days ago", "13d ago" and "3 mo ago" without knowing
  // any of them in advance, and still refuses "long ago" and "ages ago",
  // because those carry no number.
  const size_t ago = text.find(" ago");
  if (ago != std::string_view::npos) {
    // Just the tail before "ago" -- an age is short, and looking further back
    // only invites a number from an unrelated sentence.
    constexpr size_t kAgeWindow = 14;
    const size_t from = ago > kAgeWindow ? ago - kAgeWindow : 0;
    std::string_view before = text.substr(from, ago - from);

    // Walk back over the letters, then any space, then the digits.
    size_t at = before.size();
    while (at > 0 && base::IsAsciiAlpha(before[at - 1])) {
      --at;
    }
    const size_t letters = at;
    while (at > 0 && base::IsAsciiWhitespace(before[at - 1])) {
      --at;
    }
    const size_t digits_end = at;
    while (at > 0 && base::IsAsciiDigit(before[at - 1])) {
      --at;
    }
    if (at < digits_end && letters < before.size()) {
      std::string found(text.substr(from + at, ago - from - at + 4));
      base::TrimWhitespaceASCII(found, base::TRIM_ALL, &found);
      return found;
    }
  }

  // Failing that, a written date.
  //
  // Plenty of pages print "12 August 2025" or "Aug 12, 2025" rather than an
  // age -- a review, an article, a release. It is a worse answer to "which is
  // newest" than an age is, and a far better one than nothing.
  static constexpr std::string_view kMonths[] = {
      "january", "february", "march",     "april",   "may",      "june",
      "july",    "august",   "september", "october", "november", "december"};
  const std::string lowered = base::ToLowerASCII(text);
  for (std::string_view month : kMonths) {
    const size_t at = lowered.find(month);
    if (at == std::string::npos) {
      continue;
    }
    // A month name is only a date when a year is beside it. "March" on its own
    // is a word.
    const size_t from = at > 8 ? at - 8 : 0;
    const std::string_view around =
        std::string_view(lowered).substr(from, month.size() + 24);
    size_t digits = 0;
    for (const char c : around) {
      digits += base::IsAsciiDigit(c) ? 1 : 0;
    }
    if (digits < 4) {
      continue;
    }
    std::string found(text.substr(from, around.size()));
    base::TrimWhitespaceASCII(found, base::TRIM_ALL, &found);
    return found;
  }
  return std::string();
}

// The words near `node` that its own name does not already carry.
//
// Walks up until an ancestor has meaningfully more text than the element does,
// then reports what that ancestor adds. On a results page that ancestor is the
// card around the link, and what it adds is the channel, the view count and the
// upload age -- exactly the line a person reads to tell one result from
// another.
//
// Bounded hard, and taken from an ancestor rather than the whole page, because
// the point is a caption and not a second copy of the document.
std::string DetailAround(const ui::AXNode& node, const std::string& name) {
  // Room for a caption, now that it no longer repeats the title.
  constexpr size_t kMaxDetailLength = 120;
  // Five, and the depth is not what keeps this honest -- the length cap below
  // is. An ancestor that has climbed far enough to hold the neighbouring
  // results is an ancestor whose text is far longer than a caption, and it is
  // skipped on that ground whatever its depth. Stopping at three only meant
  // giving up before reaching the row that had the facts.
  constexpr int kAncestorsToTry = 5;

  // The first thing worth quoting, kept in case nothing better turns up.
  std::string fallback;

  const ui::AXNode* ancestor = node.parent();
  for (int step = 0; ancestor && step < kAncestorsToTry;
       ++step, ancestor = ancestor->parent()) {
    std::string around = ancestor->GetTextContentUTF8();
    if (around.empty()) {
      continue;
    }

    // No length test against the name.
    //
    // "Is this ancestor longer than the name" was a proxy for "does it add
    // anything", and a wrong one: text content is built from DESCENDANT static
    // text, so a link with a name and no children contributes none of itself to
    // it. A card whose only other text was "7h ago" therefore measured shorter
    // than the name and was skipped -- losing the upload age precisely where
    // the page was most concise about it. What is left AFTER removing the
    // element's own words is the only thing worth testing.

    // Remove the element's own words from the front, one word at a time.
    //
    // An exact substring search missed almost every time: the NAME is the
    // accessibility label, which on a results page is the title plus the
    // duration, while the surrounding text is the title followed by the view
    // count. They share a prefix and differ after it, so nothing matched and
    // the caption spent its whole budget repeating the title -- which is how
    // the upload age, the one fact "latest" needs, ended up cut off.
    for (const std::string& word : base::SplitString(
             name, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      if (around.size() >= word.size() &&
          around.compare(0, word.size(), word) == 0) {
        around.erase(0, word.size());
        continue;
      }
      break;
    }
    around = base::CollapseWhitespaceASCII(
        around, /*trim_sequences_with_line_breaks=*/false);
    base::TrimWhitespaceASCII(around, base::TRIM_ALL, &around);
    if (around.empty()) {
      continue;
    }
    if (around.size() > kMaxDetailLength) {
      // A run this long is the rest of the page, not a caption for this one
      // element. Keep climbing rather than quoting it.
      continue;
    }

    // It has to add a FACT, and a fact here has a number in it.
    //
    // Views, ages, prices, ratings, counts -- everything this field exists to
    // carry is numeric. What it is NOT for is the names of the element's
    // neighbours: the "All" filter chip sat in a bar with the others, so its
    // caption came back "ShortsUnwatchedWatchedVideosRecently uploadedLive",
    // which tells a model nothing it cannot already see in the element list.
    //
    // Measured, and this is the part that matters: without this test almost
    // EVERY element got a caption from its immediate parent, and the
    // observation grew enough to break a timing-sensitive click test that had
    // been green all day. A field that costs every look something has to earn
    // it on every element it appears on.
    if (!std::any_of(around.begin(), around.end(),
                     [](char c) { return base::IsAsciiDigit(c); })) {
      continue;
    }

    // Of the enclosing texts that qualify, prefer one that DATES the thing.
    //
    // Taking the first was the bug, and it is a subtle one because the first is
    // usually right. MEASURED on a search results page: 3 of 30 elements got a
    // caption at all, against 19 of 30 on the channel page for the same
    // videos. The difference was not the data -- both pages print the upload
    // age -- it was that search results nest one level deeper, and the shallow
    // ancestor holds only the title.
    //
    //   e11  detail: "$100,000 vs $100 CRUISE HOLIDAY"           <- stopped here
    //   e25  detail: "ABANDONED IN ASIA7.2M views - 4 months ago"
    //
    // Both passed the "has a number in it" test, because e11's title is about
    // money. A digit proves the text carries a fact; it does not prove the
    // fact is the one being looked for. So keep climbing while no age has been
    // found, and settle for the first candidate only if none of them has one.
    //
    // General, not a rule about videos: an article, a review and a listing all
    // put their date one wrapper out from their title.
    if (!PostedAgeIn(around).empty()) {
      return around;
    }
    if (fallback.empty()) {
      fallback = around;
    }
  }
  return fallback;
}

std::string DescribeNode(const ui::AXNode& node) {
  std::string name = node.GetStringAttribute(ax::mojom::StringAttribute::kName);
  if (!name.empty()) {
    return Shorten(std::move(name));
  }
  name = node.GetStringAttribute(ax::mojom::StringAttribute::kPlaceholder);
  if (!name.empty()) {
    return Shorten(std::move(name));
  }
  return Shorten(
      node.GetStringAttribute(ax::mojom::StringAttribute::kDescription));
}

// True for the parts of a page that are the same on every page of the site.
//
// Navigation rails, headers and footers. A person skips these without noticing;
// a model reading a flat list cannot tell them from the content it was asked
// about. On a YouTube results page they contribute roughly forty links -- Home,
// Shorts, Subscriptions, About, Press, Copyright, Terms, Privacy -- competing
// for the same sixty slots as the actual videos.
//
// That is not hypothetical. Asked to play a video, a model clicked "Copyright"
// and landed on YouTube's copyright policy page, repeatedly. The element it
// picked was a real one and the click was accurate: the Observation simply
// offered it as though it were as relevant as a search result.
bool IsPageChrome(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kNavigation:
    case ax::mojom::Role::kBanner:
    case ax::mojom::Role::kContentInfo:
    case ax::mojom::Role::kComplementary:
    case ax::mojom::Role::kFooter:
      return true;
    default:
      return false;
  }
}

void Collect(const ui::AXTree& tree,
             const ui::AXNode& node,
             bool inside_chrome,
             std::vector<ObservedNode>& content,
             std::vector<ObservedNode>& chrome) {
  // Invisible and ignored are NOT the same thing, and treating them the same
  // was a bug that made this return nothing at all on real pages.
  //
  // Invisible means the whole subtree is hidden from everyone, so there is
  // nothing below worth walking. Ignored means only that this node should not
  // be exposed *itself* -- its children still should. Real documents are full
  // of ignored wrappers (html, body, layout containers), so pruning at one
  // meant pruning everything under it.
  if (node.data().IsInvisible()) {
    return;
  }

  inside_chrome = inside_chrome || IsPageChrome(node.GetRole());

  const std::string role = node.IsIgnored() ? std::string() : RoleNameFor(node);
  if (!role.empty()) {
    std::string name = DescribeNode(node);
    std::string value = node.GetValueForControl();

    // A control with nothing to call it is one the model cannot choose between.
    // Text fields used to be offered anyway, on the theory that an input is
    // identifiable by being the only one -- which is false on a real page, and
    // cost a task eight steps of typing into an anonymous box. If nothing above
    // could name it, it is not worth offering.
    if (!name.empty()) {
      {
        ObservedNode observed;
        // The id is issued later, once content and chrome have been put in
        // order. Numbering here would number them in the order they appear in
        // the document, which is the order this exists to stop using.
        observed.role = role;
        observed.detail = DetailAround(node, name);
        observed.posted = PostedAgeIn(observed.detail);
        observed.name = std::move(name);
        observed.value = std::move(value);
        observed.ax_id = node.id();

        // Where it is on screen. Only the browser ever sees this; it is what
        // lets the executor put a real pointer on a field, which is the only
        // way that has been made to focus one.
        //
        // Asking the tree rather than reading the node's own rect is the whole
        // point: a node's bounds are relative to its offset container, so on a
        // real page -- nested in scrollers and transformed ancestors -- the raw
        // rect is not where the element is. GetTreeBounds walks that chain and
        // applies the scroll offsets.
        bool offscreen = false;
        observed.bounds =
            gfx::ToEnclosingRect(tree.GetTreeBounds(&node, &offscreen));
        observed.offscreen = offscreen;
        observed.parent_ax_id =
            node.parent() ? node.parent()->id() : ui::kInvalidAXNodeID;

        (inside_chrome ? chrome : content).push_back(std::move(observed));
      }
    }
  }

  for (const ui::AXNode* child : node.children()) {
    if (child) {
      Collect(tree, *child, inside_chrome, content, chrome);
    }
  }
}

// Every visible node with its position, in document order.
//
// Gathered once and reused, because the alternative -- asking the tree for
// bounds per element per candidate -- walks the ancestor chain thousands of
// times over for the same answer.
struct PlacedNode {
  // raw_ptr rather than a bare pointer because this is a field, and the
  // checker is right to insist even though the vector never leaves the call
  // that built it -- "it is local" is exactly the reasoning that stops being
  // true the first time someone stores one of these.
  raw_ptr<const ui::AXNode> node;
  gfx::Rect bounds;
};

void PlaceAll(const ui::AXTree& tree,
              const ui::AXNode& node,
              std::vector<PlacedNode>& out) {
  if (node.data().IsInvisible()) {
    return;
  }
  const gfx::Rect bounds = gfx::ToEnclosingRect(tree.GetTreeBounds(&node));
  if (!bounds.IsEmpty()) {
    out.push_back({&node, bounds});
  }
  for (const ui::AXNode* child : node.children()) {
    if (child) {
      PlaceAll(tree, *child, out);
    }
  }
}

bool IsSelfOrAncestorOf(const ui::AXNode* maybe_ancestor,
                        const ui::AXNode* node) {
  for (const ui::AXNode* walk = node; walk; walk = walk->parent()) {
    if (walk == maybe_ancestor) {
      return true;
    }
  }
  return false;
}

// True if something else is drawn over the middle of `element`.
//
// Approximates paint order by DOCUMENT order: of everything covering that
// point, the one latest in the document is the one on top. That is how the web
// behaves without explicit z-index, and it is how banners and modals work --
// they are appended at the end of the body precisely so they land on top.
//
// It is an approximation, and worth naming as one: an element lifted by
// z-index from earlier in the document will be missed. The case that matters
// is caught, which is a cookie banner or dialog swallowing a click.
bool IsCoveredAt(const std::vector<PlacedNode>& placed,
                 const ui::AXNode* element,
                 const gfx::Point& point) {
  const ui::AXNode* topmost = nullptr;
  for (const PlacedNode& candidate : placed) {
    if (candidate.bounds.Contains(point)) {
      topmost = candidate.node;
    }
  }
  if (!topmost) {
    return false;
  }
  // Its own children are not "on top of" it in any sense that matters -- the
  // text inside a button is part of the button.
  return !IsSelfOrAncestorOf(element, topmost);
}

// Labels sets of elements that are repetitions of the same thing.
//
// Same parent, same role, three or more of them. Three because two of anything
// is a pair rather than a pattern, and a pair is as likely to be Cancel/OK as
// a list.
void FindGroups(std::vector<ObservedNode>& elements) {
  std::map<std::pair<ui::AXNodeID, std::string>, std::vector<size_t>> sets;
  for (size_t i = 0; i < elements.size(); ++i) {
    if (elements[i].parent_ax_id == ui::kInvalidAXNodeID) {
      continue;
    }
    sets[{elements[i].parent_ax_id, elements[i].role}].push_back(i);
  }

  int next = 1;
  for (const auto& [key, members] : sets) {
    if (members.size() < 3) {
      continue;
    }
    const std::string label =
        base::StrCat({"set", base::NumberToString(next++)});
    for (const size_t index : members) {
      elements[index].group = label;
      elements[index].group_size = members.size();
    }
  }
}

}  // namespace

bool IsTextEntryRole(std::string_view role) {
  return role == "textbox" || role == "searchbox" || role == "combobox" ||
         role == "password";
}

Observation::Observation() = default;
Observation::Observation(Observation&&) = default;
Observation& Observation::operator=(Observation&&) = default;
Observation::~Observation() = default;

const ObservedNode* Observation::Find(std::string_view element_id) const {
  for (const ObservedNode& node : elements) {
    if (node.id == element_id) {
      return &node;
    }
  }
  return nullptr;
}

void Observation::DescribeChangeFrom(const Observation& previous) {
  std::vector<std::string> notes;

  if (previous.url != url) {
    notes.push_back(base::StrCat({"you are now on \"", title, "\""}));
  } else if (previous.title != title) {
    // Same address, new title: this is how a single-page app announces that it
    // became something else, and it is invisible if you only watch the URL.
    notes.push_back(base::StrCat({"the page became \"", title, "\""}));
  }

  if (media_playing && !previous.media_playing) {
    notes.push_back("it started playing media");
  } else if (!media_playing && previous.media_playing) {
    notes.push_back("it stopped playing media");
  }

  // What appeared. Named, because "8 new things" is not actionable and
  // "including Checkout and Place order" is.
  std::set<std::string> before;
  for (const ObservedNode& node : previous.elements) {
    before.insert(node.name);
  }
  std::vector<std::string> arrived;
  for (const ObservedNode& node : elements) {
    if (!node.name.empty() && !before.count(node.name)) {
      arrived.push_back(node.name);
    }
  }
  if (!arrived.empty()) {
    std::string note =
        base::StrCat({base::NumberToString(arrived.size()), " new thing",
                      arrived.size() == 1 ? "" : "s", " appeared"});
    const size_t named = arrived.size() < 2 ? arrived.size() : 2;
    for (size_t i = 0; i < named; ++i) {
      base::StrAppend(&note, {i == 0 ? ", including \"" : "\" and \"",
                              arrived[i]});
    }
    if (named > 0) {
      note += "\"";
    }
    notes.push_back(std::move(note));
  }

  // Fields whose contents changed.
  //
  // Compared by name, because ids are reissued every look and a field is the
  // same field to a person if it is still called the same thing. The value
  // itself is not repeated here -- it is already in the elements list, and
  // some of it is redacted.
  std::map<std::string, std::string> held_before;
  for (const ObservedNode& node : previous.elements) {
    if (!node.name.empty()) {
      held_before[node.name] = node.value;
    }
  }
  std::vector<std::string> refilled;
  for (const ObservedNode& node : elements) {
    const auto found = held_before.find(node.name);
    if (found != held_before.end() && found->second != node.value) {
      refilled.push_back(node.name);
    }
  }
  if (!refilled.empty()) {
    std::string note = base::StrCat(
        {base::NumberToString(refilled.size()), " field",
         refilled.size() == 1 ? "" : "s", " now hold different text"});
    const size_t named = refilled.size() < 2 ? refilled.size() : 2;
    for (size_t i = 0; i < named; ++i) {
      base::StrAppend(&note,
                      {i == 0 ? ", including \"" : "\" and \"", refilled[i]});
    }
    if (named > 0) {
      note += "\"";
    }
    notes.push_back(std::move(note));
  }

  if (notes.empty()) {
    // Saying nothing changed is as useful as saying what did. It is the
    // difference between "that did not work" and "that worked and I cannot
    // tell", and the agent kept guessing wrong about exactly this.
    changed = "nothing on the page changed";
    return;
  }
  changed = base::JoinString(notes, "; ");
}

std::vector<const ObservedNode*> Observation::Matching(
    std::string_view query) const {
  std::vector<const ObservedNode*> matches;
  const std::string needle = base::ToLowerASCII(query);
  for (const ObservedNode& node : elements) {
    if (needle.empty() ||
        base::ToLowerASCII(node.name).find(needle) != std::string::npos ||
        node.role == needle) {
      matches.push_back(&node);
    }
  }
  return matches;
}

std::string Observation::ToJson(int level) const {
  base::DictValue root;
  if (loading) {
    root.Set("loading", true);
  }
  root.Set("url", url);
  root.Set("title", title);
  if (!changed.empty()) {
    // Right at the top, because it is the answer to the question the model is
    // actually asking: did what I just did work?
    root.Set("what_changed", changed);
  }
  if (media_playing) {
    // At the top level, beside url and title, because it describes the PAGE
    // rather than any one element -- and because for a task about playing
    // something, this is the answer.
    root.Set("media_playing", true);
  }

  if (level >= 1) {
    base::ListValue list;
    for (const ObservedNode& node : elements) {
      base::DictValue entry;
      entry.Set("id", node.id);
      entry.Set("role", node.role);
      entry.Set("name", node.name);
      if (node.offscreen) {
        entry.Set("offscreen", true);
      }
      if (!node.value.empty()) {
        entry.Set("value", node.value);
      }
      if (!focused_id.empty() && node.id == focused_id) {
        entry.Set("focused", true);
      }
      if (!node.posted.empty()) {
        // The one fact "which is the latest" turns on, in the same place for
        // every candidate so they can be compared.
        entry.Set("posted", node.posted);
      }
      if (!node.detail.empty()) {
        // The line under the title: who posted it, how many views, how long
        // ago. Without this "the latest one" has no answer on the page.
        entry.Set("detail", node.detail);
      }
      if (node.obscured) {
        // Said plainly, because "covered" and "missing" call for completely
        // different next steps and the model cannot see the difference.
        entry.Set("covered_by_something", true);
      }
      if (!node.group.empty()) {
        // Which repeated set this belongs to, and how big it is. This is what
        // tells a search result apart from the filter chip beside it.
        entry.Set("set", node.group);
        entry.Set("set_size", static_cast<int>(node.group_size));
      }
      list.Append(std::move(entry));
    }
    root.Set("elements", std::move(list));
    root.Set("text", text);
    if (!vision_summary.empty()) {
      // Named "looks_like" rather than "vision" so the model reads it as a
      // second opinion about appearance, not as the authoritative list. The
      // elements above are what it can actually name and act on.
      root.Set("looks_like", vision_summary);
    }
    if (truncated) {
      // Said plainly, because "these are the elements" and "these are the
      // first hundred" lead the model to different next steps.
      root.Set("truncated", true);
    }
  }

  std::string json;
  base::JSONWriter::Write(root, &json);
  return json;
}

Observation BuildObservation(const ui::AXTreeUpdate& update,
                             const std::string& url,
                             const std::string& title,
                             size_t max_elements,
                             size_t max_text_length) {
  Observation observation;
  observation.url = url;
  // Bounded like everything else the page writes. It was the one string that
  // went through untouched, and it is read three times over -- in the JSON, in
  // what_changed, and in the arrival note -- so a page with a huge title
  // crowded out the page it was describing.
  observation.title = Shorten(title, kMaxTitleLength);
  observation.tree_id = update.tree_data.tree_id;

  // A snapshot that will not unserialize is a snapshot we cannot read. An empty
  // Observation is the honest result: it offers nothing, so every element id
  // the model could name is ungrounded and gets refused.
  // Checked BEFORE handing it over, because Unserialize does not merely fail on
  // a rootless update any more -- it DCHECKs. Upstream tightened that, and a
  // snapshot with no root is a thing that genuinely arrives: a tab that has not
  // committed, a frame torn down mid-capture. Asking the question ourselves
  // turns a browser crash in any DCHECK build back into the empty Observation
  // this was always meant to return.
  if (update.nodes.empty() || update.root_id == ui::kInvalidAXNodeID) {
    return observation;
  }

  ui::AXTree tree;
  if (!tree.Unserialize(update) || !tree.root()) {
    return observation;
  }

  // Content first, then the navigation and footer links.
  //
  // The cap therefore falls on the parts of the page that are the same
  // everywhere, rather than on the part the task is about. Before this, a
  // YouTube results page could spend most of its sixty slots on the guide rail
  // and the footer, and a model asked to play a video picked "Copyright".
  //
  // Order, not exclusion. Chrome is still offered -- "Sign in" and "Home" are
  // real things to click -- just after the content, and it is what gets dropped
  // when there is too much.
  std::vector<ObservedNode> content;
  std::vector<ObservedNode> chrome;
  Collect(tree, *tree.root(), /*inside_chrome=*/false, content, chrome);

  for (std::vector<ObservedNode>* group : {&content, &chrome}) {
    for (ObservedNode& node : *group) {
      if (observation.elements.size() >= max_elements) {
        observation.truncated = true;
        break;
      }
      // Issued here, so the numbers run in the order the model is shown them.
      node.id = base::StrCat(
          {"e", base::NumberToString(observation.elements.size() + 1)});
      observation.elements.push_back(std::move(node));
    }
  }

  // Which of the issued ids, if any, the page has focused.
  const ui::AXNodeID focused = update.tree_data.focus_id;
  if (focused != ui::kInvalidAXNodeID) {
    for (const ObservedNode& node : observation.elements) {
      if (node.ax_id == focused) {
        observation.focused_id = node.id;
        break;
      }
    }
  }

  // What is actually on top, and what is a repetition of what.
  //
  // Both are facts the browser can state and a screenshot-driven agent has to
  // infer from pixels. Neither costs a step: they ride along with the
  // Observation the model is already shown.
  std::vector<PlacedNode> placed;
  PlaceAll(tree, *tree.root(), placed);
  for (ObservedNode& element : observation.elements) {
    if (element.offscreen || element.bounds.IsEmpty()) {
      continue;
    }
    element.obscured =
        IsCoveredAt(placed, tree.GetFromId(element.ax_id),
                    element.bounds.CenterPoint());
  }
  FindGroups(observation.elements);

  observation.text = tree.root()->GetTextContentUTF8();
  if (observation.text.size() > max_text_length) {
    base::TruncateUTF8ToByteSize(observation.text, max_text_length,
                               &observation.text);
    observation.truncated = true;
  }

  return observation;
}

}  // namespace zephyrus::agent
