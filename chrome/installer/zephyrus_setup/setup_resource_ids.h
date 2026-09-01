// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_INSTALLER_ZEPHYRUS_SETUP_SETUP_RESOURCE_IDS_H_
#define CHROME_INSTALLER_ZEPHYRUS_SETUP_SETUP_RESOURCE_IDS_H_

// Inter, embedded rather than requested by name.
//
// Inter is not a Windows system font. Asking DirectWrite for "Inter" works on
// a machine that happens to have it installed -- a designer's machine, for
// instance -- and silently falls back to something else everywhere else. An
// installer is the one program whose appearance cannot depend on what the
// target machine already has, which is the same reason this UI is native
// rather than a WebView.
//
// Inter is licensed under SIL OFL 1.1. Embedding is permitted; shipping the
// licence text alongside is REQUIRED. See fonts/README.md.
#define IDR_FONT_INTER_REGULAR 256
#define IDR_FONT_INTER_SEMIBOLD 257

// The OFL text itself, embedded beside the fonts it covers. A licence file
// left in the source tree does not travel with a single-executable download,
// and "accompany" is the obligation. Stage 2 also writes it into the install
// directory so it survives next to the installed browser.
#define IDR_LICENSE_INTER_OFL 258

// Chromium's mini_installer.exe, injected AFTER the build by
// tools/package_installer.py. It is a build output of the same build, so it
// cannot be named by the .rc at compile time without creating an ordering the
// build cannot satisfy.
#define IDR_PAYLOAD_MINI_INSTALLER 300

#endif  // CHROME_INSTALLER_ZEPHYRUS_SETUP_SETUP_RESOURCE_IDS_H_
