// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/tool_surface.h"

#include <utility>

namespace zephyrus::agent {

ToolSurface::~ToolSurface() = default;

void ToolSurface::ObserveForFind(const std::string& query,
                                 ObserveCallback callback) {
  // The default is an ordinary look.
  //
  // A surface that cannot search beyond its element budget still owes the
  // caller an Observation, and the ids in an ordinary one are at least valid.
  // Out of line rather than in the header because Chromium's style plugin
  // rejects a virtual method with a body there -- an inline virtual is a vtable
  // in every translation unit that includes it.
  Observe(std::move(callback));
}

}  // namespace zephyrus::agent
