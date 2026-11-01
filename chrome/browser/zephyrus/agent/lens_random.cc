// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/lens_random.h"

#include "base/rand_util.h"

namespace zephyrus::agent {

double SeededLensRandom::Next() {
  // mulberry32, as in the prototype's space scene.
  state_ += 0x6D2B79F5u;
  uint32_t t = state_;
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + (t ^ (t >> 7)) * (t | 61u);
  return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

double SystemLensRandom::Next() {
  return base::RandDouble();
}

}  // namespace zephyrus::agent
