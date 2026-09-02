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

TEST(ObservationTest, SkipsAnUnnamedControlButKeepsAnUnnamedTextField) {
  // An unnamed button is one the model cannot sensibly choose between. An
  // unnamed input still is: the field itself is the thing being pointed at.
  Observation observation = Build({
      MakeNode(2, ax::mojom::Role::kButton),
      MakeNode(3, ax::mojom::Role::kTextField),
  });

  ASSERT_EQ(observation.elements.size(), 1u);
  EXPECT_EQ(observation.elements[0].role, "textbox");
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

}  // namespace
}  // namespace zephyrus::agent
