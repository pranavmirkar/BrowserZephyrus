// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_SANITIZER_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_SANITIZER_H_

#include <string>
#include <string_view>
#include <vector>

#include "chrome/browser/zephyrus/agent/observation.h"
#include "ui/gfx/geometry/rect.h"

namespace zephyrus::agent {

// What kind of private thing an element holds, if any.
enum class Sensitivity {
  kNone,
  // A protected field. The page itself says so, which makes this the one
  // classification that needs no guessing.
  kPassword,
  kEmail,
  kPhone,
  kPaymentCard,
  // A person's name, which is the hardest of these and the least certain.
  kPersonalName,
};

// One thing to hide, and where it is on screen.
struct Redaction {
  std::string element_id;
  Sensitivity kind = Sensitivity::kNone;
  // Where to paint over. Empty when the element was never on screen, in which
  // case only its text is replaced.
  gfx::Rect bounds;
};

// What an element holds, judged from what the page says about it.
//
// The accessibility tree is a BETTER detector than a vision model for this,
// and it is worth being clear why: a field's role, its label and its own
// protected flag are stated facts, not inferences from pixels. A vision model
// looking at the same form has to read it back off the screen and guess. The
// picture matters for what the tree cannot describe -- a face in a photograph,
// text baked into an image -- which is a different job.
Sensitivity ClassifyElement(const ObservedNode& node);

// True if `text` looks like a thing that should never leave the machine.
//
// Deliberately shaped to over-match rather than under-match. A redaction that
// hides one harmless string costs the model a little context; a miss puts a
// real address or card number on the wire, and that cannot be taken back.
Sensitivity ClassifyText(std::string_view text);

// Everything in this Observation that must not be sent, with its position.
std::vector<Redaction> FindRedactions(const Observation& observation);

// Replaces private values and page text in place, leaving the STRUCTURE the
// model needs to reason.
//
// This is the whole trick of the thing. The server is told there is an email
// field, that it is filled in, and where it sits -- it is simply not told what
// the address is. "Click Submit" needs none of that, and neither does deciding
// whether a form is complete.
void RedactObservation(Observation& observation);

// The mask a redacted value is replaced with.
inline constexpr char kRedactedMarker[] = "[redacted]";

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_SANITIZER_H_
