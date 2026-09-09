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

TEST(SanitizerTest, APrivateThingInTheNameIsFound) {
  // The gap: an element whose accessible NAME is the private thing. There is no
  // field and no label, so the label hints miss it and the value is empty --
  // and it went out untouched in the JSON and unpainted in the picture.
  ObservedNode account =
      Field("button", "Signed in as pranav@gmail.com", "");
  EXPECT_EQ(ClassifyElement(account), Sensitivity::kEmail);

  Observation observation;
  observation.elements.push_back(account);
  EXPECT_EQ(FindRedactions(observation).size(), 1u)
      << "the picture would not have been masked there either";
}

TEST(SanitizerTest, RedactingANameKeepsTheThingClickable) {
  // Word-wise, because the name is also how the model refers to the element.
  // Blanking it whole would hide the address and the button with it.
  Observation observation;
  observation.elements.push_back(
      Field("button", "Signed in as pranav@gmail.com", ""));

  RedactObservation(observation);

  const std::string& name = observation.elements[0].name;
  EXPECT_EQ(name.find("pranav@gmail.com"), std::string::npos) << name;
  EXPECT_NE(name.find("Signed in as"), std::string::npos)
      << "the button lost the words that make it findable: " << name;
}

TEST(SanitizerTest, ALabelIsNotMistakenForAValue) {
  // The other half of the same rule. "Email address" is what the box is CALLED,
  // and redacting the label would leave a nameless box the model cannot use.
  Observation observation;
  observation.elements.push_back(
      Field("textbox", "Email address", "pranav@gmail.com"));

  RedactObservation(observation);

  EXPECT_EQ(observation.elements[0].name, "Email address");
  EXPECT_EQ(observation.elements[0].value, kRedactedMarker);
}

TEST(SanitizerTest, TheTitleIsRedactedToo) {
  // A page is free to put whatever it likes in its own title, and some do put
  // the signed-in address there. The title is read three separate times on the
  // way to a prompt, so leaving it out of this undid the rest.
  Observation observation;
  observation.title = "Inbox - pranav@gmail.com";

  RedactObservation(observation);

  EXPECT_EQ(observation.title.find("pranav@gmail.com"), std::string::npos)
      << observation.title;
}

TEST(SanitizerTest, ATitleThatHappensToContainDigitsIsLeftAlone) {
  // The control on my own rule. Reading a NAME as content is right for an
  // address and wrong for a number: a name is a title as often as it is data,
  // and a false positive does not merely lose a word -- it paints a black
  // rectangle over that element in the screenshot and takes the title the task
  // was about with it.
  Observation observation;
  observation.elements.push_back(
      Field("link", "I Spent 1000000 Dollars", ""));

  EXPECT_EQ(ClassifyElement(observation.elements[0]), Sensitivity::kNone);
  EXPECT_TRUE(FindRedactions(observation).empty())
      << "the video the task was about would have been blacked out";

  RedactObservation(observation);
  EXPECT_EQ(observation.elements[0].name, "I Spent 1000000 Dollars");
}

TEST(SanitizerTest, DigitsInAFieldValueAreStillCaught) {
  // The other side of the same line. Ten digits typed into a box is a phone
  // number; ten digits in a headline is a headline. Narrowing the rule for
  // names must not have narrowed it for values.
  Observation observation;
  observation.elements.push_back(Field("textbox", "Contact", "9876543210"));

  EXPECT_EQ(ClassifyElement(observation.elements[0]), Sensitivity::kPhone);
  RedactObservation(observation);
  EXPECT_EQ(observation.elements[0].value, kRedactedMarker);
}

TEST(SanitizerTest, TheVerdictIsRecordedBeforeTheValueIsReplaced) {
  // Order matters, and getting it wrong is invisible: classify after redacting
  // and a card field known only by its digits reads as harmless, because the
  // digits are already gone. The policy that refuses to type card details
  // reads this field, so a stale verdict there is a security rule that passes
  // its own tests and never fires.
  Observation observation;
  observation.elements.push_back(Field("textbox", "", "4111111111111111"));

  RedactObservation(observation);

  EXPECT_EQ(observation.elements[0].sensitivity, "payment_card");
  EXPECT_EQ(observation.elements[0].value, kRedactedMarker);
}

TEST(SanitizerTest, TheLineAroundAnElementIsRedactedToo) {
  // A new field leaked what every other field was masking.
  //
  // `detail` carries the prose beside an element, added so that "which of these
  // is newest" has an answer. It went out raw: the value beside it said
  // [redacted] and the detail said the address in full, plus a phone number.
  // That is the exact shape of the bug this file exists to stop, in a field
  // that did not exist when it was written -- so the test is about the
  // PRINCIPLE: every channel carrying page text is a channel that leaks.
  Observation observation;
  ObservedNode plain = Field("button", "Menu", "");
  plain.detail = "We will write to pranav@gmail.com or call 9876543210.";
  observation.elements.push_back(std::move(plain));

  RedactObservation(observation);

  const std::string& detail = observation.elements[0].detail;
  EXPECT_EQ(detail.find("pranav@gmail.com"), std::string::npos) << detail;
  EXPECT_EQ(detail.find("9876543210"), std::string::npos) << detail;
  // And it is still a useful caption.
  EXPECT_NE(detail.find("We will write to"), std::string::npos) << detail;
}

}  // namespace
}  // namespace zephyrus::agent
