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

TEST(SanitizerTest, ReadingAPageDoesNotSendItsPrivateTextInTheClear) {
  // page.read pages through `full_text`, which was copied from `text` before
  // redaction and never redacted itself: the short view of a page was masked and
  // the long view of the same page was not.
  Observation observation;
  observation.text = "Ship to Asha Rao, call 9876543210 or write a.rao@example.com.";
  observation.full_text = observation.text + " Card 4111 1111 1111 1111 expires 12/28.";
  RedactObservation(observation);
  for (const std::string* text : {&observation.text, &observation.full_text}) {
    EXPECT_EQ(text->find("9876543210"), std::string::npos) << *text;
    EXPECT_EQ(text->find("a.rao@example.com"), std::string::npos) << *text;
  }
  EXPECT_EQ(observation.full_text.find("4111"), std::string::npos)
      << observation.full_text;
  // Not more than it has to: the rest of the sentence is still there.
  EXPECT_NE(observation.full_text.find("Ship to"), std::string::npos);
}

TEST(SanitizerTest, ANumberWrittenInGroupsIsCaughtWhereverItIsSeen) {
  std::string card = "Pay with 4111 1111 1111 1111 today";
  EXPECT_TRUE(RedactSpacedNumbers(card));
  EXPECT_EQ(card.find("4111"), std::string::npos) << card;
  std::string phone = "Call +91 98765 43210 now";
  EXPECT_TRUE(RedactSpacedNumbers(phone));
  EXPECT_EQ(phone.find("98765"), std::string::npos) << phone;
  // Dates, times and ordinary sums are not.
  for (const char* harmless :
       {"Published 2026-09-30 at 10:43", "Total 1,299.00 rupees",
        "Room 204, floor 3", "Call 15 January 2026"}) {
    std::string text = harmless;
    EXPECT_FALSE(RedactSpacedNumbers(text)) << harmless;
    EXPECT_EQ(text, harmless);
  }
}

TEST(SanitizerTest, ADropDownOfSavedAddressesDoesNotListThem) {
  // The choices a list offers reach the model in their own field. A list of
  // saved cards or addresses is the private thing itself.
  Observation observation;
  ObservedNode list = Field("combobox", "Delivery address", "");
  list.options = {"Home, write to a.rao@example.com", "Office, 9876543210",
                  "Visa 4111 1111 1111 1111", "Pick up in store"};
  observation.elements.push_back(list);
  ObservedNode phone_list = Field("combobox", "Mobile number", "");
  phone_list.options = {"98765 43210", "Other"};
  observation.elements.push_back(phone_list);

  RedactObservation(observation);

  ASSERT_EQ(observation.elements[0].options.size(), 4u);
  for (const std::string& option : observation.elements[0].options) {
    EXPECT_EQ(option.find("a.rao@"), std::string::npos) << option;
    EXPECT_EQ(option.find("9876543210"), std::string::npos) << option;
    EXPECT_EQ(option.find("4111"), std::string::npos) << option;
  }
  EXPECT_EQ(observation.elements[0].options[3], "Pick up in store");
  // A control that IS the private thing offers no choices at all.
  EXPECT_TRUE(observation.elements[1].options.empty());
}

TEST(SanitizerTest, PlainTextOnThePageIsPaintedOverInThePicture) {
  // The picture is the whole page; only controls used to be masked in it. An
  // address shown as ordinary text was redacted in the JSON and plain to see in
  // the image sent beside it.
  Observation observation;
  observation.private_regions = {gfx::Rect(40, 300, 220, 18)};
  const std::vector<Redaction> found = FindRedactions(observation);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].kind, Sensitivity::kPageText);
  EXPECT_EQ(found[0].bounds, gfx::Rect(40, 300, 220, 18));
}

TEST(SanitizerTest, ThePictureRuleLeavesTitlesAndPricesAlone) {
  // A black box over a video title costs the model the page.
  EXPECT_TRUE(ContainsPrivateText("Write to asha.rao@example.com"));
  EXPECT_TRUE(ContainsPrivateText("Call 9876543210"));
  EXPECT_TRUE(ContainsPrivateText("Card 4111 1111 1111 1111"));
  EXPECT_FALSE(ContainsPrivateText("I Spent 1000000 Dollars"));
  EXPECT_FALSE(ContainsPrivateText("Order 1234567 shipped on 30 September"));
  EXPECT_FALSE(ContainsPrivateText("Anvil Pro, 48 kg, $249.00"));
}

TEST(SanitizerTest, ANumberGluedToTheWordBesideItIsStillCaught) {
  // The accessibility text has no separator between blocks, so a value arrives
  // fused to the next label. Word-wise it is not a number and used to pass.
  Observation observation;
  observation.text = "Phone: 9876543210Email: shown belowSaved 9123456789";
  observation.full_text = observation.text;
  RedactObservation(observation);
  for (const std::string* text : {&observation.text, &observation.full_text}) {
    EXPECT_EQ(text->find("9876543210"), std::string::npos) << *text;
    EXPECT_EQ(text->find("9123456789"), std::string::npos) << *text;
    EXPECT_NE(text->find("Phone:"), std::string::npos) << *text;
  }
  EXPECT_TRUE(ContainsPrivateText("Phone: 9876543210Email:"));
  // A seven-digit order number beside letters is still not private.
  std::string order = "Order 1234567shipped";
  EXPECT_FALSE(RedactSpacedNumbers(order));
}

TEST(SanitizerTest, TheRealValueIsKeptForTheBrowserAndNeverSerialised) {
  // Typed text is checked against what the field holds. A masked value made
  // every phone, email and card field fail that check. The real value stays on
  // the node for the browser and must never reach the JSON the model reads.
  Observation observation;
  ObservedNode phone = Field("textbox", "Telephone", "9876543210");
  observation.elements.push_back(phone);
  RedactObservation(observation);
  EXPECT_EQ(observation.elements[0].value, kRedactedMarker);
  EXPECT_EQ(observation.elements[0].raw_value, "9876543210");
  for (int level : {0, 1, 2}) {
    const std::string json = observation.ToJson(level);
    EXPECT_EQ(json.find("9876543210"), std::string::npos)
        << "level " << level << ": " << json;
  }
}

}  // namespace
}  // namespace zephyrus::agent
