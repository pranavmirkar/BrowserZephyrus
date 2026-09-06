// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// What the browser decides to show the model.
//
// BuildObservation is pure, so these are the cheapest tests in the feature and
// they cover the decisions with the most consequence: which roles the agent is
// even offered, what a password field looks like, and where the caps fall.

#include "chrome/browser/zephyrus/agent/observation.h"

#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree_id.h"
#include "ui/accessibility/ax_tree_update.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"

namespace zephyrus::agent {
namespace {

ui::AXNodeData MakeNode(int id,
                        ax::mojom::Role role,
                        const std::string& name = std::string()) {
  ui::AXNodeData node;
  node.id = id;
  node.role = role;
  if (!name.empty()) {
    node.SetName(name);
  }
  return node;
}

// A document whose root has `children`, in order.
ui::AXTreeUpdate MakeTree(std::vector<ui::AXNodeData> children) {
  ui::AXNodeData root = MakeNode(1, ax::mojom::Role::kRootWebArea);
  for (const ui::AXNodeData& child : children) {
    root.child_ids.push_back(child.id);
  }

  ui::AXTreeUpdate update;
  update.root_id = root.id;
  update.nodes.push_back(root);
  for (ui::AXNodeData& child : children) {
    update.nodes.push_back(std::move(child));
  }
  update.has_tree_data = true;
  update.tree_data.tree_id = ui::AXTreeID::CreateNewAXTreeID();
  return update;
}

Observation Build(std::vector<ui::AXNodeData> children,
                  size_t max_elements = kMaxObservedElements) {
  return BuildObservation(MakeTree(std::move(children)),
                          "https://example.org/page", "A page", max_elements,
                          kMaxObservedTextLength);
}

TEST(ObservationTest, IssuesIdsInDocumentOrder) {
  Observation observation = Build({
      MakeNode(2, ax::mojom::Role::kLink, "Specifications"),
      MakeNode(3, ax::mojom::Role::kButton, "Buy now"),
  });

  ASSERT_EQ(observation.elements.size(), 2u);
  EXPECT_EQ(observation.elements[0].id, "e1");
  EXPECT_EQ(observation.elements[0].role, "link");
  EXPECT_EQ(observation.elements[0].name, "Specifications");
  EXPECT_EQ(observation.elements[0].ax_id, 2);
  EXPECT_EQ(observation.elements[1].id, "e2");
  EXPECT_EQ(observation.elements[1].role, "button");
}

TEST(ObservationTest, LeavesOutRolesThatAreNotControls) {
  // A page is mostly structure. Offering it would bury the handful of controls
  // that matter under a page's worth of divs.
  Observation observation = Build({
      MakeNode(2, ax::mojom::Role::kParagraph, "Some prose"),
      MakeNode(3, ax::mojom::Role::kHeading, "A heading"),
      MakeNode(4, ax::mojom::Role::kGenericContainer, "A div"),
      MakeNode(5, ax::mojom::Role::kButton, "Buy now"),
  });

  ASSERT_EQ(observation.elements.size(), 1u);
  EXPECT_EQ(observation.elements[0].name, "Buy now");
}

TEST(ObservationTest, ANamelessControlIsNotOffered) {
  // Neither is worth putting in front of a model: it has nothing to choose by.
  //
  // Text fields used to be an exception, on the theory that an input is
  // identifiable by being the only one. That is false on a real page -- YouTube
  // offers an unnamed input beside its named search box -- and a real task
  // spent eight steps typing into the anonymous one.
  Observation observation = Build({
      MakeNode(2, ax::mojom::Role::kButton),
      MakeNode(3, ax::mojom::Role::kTextField),
  });

  EXPECT_TRUE(observation.elements.empty());
}

TEST(ObservationTest, AFieldIsNamedByItsPlaceholderWhenItHasNoName) {
  // How a person reads a form when there is no label.
  ui::AXNodeData field = MakeNode(2, ax::mojom::Role::kTextField);
  field.AddStringAttribute(ax::mojom::StringAttribute::kPlaceholder,
                           "Search YouTube");

  Observation observation = Build({std::move(field)});

  ASSERT_EQ(observation.elements.size(), 1u);
  EXPECT_EQ(observation.elements[0].name, "Search YouTube");
}

TEST(ObservationTest, ADescriptionIsTheLastResort) {
  ui::AXNodeData field = MakeNode(2, ax::mojom::Role::kTextField);
  field.AddStringAttribute(ax::mojom::StringAttribute::kDescription,
                           "Where to go");

  Observation observation = Build({std::move(field)});

  ASSERT_EQ(observation.elements.size(), 1u);
  EXPECT_EQ(observation.elements[0].name, "Where to go");
}

TEST(ObservationTest, TheRealNameWinsOverAPlaceholder) {
  ui::AXNodeData field = MakeNode(2, ax::mojom::Role::kTextField, "Search");
  field.AddStringAttribute(ax::mojom::StringAttribute::kPlaceholder,
                           "Type here");

  Observation observation = Build({std::move(field)});

  ASSERT_EQ(observation.elements.size(), 1u);
  EXPECT_EQ(observation.elements[0].name, "Search");
}

TEST(ObservationTest, AProtectedFieldIsCalledAPassword) {
  // Chromium has no password role -- it is a text field with a protected
  // state. The policy engine refuses to type into one, and it can only do that
  // if the Observation tells them apart.
  ui::AXNodeData password = MakeNode(2, ax::mojom::Role::kTextField, "Password");
  password.AddState(ax::mojom::State::kProtected);

  Observation observation = Build({
      MakeNode(3, ax::mojom::Role::kTextField, "Email"),
      std::move(password),
  });

  ASSERT_EQ(observation.elements.size(), 2u);
  EXPECT_EQ(observation.elements[0].role, "textbox");
  EXPECT_EQ(observation.elements[1].role, "password");
}

TEST(ObservationTest, SkipsAnInvisibleSubtreeEntirely) {
  ui::AXNodeData hidden = MakeNode(2, ax::mojom::Role::kGenericContainer);
  hidden.AddState(ax::mojom::State::kInvisible);
  hidden.child_ids.push_back(4);

  ui::AXNodeData inside = MakeNode(4, ax::mojom::Role::kButton, "Hidden button");

  ui::AXTreeUpdate update = MakeTree({
      std::move(hidden),
      MakeNode(3, ax::mojom::Role::kButton, "Visible button"),
  });
  update.nodes.push_back(std::move(inside));

  Observation observation =
      BuildObservation(update, "https://example.org/page", "A page",
                       kMaxObservedElements, kMaxObservedTextLength);

  ASSERT_EQ(observation.elements.size(), 1u);
  EXPECT_EQ(observation.elements[0].name, "Visible button");
}

TEST(ObservationTest, WalksThroughAnIgnoredWrapper) {
  // Ignored is not invisible. An ignored node should not be offered itself, but
  // its children still should -- and real documents are full of ignored
  // wrappers (html, body, layout containers).
  //
  // This is a regression test with a story: pruning the subtree at an ignored
  // node made BuildObservation return NOTHING on every real page, while every
  // synthetic test here still passed because none of them had a wrapper.
  ui::AXNodeData wrapper = MakeNode(2, ax::mojom::Role::kGenericContainer);
  wrapper.AddState(ax::mojom::State::kIgnored);
  wrapper.child_ids.push_back(3);

  ui::AXTreeUpdate update = MakeTree({std::move(wrapper)});
  update.nodes.push_back(MakeNode(3, ax::mojom::Role::kButton, "Continue"));

  Observation observation =
      BuildObservation(update, "https://example.org/page", "A page",
                       kMaxObservedElements, kMaxObservedTextLength);

  ASSERT_EQ(observation.elements.size(), 1u)
      << "an ignored wrapper hid the control underneath it";
  EXPECT_EQ(observation.elements[0].name, "Continue");
}

TEST(ObservationTest, CapsTheElementsAndSaysSo) {
  std::vector<ui::AXNodeData> many;
  for (int i = 0; i < 10; ++i) {
    many.push_back(MakeNode(i + 2, ax::mojom::Role::kButton,
                            "Button " + base::NumberToString(i)));
  }

  Observation observation = Build(std::move(many), /*max_elements=*/3);
  EXPECT_EQ(observation.elements.size(), 3u);
  EXPECT_TRUE(observation.truncated)
      << "a capped list must say it was capped, or the model reads it as "
         "the whole page";
}

TEST(ObservationTest, AnUnreadableSnapshotOffersNothing) {
  // No root, so nothing can be read. Offering nothing is the honest result:
  // every element id the model could name is then ungrounded and gets refused.
  ui::AXTreeUpdate broken;
  Observation observation = BuildObservation(broken, "https://example.org/", "",
                                             kMaxObservedElements,
                                             kMaxObservedTextLength);
  EXPECT_TRUE(observation.elements.empty());
  EXPECT_EQ(observation.url, "https://example.org/");
}

TEST(ObservationTest, FindsOnlyIdsItIssued) {
  Observation observation = Build({
      MakeNode(2, ax::mojom::Role::kButton, "Buy now"),
  });

  ASSERT_TRUE(observation.Find("e1"));
  EXPECT_EQ(observation.Find("e1")->ax_id, 2);
  EXPECT_FALSE(observation.Find("e2"));
  EXPECT_FALSE(observation.Find("checkout-button"));
}

TEST(ObservationTest, MatchesOnNameAndRole) {
  Observation observation = Build({
      MakeNode(2, ax::mojom::Role::kButton, "Buy now"),
      MakeNode(3, ax::mojom::Role::kLink, "Shipping details"),
      MakeNode(4, ax::mojom::Role::kSearchBox, "Search reviews"),
  });

  EXPECT_EQ(observation.Matching("shipping").size(), 1u);
  EXPECT_EQ(observation.Matching("SHIPPING").size(), 1u);
  EXPECT_EQ(observation.Matching("searchbox").size(), 1u);
  EXPECT_EQ(observation.Matching("nothing here").size(), 0u);
}

TEST(ObservationTest, LevelZeroLeavesTheElementsOut) {
  Observation observation = Build({
      MakeNode(2, ax::mojom::Role::kButton, "Buy now"),
  });

  const std::string brief = observation.ToJson(0);
  EXPECT_EQ(brief.find("elements"), std::string::npos) << brief;
  EXPECT_NE(brief.find("A page"), std::string::npos) << brief;

  const std::string full = observation.ToJson(1);
  EXPECT_NE(full.find("Buy now"), std::string::npos) << full;
}

TEST(ObservationTest, DoesNotShowTheModelRealNodeIds) {
  // The model works in issued ids so a made-up one resolves to nothing. Leaking
  // the accessibility id would give it something real to guess at.
  Observation observation = Build({
      MakeNode(4242, ax::mojom::Role::kButton, "Buy now"),
  });

  const std::string json = observation.ToJson(1);
  EXPECT_EQ(json.find("4242"), std::string::npos) << json;
}

// Where an element is, which is what the pointer is aimed at.
//
// These matter more than they look. page.type clicks the field before filling
// it, because accessibility focus actions do not work here -- so bounds that
// came back empty, or a visible field wrongly called offscreen, would silently
// turn the click back off and put the caret nowhere. That failure is invisible
// from the outside: the text still lands, and only a later Enter goes missing.
//
// The tree is built by hand rather than through MakeTree because the numbers
// are the subject: a viewport that clips its children, and a control placed in
// it deliberately.
ui::AXTreeUpdate MakeViewport(std::vector<ui::AXNodeData> children) {
  ui::AXNodeData root = MakeNode(1, ax::mojom::Role::kRootWebArea);
  root.relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);
  // Without this the tree does no clipping at all, and "offscreen" would never
  // be true no matter where a node sat -- a test that could not fail.
  root.AddBoolAttribute(ax::mojom::BoolAttribute::kClipsChildren, true);
  for (const ui::AXNodeData& child : children) {
    root.child_ids.push_back(child.id);
  }

  ui::AXTreeUpdate update;
  update.root_id = root.id;
  update.nodes.push_back(root);
  for (ui::AXNodeData& child : children) {
    update.nodes.push_back(std::move(child));
  }
  update.has_tree_data = true;
  update.tree_data.tree_id = ui::AXTreeID::CreateNewAXTreeID();
  return update;
}

ui::AXNodeData MakeNodeAt(int id,
                          ax::mojom::Role role,
                          const std::string& name,
                          const gfx::RectF& bounds) {
  ui::AXNodeData node = MakeNode(id, role, name);
  node.relative_bounds.bounds = bounds;
  return node;
}

TEST(ObservationTest, ReportsWhereAVisibleControlIs) {
  Observation observation = BuildObservation(
      MakeViewport({MakeNodeAt(2, ax::mojom::Role::kSearchBox, "Search",
                               gfx::RectF(100, 200, 120, 40))}),
      "https://example.org/page", "A page", kMaxObservedElements,
      kMaxObservedTextLength);

  ASSERT_EQ(observation.elements.size(), 1u);
  EXPECT_EQ(observation.elements[0].bounds, gfx::Rect(100, 200, 120, 40));
  EXPECT_FALSE(observation.elements[0].offscreen);
  // The centre is the point that gets clicked, so say it outright.
  EXPECT_EQ(observation.elements[0].bounds.CenterPoint(), gfx::Point(160, 220));
}

TEST(ObservationTest, MarksAControlScrolledOutOfViewAsOffscreen) {
  // Far below the fold. Its bounds get clipped to the edge it went behind, so
  // they no longer name a point on it -- clicking their centre would press on
  // whatever is at that edge instead. The flag is what stops that.
  Observation observation = BuildObservation(
      MakeViewport({MakeNodeAt(2, ax::mojom::Role::kButton, "Subscribe",
                               gfx::RectF(100, 5000, 120, 40))}),
      "https://example.org/page", "A page", kMaxObservedElements,
      kMaxObservedTextLength);

  ASSERT_EQ(observation.elements.size(), 1u);
  EXPECT_TRUE(observation.elements[0].offscreen);
}

TEST(ObservationTest, DoesNotShowTheModelWhereThingsAre) {
  // Bounds are internal for the same reason node ids are, and more so: this is
  // the point a real pointer is sent to. A model that could name one could
  // click anywhere on the page while claiming to click a button.
  Observation observation = BuildObservation(
      MakeViewport({MakeNodeAt(2, ax::mojom::Role::kButton, "Buy now",
                               gfx::RectF(317, 429, 120, 40))}),
      "https://example.org/page", "A page", kMaxObservedElements,
      kMaxObservedTextLength);

  const std::string json = observation.ToJson(1);
  EXPECT_EQ(json.find("317"), std::string::npos) << json;
  EXPECT_EQ(json.find("429"), std::string::npos) << json;
}

TEST(ObservationTest, ShortensARunOnNameToSomethingChoosable) {
  // Measured on youtube.com. A link wrapping the channel card came back named
  // with everything inside it, because an accessible name is computed from the
  // element's contents. A model choosing between a dozen of those is reading
  // paragraphs rather than labels, and it chose badly.
  const std::string run_on =
      "Sidemen Verified @Sidemen 23.4M subscribers Welcome to the official "
      "Sidemen channel. The home of #SidemenSundays - We post new Sidemen "
      "videos every single Sunday!";
  Observation observation = Build({
      MakeNode(2, ax::mojom::Role::kLink, run_on),
  });

  ASSERT_EQ(observation.elements.size(), 1u);
  const std::string& name = observation.elements[0].name;
  EXPECT_LT(name.size(), run_on.size());
  EXPECT_LE(name.size(), 144u) << name;
  // The cap is 140 rather than 80 because a video's link name carries the
  // channel, the view count and the upload age after the title, and cutting at
  // 80 threw away the very field a question like "the latest one" turns on.
  // The front is kept, because that is the part a person reads to choose.
  EXPECT_EQ(name.rfind("Sidemen Verified", 0), 0u) << name;
  // And it stops on a word, not mid-way through one.
  EXPECT_NE(name.find("..."), std::string::npos) << name;
}

TEST(ObservationTest, KeepsAShortNameExactlyAsItIs) {
  // The other half: shortening must not touch a name that was already usable,
  // or every label in the Observation would end in an ellipsis.
  Observation observation = Build({
      MakeNode(2, ax::mojom::Role::kButton, "Subscribe"),
  });

  ASSERT_EQ(observation.elements.size(), 1u);
  EXPECT_EQ(observation.elements[0].name, "Subscribe");
}

TEST(ObservationTest, CollapsesTheWhitespaceAPageLaidOutWith) {
  // Markup indentation reaches the accessible name as runs of spaces and
  // newlines. They mean nothing on one line and cost prompt space on every
  // step.
  Observation observation = Build({
      MakeNode(2, ax::mojom::Role::kLink, "Watch\n   later   now"),
  });

  ASSERT_EQ(observation.elements.size(), 1u);
  EXPECT_EQ(observation.elements[0].name, "Watch later now");
}

// A page laid out like a real one: a navigation rail, then the content, then a
// footer. This is the shape that made an agent click "Copyright".
ui::AXTreeUpdate MakeSiteShapedPage(int nav_links,
                                    int content_links,
                                    int footer_links) {
  ui::AXNodeData root = MakeNode(1, ax::mojom::Role::kRootWebArea);
  std::vector<ui::AXNodeData> nodes;
  int next = 2;

  ui::AXNodeData nav = MakeNode(next++, ax::mojom::Role::kNavigation);
  for (int i = 0; i < nav_links; ++i) {
    ui::AXNodeData link = MakeNode(next++, ax::mojom::Role::kLink,
                                   "Nav " + base::NumberToString(i));
    nav.child_ids.push_back(link.id);
    nodes.push_back(std::move(link));
  }

  ui::AXNodeData main = MakeNode(next++, ax::mojom::Role::kMain);
  for (int i = 0; i < content_links; ++i) {
    ui::AXNodeData link = MakeNode(next++, ax::mojom::Role::kLink,
                                   "Video " + base::NumberToString(i));
    main.child_ids.push_back(link.id);
    nodes.push_back(std::move(link));
  }

  ui::AXNodeData footer = MakeNode(next++, ax::mojom::Role::kContentInfo);
  for (int i = 0; i < footer_links; ++i) {
    ui::AXNodeData link = MakeNode(
        next++, ax::mojom::Role::kLink,
        i == 0 ? std::string("Copyright") : "Foot " + base::NumberToString(i));
    footer.child_ids.push_back(link.id);
    nodes.push_back(std::move(link));
  }

  root.child_ids = {nav.id, main.id, footer.id};

  ui::AXTreeUpdate update;
  update.root_id = root.id;
  update.nodes.push_back(root);
  update.nodes.push_back(std::move(nav));
  update.nodes.push_back(std::move(main));
  update.nodes.push_back(std::move(footer));
  for (ui::AXNodeData& n : nodes) {
    update.nodes.push_back(std::move(n));
  }
  update.has_tree_data = true;
  update.tree_data.tree_id = ui::AXTreeID::CreateNewAXTreeID();
  return update;
}

TEST(ObservationTest, OffersTheContentBeforeTheNavigation) {
  // The nav comes FIRST in the document, so in document order it would take the
  // early ids and, on a real page, most of the cap. What the task is about has
  // to come first instead.
  Observation observation = BuildObservation(
      MakeSiteShapedPage(/*nav_links=*/5, /*content_links=*/3,
                         /*footer_links=*/4),
      "https://example.org/results", "Results", kMaxObservedElements,
      kMaxObservedTextLength);

  ASSERT_GE(observation.elements.size(), 3u);
  EXPECT_EQ(observation.elements[0].name, "Video 0");
  EXPECT_EQ(observation.elements[1].name, "Video 1");
  EXPECT_EQ(observation.elements[2].name, "Video 2");
  // Chrome is still offered -- "Sign in" is a real thing to click -- just after.
  EXPECT_NE(observation.ToJson(1).find("Copyright"), std::string::npos);
}

TEST(ObservationTest, TheCapFallsOnNavigationNotOnContent) {
  // The actual failure. With forty navigation and footer links ahead of them in
  // the document, a page's real content used to be pushed past the cap entirely
  // -- so a model asked to play a video was offered "Copyright" and not one
  // video. It clicked Copyright.
  Observation observation = BuildObservation(
      MakeSiteShapedPage(/*nav_links=*/40, /*content_links=*/6,
                         /*footer_links=*/14),
      "https://example.org/results", "Results", /*max_elements=*/10,
      kMaxObservedTextLength);

  ASSERT_EQ(observation.elements.size(), 10u);
  EXPECT_TRUE(observation.truncated);
  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(observation.elements[i].name, "Video " + base::NumberToString(i))
        << "content lost its place to the navigation";
  }
  // And the ids run in the order the model is shown them, not document order.
  EXPECT_EQ(observation.elements[0].id, "e1");
}

// --- what is on top, and what is a repetition ---------------------------

// A page with a banner appended last, covering part of what is underneath.
// This is how cookie banners and modals actually work: added at the end of the
// document precisely so they land on top.
ui::AXTreeUpdate MakePageWithBanner() {
  ui::AXNodeData root = MakeNode(1, ax::mojom::Role::kRootWebArea);
  root.relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);

  ui::AXNodeData buried = MakeNode(2, ax::mojom::Role::kButton, "Accept order");
  buried.relative_bounds.bounds = gfx::RectF(100, 400, 200, 40);

  ui::AXNodeData clear = MakeNode(3, ax::mojom::Role::kButton, "Search");
  clear.relative_bounds.bounds = gfx::RectF(100, 100, 200, 40);

  // Appended AFTER, and covering the lower part of the page.
  ui::AXNodeData banner = MakeNode(4, ax::mojom::Role::kGenericContainer);
  banner.relative_bounds.bounds = gfx::RectF(0, 350, 800, 250);

  ui::AXNodeData agree = MakeNode(5, ax::mojom::Role::kButton, "I agree");
  agree.relative_bounds.bounds = gfx::RectF(600, 500, 100, 40);
  banner.child_ids = {agree.id};

  root.child_ids = {buried.id, clear.id, banner.id};

  ui::AXTreeUpdate update;
  update.root_id = root.id;
  update.nodes = {root, buried, clear, banner, agree};
  update.has_tree_data = true;
  update.tree_data.tree_id = ui::AXTreeID::CreateNewAXTreeID();
  return update;
}

TEST(ObservationTest, SaysWhenSomethingIsDrawnOverAnElement) {
  // The commonest reason a click does nothing, and the question vision was
  // going to be needed for. Being inside the browser, it is a fact instead.
  Observation observation = BuildObservation(
      MakePageWithBanner(), "https://example.org/", "Shop",
      kMaxObservedElements, kMaxObservedTextLength);

  const ObservedNode* buried = nullptr;
  const ObservedNode* clear = nullptr;
  const ObservedNode* on_top = nullptr;
  for (const ObservedNode& node : observation.elements) {
    if (node.name == "Accept order") buried = &node;
    if (node.name == "Search") clear = &node;
    if (node.name == "I agree") on_top = &node;
  }

  ASSERT_TRUE(buried && clear && on_top) << observation.ToJson(1);
  EXPECT_TRUE(buried->obscured)
      << "a button under a banner was reported as clickable";
  EXPECT_FALSE(clear->obscured)
      << "a button nowhere near the banner was reported as covered";
  // The banner's own button is on top, so it is not covered by anything.
  EXPECT_FALSE(on_top->obscured)
      << "the thing doing the covering was reported as covered";
}

TEST(ObservationTest, TheModelIsToldWhichThingIsCovered) {
  Observation observation = BuildObservation(
      MakePageWithBanner(), "https://example.org/", "Shop",
      kMaxObservedElements, kMaxObservedTextLength);

  // "Covered" and "missing" call for completely different next steps, and the
  // model cannot tell them apart unless it is said.
  EXPECT_NE(observation.ToJson(1).find("covered_by_something"),
            std::string::npos)
      << observation.ToJson(1);
}

// A results page: a row of filter chips, then a list of results. Both are
// links; only one of them is what a task is ever about.
ui::AXTreeUpdate MakeResultsPage() {
  ui::AXNodeData root = MakeNode(1, ax::mojom::Role::kRootWebArea);
  root.relative_bounds.bounds = gfx::RectF(0, 0, 800, 2000);

  ui::AXNodeData chips = MakeNode(2, ax::mojom::Role::kGenericContainer);
  ui::AXNodeData results = MakeNode(3, ax::mojom::Role::kMain);

  std::vector<ui::AXNodeData> nodes;
  int next = 10;
  for (const char* name : {"Latest", "Popular", "Oldest"}) {
    ui::AXNodeData chip = MakeNode(next++, ax::mojom::Role::kButton, name);
    chip.relative_bounds.bounds = gfx::RectF(10, 10, 80, 30);
    chips.child_ids.push_back(chip.id);
    nodes.push_back(std::move(chip));
  }
  for (int i = 0; i < 5; ++i) {
    ui::AXNodeData video =
        MakeNode(next++, ax::mojom::Role::kLink,
                 "SIDEMEN VIDEO " + base::NumberToString(i));
    video.relative_bounds.bounds = gfx::RectF(10, 100 + i * 200, 400, 180);
    results.child_ids.push_back(video.id);
    nodes.push_back(std::move(video));
  }

  root.child_ids = {chips.id, results.id};

  ui::AXTreeUpdate update;
  update.root_id = root.id;
  update.nodes = {root, chips, results};
  for (ui::AXNodeData& node : nodes) {
    update.nodes.push_back(std::move(node));
  }
  update.has_tree_data = true;
  update.tree_data.tree_id = ui::AXTreeID::CreateNewAXTreeID();
  return update;
}

TEST(ObservationTest, RepetitionsAreLabelledAsASet) {
  // The failure this exists for. On youtube.com the videos and the filter chips
  // arrived as one undifferentiated list of links and buttons, and the model
  // clicked "Latest" and "Videos" over and over while the videos sat beside
  // them looking exactly as important.
  Observation observation = BuildObservation(
      MakeResultsPage(), "https://example.org/results", "Results",
      kMaxObservedElements, kMaxObservedTextLength);

  const ObservedNode* video = nullptr;
  for (const ObservedNode& node : observation.elements) {
    if (node.name == "SIDEMEN VIDEO 0") {
      video = &node;
    }
  }
  ASSERT_TRUE(video) << observation.ToJson(1);

  EXPECT_FALSE(video->group.empty()) << "the results were not seen as a set";
  EXPECT_EQ(video->group_size, 5u);

  // The chips are a set too -- three of them under one parent -- but a
  // DIFFERENT one, which is the whole point: the model can now tell that these
  // are two kinds of thing rather than one list of nine.
  const ObservedNode* chip = nullptr;
  for (const ObservedNode& node : observation.elements) {
    if (node.name == "Latest") {
      chip = &node;
    }
  }
  ASSERT_TRUE(chip);
  EXPECT_NE(chip->group, video->group)
      << "filter chips and results were put in the same set";
}

TEST(ObservationTest, APairIsNotASet) {
  // Two of anything is a pair, not a pattern -- and a pair is as likely to be
  // Cancel and OK as a list. Labelling those as a set would be noise.
  Observation observation = Build({
      MakeNode(2, ax::mojom::Role::kButton, "Cancel"),
      MakeNode(3, ax::mojom::Role::kButton, "OK"),
  });

  ASSERT_EQ(observation.elements.size(), 2u);
  EXPECT_TRUE(observation.elements[0].group.empty());
  EXPECT_TRUE(observation.elements[1].group.empty());
}

}  // namespace
}  // namespace zephyrus::agent
