// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_PUBLIC_PLATFORM_WEB_CONTENT_SETTINGS_CLIENT_H_
#define THIRD_PARTY_BLINK_PUBLIC_PLATFORM_WEB_CONTENT_SETTINGS_CLIENT_H_

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "base/functional/callback.h"

namespace blink {

class WebURL;

// This class provides the content settings information which tells
// whether each feature is allowed or not.
class WebContentSettingsClient {
 public:
  // Only used if this is a WebContentSettingsClient on a worker thread. Clones
  // this WebContentSettingsClient so it can be used by another worker thread.
  virtual std::unique_ptr<WebContentSettingsClient> Clone() { return nullptr; }

  enum class StorageType {
    kCacheStorage,
    kIndexedDB,
    kFileSystem,
    kWebLocks,
    kLocalStorage,
    kSessionStorage
  };

  // Controls whether access to the given StorageType is allowed for this frame.
  // Runs asynchronously.
  virtual void AllowStorageAccess(StorageType storage_type,
                                  base::OnceCallback<void(bool)> callback) {
    std::move(callback).Run(true);
  }

  // Controls whether access to the given StorageType is allowed for this frame.
  // Blocks until done.
  virtual bool AllowStorageAccessSync(StorageType storage_type) { return true; }

  // Controls whether insecure scripts are allowed to execute for this frame.
  virtual bool AllowRunningInsecureContent(bool enabled_per_settings,
                                           const WebURL&) {
    return enabled_per_settings;
  }

  // Controls whether access to read the clipboard is allowed for this frame.
  virtual bool AllowReadFromClipboard() { return false; }

  // Controls whether access to write the clipboard is allowed for this frame.
  virtual bool AllowWriteToClipboard() { return false; }

  // Reports that passive mixed content was found at the provided URL.
  virtual void PassiveInsecureContentFound(const WebURL&) {}

  // Notifies the client that the frame would have executed script if script
  // were enabled.
  virtual void DidNotAllowScript() {}

  // Notifies the client that the frame would have loaded an image if image were
  // enabled.
  virtual void DidNotAllowImage() {}

  // Controls whether mixed content autoupgrades should be allowed in this
  // frame.
  virtual bool ShouldAutoupgradeMixedContent() { return true; }

  // Zephyrus §6.5: the fingerprint-randomization seed for this frame's
  // document, or nullopt when randomization is off for it.
  //
  // Lives on this interface because Blink needs to PULL it synchronously at the
  // moment a canvas is read — a page can read a canvas in its first inline
  // script, and a seed that arrived asynchronously would serve that read the
  // true values, leaking exactly the fingerprint the feature perturbs. This is
  // already the per-frame interface Blink consults synchronously for
  // allow/deny decisions, so no new plumbing or lifetime has to be invented.
  //
  // The 32 bytes are derived per (origin, session) in the browser process; the
  // session secret they come from never crosses into a renderer.
  virtual std::optional<std::array<uint8_t, 32>> GetZephyrusFingerprintSeed() {
    return std::nullopt;
  }

  // Zephyrus §6.5 per-surface mask; 0 means perturb nothing. Paired with the
  // seed above — a surface must check BOTH, because a seed alone does not say
  // which surfaces the embedder currently allows.
  virtual uint32_t GetZephyrusFingerprintSurfaceMask() { return 0; }

  virtual ~WebContentSettingsClient() = default;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_PUBLIC_PLATFORM_WEB_CONTENT_SETTINGS_CLIENT_H_
