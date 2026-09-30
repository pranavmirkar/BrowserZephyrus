// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/sanitizer.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/notreached.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace zephyrus::agent {
namespace {

// Labels that name a field holding a particular kind of private thing.
//
// Matched against the element's accessible NAME, which is what the page calls
// the field -- "Email address", "Card number", "Mobile". This is the signal a
// person uses to know what a box is for, and it is stated rather than inferred.
struct LabelHint {
  const char* fragment;
  Sensitivity kind;
};

constexpr LabelHint kLabelHints[] = {
    {"email", Sensitivity::kEmail},
    {"e-mail", Sensitivity::kEmail},
    {"phone", Sensitivity::kPhone},
    {"mobile", Sensitivity::kPhone},
    {"telephone", Sensitivity::kPhone},
    {"card number", Sensitivity::kPaymentCard},
    {"credit card", Sensitivity::kPaymentCard},
    {"debit card", Sensitivity::kPaymentCard},
    {"cardholder", Sensitivity::kPaymentCard},
    {"cvv", Sensitivity::kPaymentCard},
    {"security code", Sensitivity::kPaymentCard},
    {"full name", Sensitivity::kPersonalName},
    {"first name", Sensitivity::kPersonalName},
    {"last name", Sensitivity::kPersonalName},
    {"surname", Sensitivity::kPersonalName},
    {"given name", Sensitivity::kPersonalName},
};

bool LooksLikeEmail(std::string_view text) {
  const size_t at = text.find('@');
  if (at == std::string_view::npos || at == 0 || at + 1 >= text.size()) {
    return false;
  }
  // A dot after the @, with something on each side of it. Enough to catch a
  // real address without pretending to implement the grammar.
  const size_t dot = text.find('.', at + 2);
  return dot != std::string_view::npos && dot + 1 < text.size();
}

// Digits, ignoring the punctuation people write numbers with.
size_t CountDigits(std::string_view text) {
  size_t digits = 0;
  for (const char c : text) {
    if (base::IsAsciiDigit(c)) {
      ++digits;
    }
  }
  return digits;
}

bool IsMostlyNumeric(std::string_view text) {
  size_t meaningful = 0;
  for (const char c : text) {
    if (!base::IsAsciiWhitespace(c) && c != '-' && c != '(' && c != ')' &&
        c != '+' && c != '.') {
      ++meaningful;
    }
  }
  return meaningful > 0 && CountDigits(text) == meaningful;
}

// The strongest classification found in any single word of `text`.
//
// Free text has no structure to reason about, so it is judged a word at a time.
// A run like "call me on 9876543210" is not a phone number as a whole string
// and plainly contains one.
Sensitivity ClassifyEachWord(std::string_view text) {
  for (const std::string& word : base::SplitString(
           text, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    const Sensitivity kind = ClassifyText(word);
    if (kind != Sensitivity::kNone) {
      return kind;
    }
  }
  return Sensitivity::kNone;
}

// The same, but only for the unmistakable kinds.
//
// Used on an element's NAME, where the numeric rules are too eager to trust. A
// name is a title as often as it is data -- "I Spent 1000000 Dollars" is seven
// digits and a video, not a phone number -- and a false positive here does not
// merely lose a word: it paints a black rectangle over that element in the
// screenshot and takes the title the task was about with it.
//
// An address in a name has no such reading. Nothing is called someone@example
// by accident, so that one is kept and the digit rules are not. The digit rules
// still apply in full to a field's VALUE, where ten digits really is a phone
// number, and to the page's running text.
Sensitivity ClassifyNameAsContent(std::string_view name) {
  const Sensitivity kind = ClassifyEachWord(name);
  return kind == Sensitivity::kEmail ? kind : Sensitivity::kNone;
}

// Replaces the words in a name that ClassifyNameAsContent flags, and no others.
//
// Deliberately the same test as the classification above, so that what is
// hidden in the text is exactly what is painted over in the picture. Two rules
// drifting apart here is how you get a name the JSON masked and the screenshot
// did not.
void RedactNameWords(std::string& name) {
  std::vector<std::string> words = base::SplitString(
      name, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  bool changed = false;
  for (std::string& word : words) {
    if (ClassifyNameAsContent(word) != Sensitivity::kNone) {
      word = kRedactedMarker;
      changed = true;
    }
  }
  if (changed) {
    name = base::JoinString(words, " ");
  }
}

// The bare word a policy can match on.
std::string_view DescribeSensitivity(Sensitivity kind) {
  switch (kind) {
    case Sensitivity::kNone:
      return "";
    case Sensitivity::kPassword:
      return "password";
    case Sensitivity::kEmail:
      return "email";
    case Sensitivity::kPhone:
      return "phone";
    case Sensitivity::kPaymentCard:
      return "payment_card";
    case Sensitivity::kPersonalName:
      return "personal_name";
    case Sensitivity::kPageText:
      return "page_text";
  }
  NOTREACHED();
}

bool IsNumberChar(char c) {
  return base::IsAsciiDigit(c) || c == ' ' || c == '-' || c == '(' ||
         c == ')' || c == '+' || c == '.';
}

// A number written with separators counts as private from this many digits: a
// card (13 to 19) always is, a phone number with a country code (12) is, and a
// date or a timestamp (8 to 10) is not.
constexpr size_t kSpacedNumberDigits = 12;

// And ten digits in one unbroken row are private wherever they sit, including
// glued to the letters beside them. MEASURED on a real page: the accessibility
// text joins its blocks with no separator, so "Phone: 9876543210" followed by
// "Email: ..." arrived as "9876543210Email:", one word that is not a number, and
// the phone number went to the model in the clear.
constexpr size_t kGluedNumberDigits = 10;

// The [begin, end) ranges of such runs in `text`. A run ends at its last digit,
// so a full stop after it stays with the sentence.
std::vector<std::pair<size_t, size_t>> SpacedNumberRuns(std::string_view text) {
  std::vector<std::pair<size_t, size_t>> runs;
  size_t i = 0;
  while (i < text.size()) {
    if (!base::IsAsciiDigit(text[i])) {
      ++i;
      continue;
    }
    size_t j = i;
    size_t digits = 0;
    size_t in_a_row = 0;
    size_t longest_row = 0;
    size_t last_digit_end = i;
    while (j < text.size() && IsNumberChar(text[j])) {
      if (base::IsAsciiDigit(text[j])) {
        ++digits;
        ++in_a_row;
        longest_row = std::max(longest_row, in_a_row);
        last_digit_end = j + 1;
      } else {
        in_a_row = 0;
      }
      ++j;
    }
    if (digits >= kSpacedNumberDigits || longest_row >= kGluedNumberDigits) {
      runs.emplace_back(i, last_digit_end);
    }
    i = std::max(j, i + 1);
  }
  return runs;
}

// Replaces the private words in `text`, leaving the rest. True if it changed.
//
// Word-wise rather than wholesale, because these strings are also how the model
// refers to things. Blanking an entire button name to hide the address inside it
// would remove the address and the button.
bool RedactWords(std::string& text) {
  if (text.empty()) {
    return false;
  }
  // First the numbers written in groups, which no single word gives away.
  bool changed = RedactSpacedNumbers(text);
  std::vector<std::string> words = base::SplitString(
      text, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  for (std::string& word : words) {
    if (ClassifyText(word) != Sensitivity::kNone) {
      word = kRedactedMarker;
      changed = true;
    }
  }
  if (changed) {
    text = base::JoinString(words, " ");
  }
  return changed;
}

}  // namespace

bool RedactSpacedNumbers(std::string& text) {
  const std::vector<std::pair<size_t, size_t>> runs = SpacedNumberRuns(text);
  // From the end, so the earlier offsets stay true.
  for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
    text.replace(it->first, it->second - it->first, kRedactedMarker);
  }
  return !runs.empty();
}

bool ContainsPrivateText(std::string_view text) {
  if (!SpacedNumberRuns(text).empty()) {
    return true;
  }
  for (const std::string& word : base::SplitString(
           text, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    const Sensitivity kind = ClassifyText(word);
    if (kind == Sensitivity::kEmail || kind == Sensitivity::kPaymentCard) {
      return true;
    }
    if (kind == Sensitivity::kPhone && CountDigits(word) >= 10) {
      return true;
    }
  }
  return false;
}

Sensitivity ClassifyText(std::string_view text) {
  const std::string lowered = base::ToLowerASCII(text);
  if (LooksLikeEmail(lowered)) {
    return Sensitivity::kEmail;
  }
  if (IsMostlyNumeric(text)) {
    const size_t digits = CountDigits(text);
    // A card is 13-19 digits, a phone number roughly 7-15. They overlap, and
    // the overlap is fine: both are redacted, so only the LABEL differs.
    if (digits >= 13 && digits <= 19) {
      return Sensitivity::kPaymentCard;
    }
    if (digits >= 7 && digits <= 15) {
      return Sensitivity::kPhone;
    }
  }
  return Sensitivity::kNone;
}

Sensitivity ClassifyElement(const ObservedNode& node) {
  // The page said so itself. Nothing else here is this certain.
  if (node.role == "password") {
    return Sensitivity::kPassword;
  }

  // What the field is called. A box labelled "Email" holds an email whether or
  // not anything has been typed into it yet, and knowing that BEFORE it is
  // filled is what lets the mask be in place the moment it is.
  const std::string label = base::ToLowerASCII(node.name);
  for (const LabelHint& hint : kLabelHints) {
    if (label.find(hint.fragment) != std::string::npos) {
      return hint.kind;
    }
  }

  // Failing that, what it currently holds -- judged WHOLE first.
  //
  // People write a card number as "4111 1111 1111 1111", and the whole of that
  // is sixteen digits while every word of it is four. Going word-wise first
  // silently stopped detecting the commonest way a card number is typed, which
  // the existing test caught and which is worth restating: the spaces are
  // punctuation inside one number, not boundaries between several.
  const Sensitivity whole = ClassifyText(node.value);
  if (whole != Sensitivity::kNone) {
    return whole;
  }

  // Then word-wise, for a value that is a sentence with something in it.
  const Sensitivity from_word = ClassifyEachWord(node.value);
  if (from_word != Sensitivity::kNone) {
    return from_word;
  }

  // Last, the NAME read as content rather than as a label -- and only for the
  // kinds that cannot be mistaken for a title. See ClassifyNameAsContent.
  //
  // The gap this closes: an element whose accessible name simply is the private
  // thing. An account button reading "Signed in as someone@example.com", a
  // header showing a phone number. None of those are labelled anything, so the
  // hints above miss them, and the value is empty because there is no field --
  // the address is the name. Both channels leaked it: the text because nothing
  // replaced it, the picture because nothing knew to paint over it.
  return ClassifyNameAsContent(node.name);
}

std::vector<Redaction> FindRedactions(const Observation& observation) {
  std::vector<Redaction> found;
  for (const ObservedNode& node : observation.elements) {
    const Sensitivity kind = ClassifyElement(node);
    if (kind == Sensitivity::kNone) {
      continue;
    }
    Redaction redaction;
    redaction.element_id = node.id;
    redaction.kind = kind;
    // An offscreen element's bounds name the edge it went behind rather than
    // the element, so painting over them would black out the wrong part of the
    // picture. Its text is still replaced; only the mask is skipped.
    redaction.bounds = node.offscreen ? gfx::Rect() : node.bounds;
    found.push_back(std::move(redaction));
  }
  // Plain text that is private: no element, so found in the tree and kept as a
  // region to paint over.
  for (const gfx::Rect& region : observation.private_regions) {
    Redaction redaction;
    redaction.kind = Sensitivity::kPageText;
    redaction.bounds = region;
    found.push_back(std::move(redaction));
  }
  return found;
}

void RedactObservation(Observation& observation) {
  for (ObservedNode& node : observation.elements) {
    const Sensitivity kind = ClassifyElement(node);
    // Recorded before anything is replaced, because after that the evidence is
    // gone: a card number field known only by its digits classifies as harmless
    // the moment those digits become [redacted].
    node.sensitivity = std::string(DescribeSensitivity(kind));
    if (kind == Sensitivity::kNone) {
      continue;
    }
    // The VALUE goes; the role, the label and the position stay.
    //
    // That division is the point of the whole design. "There is an email field,
    // it is filled in, it is here" is everything a model needs to decide what to
    // do next, and it is not the user's email address.
    if (!node.value.empty()) {
      node.raw_value = node.value;
      node.value = kRedactedMarker;
    }
    // A sensitive control's choices go with it: a list of saved cards or
    // addresses is the private thing itself.
    node.options.clear();

    // And the name, where the private thing is the name itself. Word-wise, so
    // "Signed in as [redacted]" survives as something the model can still find
    // and click -- the point is to remove the address, not the button.
    RedactNameWords(node.name);
  }

  // The line around each element, judged like page text rather than like a
  // title.
  //
  // This is running prose lifted from beside the element -- a channel added to
  // answer "which of these is the newest", and one that promptly carried an
  // address and a phone number to the model with the value beside it already
  // masked. Exactly the leak this whole sanitizer exists to prevent, in a field
  // that did not exist when it was written.
  //
  // Outside the sensitive-element loop above on purpose: the private thing in
  // the text near an element has nothing to do with whether that ELEMENT is
  // sensitive. A harmless button beside someone's address still shows it.
  for (ObservedNode& node : observation.elements) {
    RedactWords(node.detail);
    // The labels a drop-down offers can carry the same things ("Home, 12 MG
    // Road", "Visa ending 4242"), and they reach the model in its own field.
    for (std::string& option : node.options) {
      RedactWords(option);
    }
  }

  // The page's running text: one long run with no structure, so anything that
  // looks private inside it is replaced in place. The digit rules apply in
  // full here -- a bare ten-digit word in prose is a phone number far more
  // often than it is anything else.
  RedactWords(observation.text);
  // The rest of the page, for page.read. It was copied from `text` BEFORE this
  // point and never touched, so reading a page sent its addresses, phone numbers
  // and emails to the model in the clear while the short view of the same page
  // was masked.
  RedactWords(observation.full_text);

  // The title gets the narrower rule, for the reason names do: a title is a
  // title. "I Spent 1000000 Dollars" is seven digits and a video, and blanking
  // that number would leave the model unable to recognise the page it was sent
  // to find.
  RedactNameWords(observation.title);
}

}  // namespace zephyrus::agent
