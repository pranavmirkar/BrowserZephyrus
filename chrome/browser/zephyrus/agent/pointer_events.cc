// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/pointer_events.h"

namespace zephyrus::agent {

PointerObserver::~PointerObserver() = default;

std::optional<gfx::Point> PointerObserver::PointerHome() {
  return std::nullopt;
}

}  // namespace zephyrus::agent
