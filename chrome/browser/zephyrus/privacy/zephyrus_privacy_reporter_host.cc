// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/zephyrus_privacy_reporter_host.h"

#include <utility>

#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"
#include "content/public/browser/render_frame_host.h"
#include "url/gurl.h"

namespace zephyrus_privacy {
namespace {

// Translates the untrusted wire value into the browser's own enum.
//
// A switch with no `default:` on purpose: adding a value to the mojom without
// handling it here is then a compile error rather than a silent fall-through
// to the wrong surface. The bindings have already rejected anything outside the
// enum before this runs.
std::optional<FingerprintSurface> FromMojom(mojom::FingerprintSurface surface) {
  switch (surface) {
    case mojom::FingerprintSurface::kMediaDeviceEnumeration:
      return FingerprintSurface::kMediaDeviceEnumeration;
    case mojom::FingerprintSurface::kCanvasRead:
      return FingerprintSurface::kCanvasRead;
    case mojom::FingerprintSurface::kCanvasExport:
      return FingerprintSurface::kCanvasExport;
    case mojom::FingerprintSurface::kWebglRenderer:
      return FingerprintSurface::kWebglRenderer;
    case mojom::FingerprintSurface::kAudioBuffer:
      return FingerprintSurface::kAudioBuffer;
    case mojom::FingerprintSurface::kHardwareConcurrency:
      return FingerprintSurface::kHardwareConcurrency;
    case mojom::FingerprintSurface::kDeviceMemory:
      return FingerprintSurface::kDeviceMemory;
    case mojom::FingerprintSurface::kScreenDepth:
      return FingerprintSurface::kScreenDepth;
  }
  return std::nullopt;
}

}  // namespace

// static
void ZephyrusPrivacyReporterHost::Create(
    content::RenderFrameHost* render_frame_host,
    mojo::PendingReceiver<mojom::PrivacyReporter> receiver) {
  // DocumentService owns itself; it is destroyed with the document or on
  // disconnect.
  new ZephyrusPrivacyReporterHost(*render_frame_host, std::move(receiver));
}

ZephyrusPrivacyReporterHost::ZephyrusPrivacyReporterHost(
    content::RenderFrameHost& render_frame_host,
    mojo::PendingReceiver<mojom::PrivacyReporter> receiver)
    : content::DocumentService<mojom::PrivacyReporter>(render_frame_host,
                                                       std::move(receiver)) {}

ZephyrusPrivacyReporterHost::~ZephyrusPrivacyReporterHost() = default;

void ZephyrusPrivacyReporterHost::ReportFingerprintSurface(
    mojom::FingerprintSurface surface) {
  const std::optional<FingerprintSurface> translated = FromMojom(surface);
  if (!translated.has_value()) {
    return;
  }

  // Null for an off-the-record profile, and when the feature is off. Both are
  // supported states, not errors — see the factory.
  PrivacyIntelligenceService* service =
      PrivacyIntelligenceServiceFactory::GetForBrowserContext(
          render_frame_host().GetBrowserContext());
  if (!service) {
    return;
  }

  // §9.2: attribute to the top-level site, not to the frame that made the call.
  // A tracker in an iframe probing the device list is something that happened
  // to the user ON the page they are looking at, and the popup describes that
  // page. Attributing it to the iframe's own site would hide it from the only
  // place the user would look.
  //
  // Read from the RenderFrameHost, never from the message: this is the line
  // that stops a compromised renderer forging an entry against another site.
  const GURL page_url =
      render_frame_host().GetOutermostMainFrame()->GetLastCommittedURL();
  service->RecordFingerprintSurface(page_url, *translated);
}

}  // namespace zephyrus_privacy
