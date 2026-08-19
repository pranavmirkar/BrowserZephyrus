// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_proxying_url_loader_factory.h"

#include "base/functional/bind.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service_factory.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_tab_helper.h"
#include <algorithm>
#include <limits>

#include "base/hash/hash.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "chrome/browser/zephyrus/privacy/privacy_host_canon.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_cname_harvester.h"
#include "chrome/browser/zephyrus/privacy/privacy_tab_helper.h"
#include "content/public/browser/render_frame_host.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "url/gurl.h"

namespace zephyrus_adblock {

namespace {

ResourceType MapDestination(network::mojom::RequestDestination destination) {
  using RD = network::mojom::RequestDestination;
  switch (destination) {
    case RD::kScript:
    case RD::kServiceWorker:
    case RD::kSharedWorker:
    case RD::kWorker:
      return kTypeScript;
    case RD::kImage:
      return kTypeImage;
    case RD::kStyle:
    case RD::kXslt:
      return kTypeStylesheet;
    case RD::kFont:
      return kTypeFont;
    case RD::kObject:
    case RD::kEmbed:
      return kTypeObject;
    case RD::kFrame:
    case RD::kIframe:
    case RD::kFencedframe:
      return kTypeSubdocument;
    case RD::kAudio:
    case RD::kVideo:
    case RD::kTrack:
      return kTypeMedia;
    case RD::kEmpty:
      return kTypeXhr;
    default:
      return kTypeOther;
  }
}

}  // namespace

ZephyrusAdblockProxyingURLLoaderFactory::ZephyrusAdblockProxyingURLLoaderFactory(
    mojo::PendingReceiver<network::mojom::URLLoaderFactory> loader_receiver,
    mojo::PendingRemote<network::mojom::URLLoaderFactory> target_factory_remote,
    base::WeakPtr<ZephyrusAdblockService> service,
    content::GlobalRenderFrameHostId frame_id,
    scoped_refptr<zephyrus_privacy::PrivacyEventSink> privacy_sink,
    scoped_refptr<zephyrus_privacy::DomainStringTable> privacy_strings,
    scoped_refptr<zephyrus_privacy::PrivacyCnameCache> privacy_cname_cache,
    base::SelfDeletingPassKey pass_key)
    : network::SelfDeletingURLLoaderFactory(std::move(loader_receiver),
                                            pass_key),
      service_(std::move(service)),
      frame_id_(frame_id),
      privacy_sink_(std::move(privacy_sink)),
      privacy_strings_(std::move(privacy_strings)) {
  target_factory_.Bind(std::move(target_factory_remote));
  target_factory_.set_disconnect_handler(base::BindOnce(
      &ZephyrusAdblockProxyingURLLoaderFactory::OnTargetFactoryError,
      base::Unretained(this)));
}

ZephyrusAdblockProxyingURLLoaderFactory::
    ~ZephyrusAdblockProxyingURLLoaderFactory() = default;

// static
void ZephyrusAdblockProxyingURLLoaderFactory::MaybeProxyRequest(
    content::BrowserContext* browser_context,
    content::RenderFrameHost* frame,
    network::URLLoaderFactoryBuilder& factory_builder) {
  ZephyrusAdblockService* service =
      ZephyrusAdblockServiceFactory::GetForBrowserContext(browser_context);
  if (!service || !service->enabled()) {
    return;
  }

  const content::GlobalRenderFrameHostId frame_id =
      frame ? frame->GetGlobalId() : content::GlobalRenderFrameHostId();

  // Null unless Privacy Intelligence is collecting for this profile. The
  // factory returns null for every off-the-record profile, so incognito events
  // never reach the pipeline at all (spec 5.2).
  scoped_refptr<zephyrus_privacy::PrivacyEventSink> privacy_sink;
  scoped_refptr<zephyrus_privacy::DomainStringTable> privacy_strings;
  scoped_refptr<zephyrus_privacy::PrivacyCnameCache> privacy_cname_cache;
  if (zephyrus_privacy::IsCollectionEnabled()) {
    if (auto* privacy = zephyrus_privacy::PrivacyIntelligenceServiceFactory::
            GetForBrowserContext(browser_context)) {
      privacy_sink = privacy->sink();
      privacy_strings = privacy->domain_strings();
      privacy_cname_cache = privacy->cname_cache();
    }
  }

  auto [receiver, remote] = factory_builder.Append();
  base::MakeSelfDeleting<ZephyrusAdblockProxyingURLLoaderFactory>(
      std::move(receiver), std::move(remote), service->GetWeakPtr(), frame_id,
      std::move(privacy_sink), std::move(privacy_strings),
      std::move(privacy_cname_cache));
}

void ZephyrusAdblockProxyingURLLoaderFactory::CreateLoaderAndStart(
    mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
    int32_t request_id,
    uint32_t options,
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) {
  const GURL initiator =
      request.request_initiator ? request.request_initiator->GetURL() : GURL();
  if (service_ &&
      service_->ShouldBlockRequest(request.url, initiator,
                                   MapDestination(request.destination))) {
    // Attribute the block to the originating tab for the per-page counter.
    if (content::RenderFrameHost* rfh =
            content::RenderFrameHost::FromID(frame_id_)) {
      if (content::WebContents* web_contents =
              content::WebContents::FromRenderFrameHost(rfh)) {
        ZephyrusAdblockTabHelper::CreateForWebContents(web_contents);
        ZephyrusAdblockTabHelper::FromWebContents(web_contents)
            ->IncrementBlocked();
      }
    }
    RecordPrivacyEvent(request.url, /*blocked=*/true);

    // Fail the request without contacting the network.
    mojo::Remote<network::mojom::URLLoaderClient> client_remote(
        std::move(client));
    client_remote->OnComplete(
        network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
    return;
  }

  // Nothing recorded here. The outcome of an unblocked request is not known
  // until it finishes, and §2.1's ALLOWED requires that it FINISHED — so the
  // event is emitted from PrivacyTabHelper::ResourceLoadComplete, which the
  // browser is told about anyway. An earlier version interposed a
  // URLLoaderClient on every request to learn the same thing, and paid for it
  // by routing responses through the browser process.
  // §9.1: harvest the canonical name, but ONLY for a host we have not resolved
  // yet. NeedsResolution() is false for every host already known — cloaked or
  // not — so the steady state attaches nothing and the request path is
  // unchanged. See PrivacyCnameCache.
  mojo::PendingRemote<network::mojom::URLLoaderClient> forwarded = std::move(client);
  if (privacy_cname_cache_) {
    const std::string_view host =
        zephyrus_privacy::CanonicalHostForHash(request.url.host());
    if (!host.empty() && privacy_cname_cache_->NeedsResolution(host)) {
      forwarded = ZephyrusCnameHarvester::Create(
          std::move(forwarded), privacy_cname_cache_, std::string(host));
    }
  }

  target_factory_->CreateLoaderAndStart(std::move(loader_receiver), request_id,
                                        options, request, std::move(forwarded),
                                        traffic_annotation);
}

// One epoch for the whole browser process, so every RawEvent delta shares an
// origin and the consumer can order events from different frames against each
// other.
base::TimeTicks PrivacyEpoch() {
  static const base::TimeTicks epoch = base::TimeTicks::Now();
  return epoch;
}

uint32_t ZephyrusAdblockProxyingURLLoaderFactory::ResolveSiteId() {
  if (site_id_) {
    return *site_id_;
  }
  // 0 is the synthetic site: no frame, no top-level document, or a URL with no
  // eTLD+1 (IP literals, localhost, about:). Spec 9.4 requires these to be
  // excluded from per-page views rather than guessed at.
  uint32_t resolved = 0;
  if (content::RenderFrameHost* rfh =
          content::RenderFrameHost::FromID(frame_id_)) {
    if (content::WebContents* web_contents =
            content::WebContents::FromRenderFrameHost(rfh)) {
      // The TOP-LEVEL document, not the immediate parent frame: a third-party
      // iframe must not become its own "site" (spec 9.4).
      const GURL& top_url =
          web_contents->GetPrimaryMainFrame()->GetLastCommittedURL();
      const std::string etld1 =
          net::registry_controlled_domains::GetDomainAndRegistry(
              top_url,
              net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
      if (!etld1.empty()) {
        // Shared with the popup, which has to derive the same id from the same
        // URL on a different thread with no state in common. Inlining the hash
        // here once let the two drift apart in principle; keeping one function
        // makes that impossible. See SiteIdForEtld1().
        resolved = zephyrus_privacy::SiteIdForEtld1(etld1);
        site_etld1_ = etld1;
      }
    }
  }
  site_id_ = resolved;
  return resolved;
}

void ZephyrusAdblockProxyingURLLoaderFactory::RecordPrivacyEvent(
    const GURL& url,
    bool blocked) {
  if (!privacy_sink_) {
    return;  // Feature off, or an off-the-record profile.
  }
  // host() returns a view into the already-parsed URL: no allocation, no
  // copy, which is what the request-path budget requires (spec 8.1, 8.3).
  // §9.7: strip a trailing dot before hashing. "ads.example." and
  // "ads.example" are the same host, and hashing them differently would file
  // one tracker under two keys and attribute neither.
  const std::string_view host =
      zephyrus_privacy::CanonicalHostForHash(url.host());
  if (host.empty()) {
    return;  // data:, blob:, about: — no eTLD+1 exists (spec 9.2).
  }

  zephyrus_privacy::RawEvent event{};
  event.site_id = ResolveSiteId();

  // §9.4 / P3-1: the page's OWN requests are not tracking, and this path was
  // missing the check that PrivacyTabHelper applies to completed requests.
  // The asymmetry was the bug: an identical request was counted when blocked
  // and ignored when allowed, so `analytics.nike.com` loaded from nike.com
  // appeared under "who tried to reach you" as **Nike** -- naming the site the
  // user had deliberately opened as a company that had reached them.
  //
  // ResolveSiteId() populates site_etld1_ with the TOP-LEVEL registrable
  // domain, so a request is first-party exactly when its host is that domain
  // or a subdomain of it. Testing that as a suffix keeps the hot path
  // allocation-free (§8.1: zero allocations per request), which
  // GetDomainAndRegistry on `host` would not.
  //
  // An empty site_etld1_ means there is no site to compare against (§9.4: IP
  // literal, localhost, about:); those already have no per-page view, so the
  // event is left alone rather than dropped on a comparison we cannot make.
  if (zephyrus_privacy::IsSameSiteHost(host, site_etld1_)) {
    return;
  }
  event.domain_hash = base::PersistentHash(host);
  if (privacy_strings_ && !site_etld1_.empty()) {
    privacy_strings_->RecordSite(event.site_id, site_etld1_);
  }
  if (privacy_strings_) {
    // First sighting only in practice: Record finds the hash present and
    // returns. The aggregator's fold-to-"(other)" bucket is hash 0, and a real
    // domain hashing to 0 is remapped to 1 there, so mirror that remap here or
    // the name would be filed under a key nothing ever looks up.
    privacy_strings_->Record(event.domain_hash ? event.domain_hash : 1u, host);
  }
  // Monotonic delta, never wall clock: an NTP correction or a user changing
  // the clock would otherwise produce negative or absurd values (spec 9.6).
  //
  // Measured from a PROCESS-WIDE epoch, not this factory's construction. A
  // per-factory epoch would make deltas incomparable the moment the consumer
  // merged events from two frames, which is the only thing it ever does.
  const int64_t delta_ms =
      (base::TimeTicks::Now() - PrivacyEpoch()).InMilliseconds();
  // Saturate rather than wrap. A browser session running past ~49 days would
  // otherwise silently restart its clock at zero.
  event.ticks_delta_ms = static_cast<uint32_t>(
      std::clamp<int64_t>(delta_ms, 0, std::numeric_limits<uint32_t>::max()));
  event.entity_id = zephyrus_privacy::kNoEntity;
  event.category = static_cast<uint8_t>(zephyrus_privacy::Category::kUnknown);
  // DETECTED, not ALLOWED, for anything we did not block. ALLOWED means
  // "request completed; data left the device" (spec 2.1) and we have observed
  // no such thing here — the request has not even started. Claiming otherwise
  // would be an accuracy-contract violation. Observing completion needs the
  // URLLoaderClient wrapped for OnComplete, which is later-phase work.
  event.status = static_cast<uint8_t>(
      blocked ? zephyrus_privacy::TrackerStatus::kBlocked
              : zephyrus_privacy::TrackerStatus::kDetected);

  // Return value ignored on purpose: a full ring drops and counts (spec 8.5).
  // Stalling a page load to preserve a statistic is the wrong trade.
  privacy_sink_->Record(event);

  // §6.2's per-page view. Fed from the same call as the durable record above,
  // so the popup and the database can never disagree about what happened —
  // they are two consumers of one event, not two observers of the network.
  //
  // Cheap and thread-legal here: this factory already runs on the UI thread
  // (it resolves RenderFrameHosts a few lines up), so this is a map lookup and
  // an increment, with no task hop on the request path (§8.1).
  const auto status = blocked ? zephyrus_privacy::TrackerStatus::kBlocked
                              : zephyrus_privacy::TrackerStatus::kDetected;
  if (content::RenderFrameHost* rfh =
          content::RenderFrameHost::FromID(frame_id_)) {
    if (content::WebContents* web_contents =
            content::WebContents::FromRenderFrameHost(rfh)) {
      zephyrus_privacy::PrivacyTabHelper::CreateForWebContents(web_contents);
      zephyrus_privacy::PrivacyTabHelper::FromWebContents(web_contents)
          ->RecordRequest(host, status);
    }
  }
}

void ZephyrusAdblockProxyingURLLoaderFactory::OnTargetFactoryError() {
  DisconnectReceiversAndDestroy();
}

}  // namespace zephyrus_adblock
