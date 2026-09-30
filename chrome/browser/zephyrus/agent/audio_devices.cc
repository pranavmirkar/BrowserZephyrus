// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/audio_devices.h"

#include <windows.h>

#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <wrl/client.h>

#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <utility>

#include "base/strings/utf_string_conversions.h"

namespace zephyrus::agent {

namespace {

// The friendly names of the active capture endpoints, as Windows' own sound
// settings show them. The older API this code records with reports names cut to
// 31 characters ("Microphone Array (Realtek(R) Au"), which is the difference
// between recognising a device and not. Empty when COM is not available.
std::vector<std::string> EndpointNames() {
  std::vector<std::string> names;
  Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
  if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
    return names;
  }
  Microsoft::WRL::ComPtr<IMMDeviceCollection> collection;
  if (FAILED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE,
                                            &collection))) {
    return names;
  }
  UINT count = 0;
  if (FAILED(collection->GetCount(&count))) {
    return names;
  }
  for (UINT i = 0; i < count; ++i) {
    Microsoft::WRL::ComPtr<IMMDevice> device;
    if (FAILED(collection->Item(i, &device))) {
      continue;
    }
    Microsoft::WRL::ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &store))) {
      continue;
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) &&
        value.vt == VT_LPWSTR && value.pwszVal) {
      names.push_back(base::WideToUTF8(value.pwszVal));
    }
    PropVariantClear(&value);
  }
  return names;
}

}  // namespace

uint32_t DefaultAudioInputId() {
  return static_cast<uint32_t>(WAVE_MAPPER);
}

std::vector<AudioInputDevice> ListAudioInputDevices() {
  std::vector<AudioInputDevice> devices;
  AudioInputDevice fallback;
  fallback.name = "Windows default microphone";
  fallback.wave_id = DefaultAudioInputId();
  fallback.is_default = true;
  devices.push_back(std::move(fallback));

  const std::vector<std::string> friendly = EndpointNames();
  const UINT count = waveInGetNumDevs();
  for (UINT i = 0; i < count; ++i) {
    WAVEINCAPSW caps = {};
    if (waveInGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) {
      continue;
    }
    const std::string short_name = base::WideToUTF8(caps.szPname);
    std::string name = short_name;
    for (const std::string& full : friendly) {
      // The short name is the front of the friendly one.
      if (!short_name.empty() && full.compare(0, short_name.size(), short_name) == 0) {
        name = full;
        break;
      }
    }
    // Two identical microphones must still be told apart.
    int same = 0;
    for (const AudioInputDevice& d : devices) {
      if (d.name == name || d.name.rfind(name + " (", 0) == 0) {
        ++same;
      }
    }
    if (same > 0) {
      name += " (" + std::to_string(same + 1) + ")";
    }
    AudioInputDevice device;
    device.name = std::move(name);
    device.wave_id = i;
    devices.push_back(std::move(device));
  }
  return devices;
}

uint32_t ResolveAudioInput(const std::string& saved_name) {
  if (saved_name.empty()) {
    return DefaultAudioInputId();
  }
  for (const AudioInputDevice& device : ListAudioInputDevices()) {
    if (device.name == saved_name) {
      return device.wave_id;
    }
  }
  return DefaultAudioInputId();
}

}  // namespace zephyrus::agent
