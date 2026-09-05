// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/sanitizer.h"

#include <utility>

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

}  // namespace

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

  // Failing that, what it currently holds.
  return ClassifyText(node.value);
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
  return found;
}

void RedactObservation(Observation& observation) {
  for (ObservedNode& node : observation.elements) {
    if (ClassifyElement(node) == Sensitivity::kNone) {
      continue;
    }
    // The VALUE goes; the role, the label and the position stay.
    //
    // That division is the point of the whole design. "There is an email field,
    // it is filled in, it is here" is everything a model needs to decide what to
    // do next, and it is not the user's email address.
    if (!node.value.empty()) {
      node.value = kRedactedMarker;
    }
  }

  // Page text is a blunter problem: it is one long run with no structure to
  // reason about, so anything that looks private inside it is replaced
  // wholesale rather than located.
  if (!observation.text.empty()) {
    std::vector<std::string> words = base::SplitString(
        observation.text, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    bool changed = false;
    for (std::string& word : words) {
      if (ClassifyText(word) != Sensitivity::kNone) {
        word = kRedactedMarker;
        changed = true;
      }
    }
    if (changed) {
      observation.text = base::JoinString(words, " ");
    }
  }
}

}  // namespace zephyrus::agent
