// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_VOICE_SETUP_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_VOICE_SETUP_H_

class BrowserView;

namespace zephyrus {

// "Hey Zep" and Voice Lock, in one dialog: turn hands-free on, choose how picky
// the lock is, record, rename, delete and test voices.
//
// Recording a voice is five takes of the phrase "Hey Zep". What is kept is what
// the phrase sounds like -- never the audio -- encrypted with the system keystore
// and stored on this computer only. See voice_lock.h for what this can and
// cannot promise: it is a lock for the hands-free path, not a security boundary.
//
// Browser-modal, so nothing on the page changes while the microphone is open for
// recording. The wake listener steps aside for as long as the dialog is up.
void ShowVoiceSetup(BrowserView* browser_view);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_VOICE_SETUP_H_
