// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_AUDIO_DEVICES_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_AUDIO_DEVICES_H_

#include <cstdint>
#include <string>
#include <vector>

namespace zephyrus::agent {

// A microphone the person can choose.
struct AudioInputDevice {
  // What the person sees: the name Windows shows in its own sound settings.
  std::string name;
  // The device number the capture code opens. For the entry that means "whatever
  // Windows currently treats as the default", the mapper's number.
  uint32_t wave_id = 0;
  bool is_default = false;
};

// The microphones present now, "Windows default" first. Cheap enough to call
// when a screen opens; not something to poll.
//
// Why this exists: a computer often has several -- the laptop's own array, a
// headset, a virtual "noise-cancelling" input -- and Windows' default is not
// always the good one. A Bluetooth headset's microphone in particular is
// narrow-band and heavily compressed, and a voice recorded through it is not the
// voice heard through the laptop's own array, which is where "Hey Zep" then fails
// to match. The person has to be able to see which one is listening, and choose.
std::vector<AudioInputDevice> ListAudioInputDevices();

// The device number for a name saved earlier. Empty, or a device that is no
// longer plugged in, means the Windows default: a saved choice that has gone
// away must never leave the person with no microphone.
uint32_t ResolveAudioInput(const std::string& saved_name);

// The number that means "the Windows default".
uint32_t DefaultAudioInputId();

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_AUDIO_DEVICES_H_
