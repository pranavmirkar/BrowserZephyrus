// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_PRIVACY_REPORTER_HOST_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_PRIVACY_REPORTER_HOST_H_

#include "content/public/browser/document_service.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "third_party/blink/public/mojom/zephyrus/zephyrus_privacy_reporter.mojom.h"

namespace content {
class RenderFrameHost;
}

namespace zephyrus_privacy {

// Browser-side implementation of the renderer -> browser privacy event channel
// (§9.2.1), scoped to a document via content::DocumentService.
//
// **This is a trust boundary.** Everything arriving here was chosen by a
// renderer that may be compromised. The defences are structural rather than
// validating:
//
//  - The interface carries no URL, origin or frame id, so there is nothing to
//    validate: the site is read from render_frame_host(), which the renderer
//    cannot influence. A compromised renderer therefore cannot attribute its
//    behaviour to another site.
//  - DocumentService ties the receiver's lifetime to the document, so a
//    navigation cannot be used to keep reporting against the old page.
//  - The service records each surface at most once per site, so message volume
//    is bounded by the size of a closed enum rather than by the renderer.
//  - Nothing here can affect a block decision or any policy (§3.1). The worst a
//    hostile renderer achieves is claiming its own page touched a
//    fingerprinting surface it did not, which costs the user one row.
class ZephyrusPrivacyReporterHost
    : public content::DocumentService<mojom::PrivacyReporter> {
 public:
  static void Create(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingReceiver<mojom::PrivacyReporter> receiver);

  // mojom::PrivacyReporter:
  void ReportFingerprintSurface(mojom::FingerprintSurface surface) override;

 private:
  ZephyrusPrivacyReporterHost(
      content::RenderFrameHost& render_frame_host,
      mojo::PendingReceiver<mojom::PrivacyReporter> receiver);
  ~ZephyrusPrivacyReporterHost() override;
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_PRIVACY_REPORTER_HOST_H_
