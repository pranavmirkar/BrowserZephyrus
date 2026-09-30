// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_VOICE_SETUP_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_VOICE_SETUP_H_

class BrowserView;

namespace views {
class View;
}  // namespace views

namespace zephyrus {

// The "Hey Zep" popup, hanging off the microphone-and-gear button in the agent
// panel's header. One step at a time:
//
//   nothing recorded yet -> "Record my voice" -> three takes of "Hey Zep" ->
//   ready (hands-free is switched on for you).
//
// Once a voice exists the popup is a small control panel: the hands-free switch,
// who Zep answers to, a test, and (folded away) Voice Lock and its strictness.
//
// What is kept is what the phrase SOUNDS like, never the audio: encrypted with
// the system keystore and stored on this computer only. See voice_lock.h for what
// Voice Lock can and cannot promise: a convenience lock, not a security boundary.
//
// The wake listener steps aside for as long as the popup is recording.
void ShowVoiceSetup(BrowserView* browser_view, views::View* anchor);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_VOICE_SETUP_H_
