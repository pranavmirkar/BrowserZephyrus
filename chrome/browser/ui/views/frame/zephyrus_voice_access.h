// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_VOICE_ACCESS_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_VOICE_ACCESS_H_

class Profile;

namespace zephyrus::agent {

class VoiceLibrary;

// The voices recorded for `profile`, made on first use and owned by the profile.
// One per profile, however many windows: the hands-free listener of every window,
// the settings screen and the enrolment dialog all see the same voices, and a
// change made in one reaches the others.
//
// A private (off-the-record) profile gets a library that never has a file, so
// nothing recorded there outlives it and nothing recorded in a regular profile
// is visible in it.
VoiceLibrary* GetVoiceLibrary(Profile* profile);

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_VOICE_ACCESS_H_
