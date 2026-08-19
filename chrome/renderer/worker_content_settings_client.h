// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_WORKER_CONTENT_SETTINGS_CLIENT_H_
#define CHROME_RENDERER_WORKER_CONTENT_SETTINGS_CLIENT_H_

#include "components/content_settings/common/content_settings_manager.mojom.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/cookies/site_for_cookies.h"
#include "third_party/blink/public/common/tokens/tokens.h"
#include <array>
#include <optional>

#include "third_party/blink/public/platform/web_content_settings_client.h"
#include "url/origin.h"

namespace content {
class RenderFrame;
}  // namespace content

struct RendererContentSettingRules;

// This client is created on the main renderer thread then passed onto the
// blink's worker thread. For workers created from other workers, Clone()
// is called on the "parent" worker's thread.
class WorkerContentSettingsClient : public blink::WebContentSettingsClient {
 public:
  explicit WorkerContentSettingsClient(content::RenderFrame* render_frame);

  WorkerContentSettingsClient& operator=(const WorkerContentSettingsClient&) =
      delete;

  ~WorkerContentSettingsClient() override;

  // WebContentSettingsClient overrides.
  std::unique_ptr<blink::WebContentSettingsClient> Clone() override;
  void AllowStorageAccess(StorageType storage_type,
                          base::OnceCallback<void(bool)> callback) override;
  bool AllowStorageAccessSync(StorageType storage_type) override;
  bool AllowRunningInsecureContent(bool allowed_per_settings,
                                   const blink::WebURL& url) override;
  bool ShouldAutoupgradeMixedContent() override;

 public:
  // Zephyrus: the fingerprint seed, captured from the creating frame.
  //
  // Without these a worker fell through to the base implementation and got
  // nothing, so OffscreenCanvas inside a Web Worker was never perturbed and a
  // script could evade the feature entirely by doing its canvas work off the
  // main thread.
  //
  // The seed MUST be the creating frame's, not a fresh one: it is per (origin,
  // session), and a worker disagreeing with its own document about the same
  // canvas is precisely the cross-read inconsistency that made four of the top
  // 200 sites serve bot challenges.
  std::optional<std::array<uint8_t, 32>> GetZephyrusFingerprintSeed() override;
  uint32_t GetZephyrusFingerprintSurfaceMask() override;

 private:
  explicit WorkerContentSettingsClient(
      const WorkerContentSettingsClient& other);
  void EnsureContentSettingsManager() const;

  // Loading document context for this worker.
  bool is_unique_origin_ = false;
  url::Origin document_origin_;
  net::SiteForCookies site_for_cookies_;
  url::Origin top_frame_origin_;
  bool allow_running_insecure_content_ = false;
  const blink::LocalFrameToken frame_token_;
  std::unique_ptr<RendererContentSettingRules> content_setting_rules_;

  std::optional<std::array<uint8_t, 32>> zephyrus_seed_;
  uint32_t zephyrus_surface_mask_ = 0;

  // Because instances of this class are created on the parent's thread (i.e,
  // on the renderer main thread or on the thread of the parent worker), it is
  // necessary to lazily bind the `content_settings_manager_` remote. The
  // pending remote is initialized on the parent thread and then the remote is
  // bound when needed on the worker's thread.
  mutable mojo::PendingRemote<content_settings::mojom::ContentSettingsManager>
      pending_content_settings_manager_;
  mutable mojo::Remote<content_settings::mojom::ContentSettingsManager>
      content_settings_manager_;
};

#endif  // CHROME_RENDERER_WORKER_CONTENT_SETTINGS_CLIENT_H_
