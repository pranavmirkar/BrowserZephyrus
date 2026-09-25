// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/buildflags/dev_switches.h"

#include "build/buildflag.h"
#include "chrome/browser/zephyrus/buildflags/buildflags.h"

namespace zephyrus {

bool DevSwitchesEnabled() {
  return BUILDFLAG(ZEPHYRUS_DEV_SWITCHES);
}

}  // namespace zephyrus
