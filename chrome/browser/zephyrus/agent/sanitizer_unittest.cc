// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// What leaves the machine, and what does not.
//
// These are the tests with the least room for "close enough". A miss here puts
// a real address or card number on the wire, and unlike a wrong click it cannot
// be undone by trying again.

#include "chrome/browser/zephyrus/agent/sanitizer.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

ObservedNode Field(const std::string& role,
                   const std::string& name,
                   const std::string& value) {
  ObservedNode node;
  node.id = "e1";
  node.role = role;
  node.name = name;
  node.value = value;
  node.bounds = gfx::Rect(10, 20, 200, 30);
  return node;
}

TEST(SanitizerTest, APasswordFieldIsSensitiveBecauseThePageSaysSo) {
  // The one classification that needs no guessing: the page marked the field
  // protected itself.
  EXPECT_EQ(ClassifyElement(Field("password", "Password", "")),
            Sensitivity::kPassword);
}

TEST(SanitizerTest, AFieldIsJudgedByItsLabelBeforeItIsFilled) {
  // Waiting for a value would mean the mask arrives one keystroke late. What
  // the box is FOR is knowable from the moment the page loads.
  EXPECT_EQ(ClassifyElement(Field("textbox", "Email address", "")),
            Sensitivity::kEmail);
  EXPECT_EQ(ClassifyElement(Field("textbox", "Card number", "")),
            Sensitivity::kPaymentCard);
  EXPECT_EQ(ClassifyElement(Field("textbox", "Mobile", "")),
            Sensitivity::kPhone);
}

TEST(SanitizerTest, ContentGivesItAwayWhenTheLabelDoesNot) {
  // A field called "Contact" holding an address is still holding an address.
  EXPECT_EQ(ClassifyElement(Field("textbox", "Contact", "pranav@gmail.com")),
            Sensitivity::kEmail);
  EXPECT_EQ(ClassifyElement(Field("textbox", "Reference", "4111 1111 1111 1111")),
            Sensitivity::kPaymentCard);
}

TEST(SanitizerTest, OrdinaryFieldsAreLeftAlone) {
  // Over-redaction is the safer failure, but it is still a failure: a model
  // that cannot see the search box cannot use it.
  EXPECT_EQ(ClassifyElement(Field("searchbox", "Search", "sidemen")),
            Sensitivity::kNone);
  EXPECT_EQ(ClassifyElement(Field("button", "Submit", "")),
            Sensitivity::kNone);
  EXPECT_EQ(ClassifyElement(Field("textbox", "Quantity", "2")),
            Sensitivity::kNone);
}

TEST(SanitizerTest, TheValueGoesAndTheStructureStays) {
  // The whole design in one assertion. The server is told there IS an email
  // field, that it is filled, and what it is called -- never what is in it.
  Observation observation;
  observation.elements.push_back(Field("textbox", "Email", "pranav@gmail.com"));

  RedactObservation(observation);

  EXPECT_EQ(observation.elements[0].value, kRedactedMarker);
  EXPECT_EQ(observation.elements[0].name, "Email") << "the label is not private";
  EXPECT_EQ(observation.elements[0].role, "textbox") << "the role is not private";

  const std::string json = observation.ToJson(1);
  EXPECT_EQ(json.find("pranav@gmail.com"), std::string::npos)
      << "the address survived into what gets sent: " << json;
}

TEST(SanitizerTest, PrivateThingsInPageTextAreReplacedToo) {
  // A form is not the only way an address reaches a page. Text has no
  // structure to reason about, so anything that looks private goes.
  Observation observation;
  observation.text = "Contact pranav@gmail.com or call 9876543210 for details";

  RedactObservation(observation);

  EXPECT_EQ(observation.text.find("pranav@gmail.com"), std::string::npos)
      << observation.text;
  EXPECT_EQ(observation.text.find("9876543210"), std::string::npos)
      << observation.text;
  // And the sentence still reads as a sentence, so the model keeps the context.
  EXPECT_NE(observation.text.find("Contact"), std::string::npos)
      << observation.text;
  EXPECT_NE(observation.text.find("for details"), std::string::npos)
      << observation.text;
}

TEST(SanitizerTest, RedactionsCarryWhereToPaint) {
  // The picture has to be masked in the same places, so each redaction has to
  // know where its element sits.
  Observation observation;
  observation.elements.push_back(Field("password", "Password", ""));

  const std::vector<Redaction> found = FindRedactions(observation);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].kind, Sensitivity::kPassword);
  EXPECT_EQ(found[0].bounds, gfx::Rect(10, 20, 200, 30));
}

TEST(SanitizerTest, AnOffscreenElementIsNotPaintedOver) {
  // An offscreen element's bounds name the edge it went behind, not the
  // element. Painting there would black out an unrelated part of the picture
  // while leaving the real field visible -- worse than doing nothing.
  Observation observation;
  ObservedNode hidden = Field("textbox", "Email", "pranav@gmail.com");
  hidden.offscreen = true;
  observation.elements.push_back(std::move(hidden));

  const std::vector<Redaction> found = FindRedactions(observation);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_TRUE(found[0].bounds.IsEmpty())
      << "it would have painted over the wrong part of the screen";
}

}  // namespace
}  // namespace zephyrus::agent
