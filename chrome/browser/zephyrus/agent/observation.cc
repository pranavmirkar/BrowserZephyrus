// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/observation.h"

#include <string_view>
#include <vector>
#include <utility>

#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
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
std::string Shorten(std::string name) {
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
  if (name.size() <= kMaxNameLength) {
    return name;
  }

  // On a character boundary, not a byte one. Cutting a UTF-8 sequence in half
  // would produce a string the JSON writer cannot encode, and the model would
  // lose the whole Observation rather than one long name.
  std::string cut;
  base::TruncateUTF8ToByteSize(name, kMaxNameLength, &cut);

  // Back up to the last space so the label ends on a word. A name that stops
  // mid-word reads as corruption and invites the model to distrust it.
  const size_t space = cut.find_last_of(' ');
  if (space != std::string::npos && space > kMaxNameLength / 2) {
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
  root.Set("url", url);
  root.Set("title", title);

  if (level >= 1) {
    base::ListValue list;
    for (const ObservedNode& node : elements) {
      base::DictValue entry;
      entry.Set("id", node.id);
      entry.Set("role", node.role);
      entry.Set("name", node.name);
      if (!node.value.empty()) {
        entry.Set("value", node.value);
      }
      if (!focused_id.empty() && node.id == focused_id) {
        entry.Set("focused", true);
      }
      list.Append(std::move(entry));
    }
    root.Set("elements", std::move(list));
    root.Set("text", text);
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
  observation.title = title;
  observation.tree_id = update.tree_data.tree_id;

  // A snapshot that will not unserialize is a snapshot we cannot read. An empty
  // Observation is the honest result: it offers nothing, so every element id
  // the model could name is ungrounded and gets refused.
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

  observation.text = tree.root()->GetTextContentUTF8();
  if (observation.text.size() > max_text_length) {
    observation.text.resize(max_text_length);
    observation.truncated = true;
  }

  return observation;
}

}  // namespace zephyrus::agent
