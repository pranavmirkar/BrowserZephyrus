// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_BUILDFLAGS_DEV_SWITCHES_H_
#define CHROME_BROWSER_ZEPHYRUS_BUILDFLAGS_DEV_SWITCHES_H_

namespace zephyrus {

// Whether this build honours the developer-only command-line switches (GN arg
// `zephyrus_dev_switches`; audit finding Z-10). When false, those switches are
// ignored however the browser is launched.
//
// A function rather than the raw BUILDFLAG at each call site: the constant
// folded into an `if` made the code after it unreachable, which
// -Wunreachable-code rejects in whichever configuration the flag leaves dead.
bool DevSwitchesEnabled();

}  // namespace zephyrus

#endif  // CHROME_BROWSER_ZEPHYRUS_BUILDFLAGS_DEV_SWITCHES_H_
