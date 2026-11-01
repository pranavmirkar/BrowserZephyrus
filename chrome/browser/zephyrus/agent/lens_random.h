// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_RANDOM_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_RANDOM_H_

#include <stdint.h>

namespace zephyrus::agent {

// Where the lens mascot's randomness comes from: gaze targets, blink timing,
// the odd double-take. Injected so a test (or the parity renderer) can seed it
// and get the same character twice; the prototype called Math.random()
// directly, which is why its screenshots could not be compared exactly.
class LensRandom {
 public:
  virtual ~LensRandom() = default;

  // A number in [0, 1).
  virtual double Next() = 0;

  double Range(double lo, double hi) { return lo + Next() * (hi - lo); }
  bool Chance(double p) { return Next() < p; }
};

// The same generator the prototype uses for its space scene (mulberry32), so a
// seed means the same sequence on both sides of a comparison.
class SeededLensRandom : public LensRandom {
 public:
  explicit SeededLensRandom(uint32_t seed) : state_(seed) {}
  double Next() override;

 private:
  uint32_t state_;
};

// What a running browser uses.
class SystemLensRandom : public LensRandom {
 public:
  double Next() override;
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_RANDOM_H_
