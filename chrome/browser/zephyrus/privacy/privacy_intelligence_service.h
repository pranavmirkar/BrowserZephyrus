// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_INTELLIGENCE_SERVICE_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_INTELLIGENCE_SERVICE_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <optional>

#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/threading/sequence_bound.h"
#include "base/timer/timer.h"
#include "base/time/time.h"
#include "base/files/file_path.h"
#include "chrome/browser/zephyrus/privacy/domain_string_table.h"
#include "chrome/browser/zephyrus/privacy/entity_artifact.h"
#include "chrome/browser/zephyrus/privacy/entity_resolver.h"
#include "chrome/browser/zephyrus/privacy/privacy_aggregator.h"
#include "chrome/browser/zephyrus/privacy/privacy_cname_cache.h"
#include "chrome/browser/zephyrus/privacy/privacy_cross_site.h"
#include "chrome/browser/zephyrus/privacy/privacy_database.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "chrome/browser/zephyrus/privacy/privacy_event_sink.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "chrome/browser/zephyrus/privacy/privacy_scores.h"
#include "chrome/browser/zephyrus/privacy/privacy_tab_helper.h"
#include "components/keyed_service/core/keyed_service.h"
#include "url/gurl.h"

class PrefService;

namespace zephyrus_privacy {

// One timeline row with its company name attached. Declared ahead of
// PrivacyEventConsumer because the consumer produces these, and ahead of
// PrivacyIntelligenceService because the service hands them to callers; the
// service aliases it as TimelineItem.
struct PrivacyIntelligenceServiceTimelineItem {
  base::Time when;
  std::string site_etld1;
  std::string entity_name;
  EventType event_type = EventType::kFirstSeenOnSite;
  TrackerStatus status = TrackerStatus::kDetected;
};

// Snapshot of pipeline health for chrome://privacy-internals (§8.11). Counters
// only — no domains, no sites, no entities. Anything with browsing data in it
// does not belong in a diagnostic surface (§13.4).
struct PipelineStats {
  uint64_t events_recorded = 0;
  uint64_t events_drained = 0;
  uint64_t events_dropped = 0;
  uint32_t peak_ring_depth = 0;
  size_t current_ring_depth = 0;
  Mode mode = Mode::kDisabled;

  // Persistence state, surfaced by chrome://privacy-internals. §5.3 requires
  // the degraded case to be VISIBLE: an empty history that is actually a
  // deliberate refusal must not read as a bug.
  // §4.2 attribution and §10 visibility: a dataset that was REJECTED must look
  // different from one that was never installed, or a bad artifact reads as a
  // clean web.
  bool has_dataset = false;
  DatasetId dataset_id = DatasetId::kUnknown;
  size_t dataset_entries = 0;
  uint64_t dataset_built_unix_seconds = 0;
  // Read out of the artifact, never hardcoded (§4.2).
  std::string dataset_version;
  std::string dataset_source;
  std::string dataset_licence;
  // §4.4.1. -1 means the artifact carries no publication date, which
  // Freshness() deliberately treats as stale rather than as current.
  int dataset_age_days = -1;
  DatasetFreshness dataset_freshness = DatasetFreshness::kAbsent;

  bool persistence_ready = false;
  bool persistence_refused = false;
  uint64_t rows_flushed = 0;
  size_t known_domain_strings = 0;
};

// The consumer half of the pipeline. Lives on the privacy sequence and is
// reached only through base::SequenceBound, so its sequence affinity cannot be
// violated by a caller forgetting to PostTask.
//
// Phase 1 scope: drain and count. Classification, aggregation and cross-site
// detection land on this class in later increments — the point of building the
// threading first is that they arrive on the correct sequence by construction
// rather than being retrofitted onto one (§8.2).
class PrivacyEventConsumer {
 public:
  PrivacyEventConsumer(scoped_refptr<PrivacyEventSink> sink,
                       scoped_refptr<DomainStringTable> strings);
  PrivacyEventConsumer(const PrivacyEventConsumer&) = delete;
  PrivacyEventConsumer& operator=(const PrivacyEventConsumer&) = delete;
  ~PrivacyEventConsumer();

  // Drains what is queued, up to a bounded amount of work, and returns how
  // many events were taken. The count drives the caller's timer backoff, so an
  // idle profile stops waking up twice a second.
  size_t DrainNow();

  uint64_t events_drained() const;

  // Hands over everything aggregated since the last call, for the cold path to
  // persist. Aggregation happens whether or not anything can be persisted
  // (§5.3), so this is safe to call with no database attached — the rows are
  // simply dropped by the caller.
  // Rows plus the lifetime delta, taken together in one hop. Two round trips
  // would let a Clear() land between them and lose or double-count a delta.
  struct FlushBatch {
    FlushBatch();
    FlushBatch(FlushBatch&&);
    FlushBatch& operator=(FlushBatch&&);
    ~FlushBatch();
    std::vector<DailyRow> rows;
    PrivacyAggregator::LifetimeDelta lifetime;
  };
  FlushBatch TakeRows(int64_t day);

  PrivacyAggregator::SessionTotals session_totals() const;

  // Resolves domains to their owning company, on the sequence where the entity
  // dataset lives. Returns one entry per input, in the same order.
  //
  // The popup cannot do this itself: the resolver is owned here, and copying
  // the dataset to the UI thread to avoid one task hop would duplicate several
  // megabytes to save microseconds. One hop per popup open is the right trade
  // (§8.9).
  struct ResolvedDomain {
    uint16_t entity_id = kNoEntity;
    Category category = Category::kUnknown;
    // Empty when no dataset covers the domain. §4.2: the caller renders the
    // bare domain rather than inventing an owner or a category.
    std::string entity_name;
  };
  std::vector<ResolvedDomain> ResolveDomains(std::vector<std::string> domains);

  // Turns raw database rows into §6.4's answer, on the sequence that owns the
  // entity dataset — which is the only place that can decide two things this
  // needs: what a tracker entity is CALLED, and whether a visited site is one
  // the entity itself owns (§9.5). Doing it here keeps it to a single hop.
  std::vector<CrossSiteEntity> BuildCrossSite(
      std::vector<PrivacyDatabase::CrossSiteRow> rows);

  // Grouping, naming and §9.5 ownership marking, shared by §6.4 and §6.7.
  // Applies NO policy of its own — the two views ask different questions of
  // the same rows, and keeping the gathering separate is what stops one of
  // them quietly reimplementing the other's rules.
  std::vector<CrossSiteEntity> GroupCrossSiteRows(
      std::vector<PrivacyDatabase::CrossSiteRow> rows);

  // Attaches company names to timeline rows, on the sequence that owns the
  // dataset. Declared after TimelineItem in the service below; see there.
  // §6.7. Shares BuildCrossSite's grouping and naming, then answers a
  // different question: who reached you, with no threshold and no ownership
  // exclusion.
  std::vector<ExposureEntity> BuildExposure(
      std::vector<PrivacyDatabase::CrossSiteRow> rows);

  std::vector<PrivacyIntelligenceServiceTimelineItem> NameTimeline(
      std::vector<PrivacyDatabase::TimelineEntry> entries);

  // Clear Browsing Data. Drains the ring first, so events emitted before the
  // clear cannot repopulate the aggregates a moment after it.
  void ClearInMemory();

  // Installs a dataset (or a replacement). Ordering matters: the aggregator is
  // repointed BEFORE the previous resolver is destroyed.
  void SetResolver(std::unique_ptr<EntityResolver> resolver);

 private:
  SEQUENCE_CHECKER(sequence_checker_);
  scoped_refptr<PrivacyEventSink> sink_;
  uint64_t events_drained_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;
  // Allocated once. A per-tick vector would be 16 KB of churn twice a second
  // for the life of the profile.
  std::vector<RawEvent> batch_ GUARDED_BY_CONTEXT(sequence_checker_);

  // No dataset until an artifact is installed; the null resolver keeps every
  // caller on one path (§4.1).
  std::unique_ptr<EntityResolver> resolver_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<PrivacyAggregator> aggregator_
      GUARDED_BY_CONTEXT(sequence_checker_);
};

// Per-profile owner of the Privacy Intelligence pipeline.
//
// **Never instantiated for off-the-record profiles.** §5.2 requires incognito
// events to be dropped at the service boundary, and the strictest way to
// honour that is for there to be no boundary at all: the factory returns null,
// the emission point finds no sink, and nothing is collected — let alone
// persisted. See PrivacyIntelligenceServiceFactory.
class PrivacyIntelligenceService : public KeyedService {
 public:
  // `os_crypt` may be null in tests, in which case the service never gains
  // persistence and runs in-memory only — which is a supported state, not an
  // error (§5.3).
  PrivacyIntelligenceService(base::FilePath profile_path,
                             PrefService* prefs,
                             os_crypt_async::OSCryptAsync* os_crypt);
  PrivacyIntelligenceService(const PrivacyIntelligenceService&) = delete;
  PrivacyIntelligenceService& operator=(const PrivacyIntelligenceService&) =
      delete;
  ~PrivacyIntelligenceService() override;

  // Handed to the network-thread emission point. Refcounted so the ring
  // outlives whichever side shuts down first — see PrivacyEventSink.
  scoped_refptr<PrivacyEventSink> sink() { return sink_; }

  // Handed to the emission point alongside the sink so domain STRINGS reach
  // the privacy sequence; RawEvent carries only hashes (§8.5).
  scoped_refptr<DomainStringTable> domain_strings() { return strings_; }

  // §9.1's hostname -> canonical eTLD+1 cache. Shared with the emission point,
  // which uses it to decide whether a request needs its canonical name
  // harvested at all.
  scoped_refptr<PrivacyCnameCache> cname_cache() { return cname_cache_; }

  // -- Renderer-observed and connection-level page signals (§6.5, §9.2.1) ----
  //
  // These describe something the PAGE did that no network request reveals, so
  // they arrive from outside the ring buffer: from the renderer over
  // zephyrus_privacy.mojom, and from PeerConnectionTrackerHostObserver. All
  // take a URL rather than a site id because that is what the callers hold; the
  // fold to eTLD+1 happens here, once.
  //
  // Called on the UI thread. Both are no-ops for a URL with no registrable
  // domain (§9.7) — an IP literal or single-label host has no site to attribute
  // to, and inventing one is worse than recording nothing.

  // Records that `page_url`'s document touched `surface`.
  //
  // Idempotent per (site, surface) for the session: repeats are dropped. A page
  // that calls the same API in a loop has not learned anything new, and a
  // compromised renderer must not be able to inflate the user's numbers by
  // calling it a million times. This is the whole rate-limit — there is no
  // timer, because the set of surfaces is closed and small.
  void RecordFingerprintSurface(const GURL& page_url,
                                FingerprintSurface surface);

  // §9.2.1: the page constructed an RTCPeerConnection and can gather ICE
  // candidates. `local_addresses_withheld` records what the WebRTC IP handling
  // policy actually did, so the UI can say "local addresses were withheld"
  // only when they were. Never reported as blocked — the connection was not
  // blocked, and §2 has no status for "we made it less bad".
  void RecordWebrtcAddressRequest(const GURL& page_url,
                                  bool local_addresses_withheld);

  // -- §6.2 current site analysis --------------------------------------------

  // One third-party the current page contacted, as the popup renders it.
  struct TrackerRow {
    // Always populated. Shown verbatim when `entity_name` is empty (§4.2:
    // unknown domains render as the bare domain with no invented owner).
    std::string domain;
    std::string entity_name;
    Category category = Category::kUnknown;
    uint32_t requests = 0;
    uint32_t blocked = 0;
    // The row's headline status. kBlocked ONLY when every counted request to
    // this domain was blocked — a row where anything got through must not
    // claim "Blocked", which is the §2 accuracy rule applied per row.
    TrackerStatus status = TrackerStatus::kDetected;
  };

  struct PageAnalysis {
    // Sorted per §6.2: blocked first, then by request count descending, then
    // by domain so the order is stable between two opens of the same popup.
    std::vector<TrackerRow> rows;
    uint32_t detected = 0;
    uint32_t blocked = 0;
    uint32_t allowed = 0;
    // Third-party cookie accesses that were prevented, for §6.6.
    uint32_t cookies_blocked = 0;
    // nullopt when the page made no third-party requests at all — see
    // ComputeProtectionApplied. Not 0%.
    std::optional<ProtectionApplied> protection;
    TrackingIntensity intensity;
  };

  // Assembles §6.1 and §6.2 for the page `rows` were collected on.
  //
  // Asynchronous because attribution needs the entity dataset, which lives on
  // the privacy sequence. §8.9 forbids blocking the UI thread to reach it, so
  // the popup renders a frame without owner names and fills them in — rather
  // than janking the browser to open a bubble.
  using PageAnalysisCallback = base::OnceCallback<void(PageAnalysis)>;
  void AnalyzeCurrentPage(const GURL& page_url,
                          std::vector<PrivacyTabHelper::DomainRow> rows,
                          PrivacyTabHelper::PageTotals totals,
                          PageAnalysisCallback callback);

  // §9: the user turned blocking off for `page_url`'s site.
  //
  // **The highest-quality breakage signal available.** A user who allows
  // trackers on a site almost always means a filter rule broke the page —
  // nobody opts into being tracked for fun. Recorded locally and never
  // transmitted (§7); it exists so the timeline can explain later why a site's
  // numbers changed, and so a future review can find the rules that cost users
  // a working page.
  //
  // Recording only. Turning blocking off is the ad blocker's allowlist, and
  // this layer never reaches into enforcement (§3.1) — the caller does both.
  void RecordUserAllowedSite(const GURL& page_url);

  // What the popup reads back for the site it is describing (§6.1, §6.2).
  struct PageSignals {
    // Bit per FingerprintSurface value seen. A mask rather than a count
    // because §6.5 counts distinct surfaces touched, not calls made.
    uint32_t fingerprint_surface_mask = 0;
    // When the first surface on this site was touched, for §6.5's five-second
    // window.
    //
    // NOTE this approximates "within 5 s of load" as "within 5 s of the first
    // surface". There is no per-page-load timestamp here — this map is keyed by
    // site, not by document — and the burst reading is arguably the truer one:
    // what marks a sweep is that the surfaces are touched TOGETHER, whenever
    // the script happens to run. A script that waits ten seconds and then
    // sweeps is still sweeping, and the literal reading would miss it.
    base::TimeTicks first_surface_at;
    // Documents on this site that constructed a peer connection.
    uint32_t webrtc_address_requests = 0;
    // True only if every recorded request ran under a policy that withheld
    // local addresses. One request under a laxer policy makes this false, so
    // the reassuring string is never shown on the strength of a different
    // request that happened to be protected.
    bool local_addresses_withheld = true;

    // Distinct fingerprinting surfaces, for IntensityInputs.
    uint32_t fingerprint_surface_count() const;
  };
  PageSignals GetPageSignals(const GURL& page_url) const;

  // Folds `page_url` to the registrable domain the two Record methods above key
  // by, or returns empty when it has none. Empty means "do not record", never
  // "record under some fallback key".
  //
  // Public and static because it is a pure function of the URL and it is the
  // rule that decides what may be attributed at all (§9.7) — worth testing
  // directly rather than only through its effects.
  static std::string SiteKeyFor(const GURL& page_url);

  // §6.4 cross-site monitor, for the dashboard. Asynchronous: it reads the
  // database and then needs the entity dataset, neither of which is reachable
  // from the UI thread (§8.9). Runs the callback with an empty list when there
  // is no database, which is a legitimate state (§5.3).
  using CrossSiteCallback =
      base::OnceCallback<void(std::vector<CrossSiteEntity>)>;
  void GetCrossSiteEntities(CrossSiteCallback callback);

  // §6.7 "who tried to reach you today". Scoped to TODAY specifically rather
  // than reusing the 7-day cross-site window: the heading says today, and
  // showing a week under it would be false in the plainest way.
  using ExposureCallback =
      base::OnceCallback<void(std::vector<ExposureEntity>)>;
  void GetExposureToday(ExposureCallback callback);

  // §6.8's timeline, resolved for display. Asynchronous for the same reason as
  // the cross-site query: the database and the entity dataset are both off the
  // UI thread (§8.9).
  // Empty entity_name means no dataset covers it, or the event was about the
  // page rather than a company (§4.2: never invent an owner).
  using TimelineItem = PrivacyIntelligenceServiceTimelineItem;
  using TimelineCallback = base::OnceCallback<void(std::vector<TimelineItem>)>;
  void GetTimeline(int limit, TimelineCallback callback);

  // Clear Browsing Data. Both are no-ops when persistence never started.
  void DeleteAllBrowsingData();
  void DeleteRange(base::Time begin, base::Time end);

  // For internals and tests. Cheap: reads atomics, never touches the DB.
  PipelineStats GetStats() const;

  // Row counts and file size, for chrome://privacy-internals. Async because it
  // hits the disk. Runs the callback with nullopt when there is no database,
  // which is a legitimate state rather than an error (§5.3).
  using DatabaseStatsCallback =
      base::OnceCallback<void(std::optional<DatabaseStats>)>;
  void GetDatabaseStats(DatabaseStatsCallback callback);

  // Everything GetStats() has, plus the counters that live on other sequences.
  // Asynchronous because reaching them synchronously would mean blocking the UI
  // thread on the privacy sequence and then on the database sequence, to
  // populate a diagnostic (§8.9 forbids exactly that trade).
  using FullStatsCallback =
      base::OnceCallback<void(PipelineStats, std::optional<DatabaseStats>)>;
  void GetFullStats(FullStatsCallback callback);

  // KeyedService:
  void Shutdown() override;

 private:
  void OnDrainTimer();
  void OnDrained(size_t count);
  void OnEncryptorReady(scoped_refptr<os_crypt_async::Encryptor> encryptor);
  void OnFlushTimer();
  void OnPruneTimer();
  void OnDatasetLoaded(std::unique_ptr<EntityResolver> resolver);
  void OnRowsReady(uint32_t clear_generation,
                   PrivacyEventConsumer::FlushBatch batch);
  void OnDrainedCountForStats(FullStatsCallback callback,
                              PipelineStats stats,
                              uint64_t events_drained);
  // Second half of AnalyzeCurrentPage, once attribution comes back from the
  // privacy sequence.
  void OnExposureRows(ExposureCallback callback,
                      std::vector<PrivacyDatabase::CrossSiteRow> rows);

  void OnTimelineRows(TimelineCallback callback,
                      std::vector<PrivacyDatabase::TimelineEntry> entries);

  void OnCrossSiteRows(CrossSiteCallback callback,
                       std::vector<PrivacyDatabase::CrossSiteRow> rows);

  void OnDomainsResolved(
      std::vector<PrivacyTabHelper::DomainRow> rows,
      PrivacyTabHelper::PageTotals totals,
      PageSignals signals,
      PageAnalysisCallback callback,
      std::vector<PrivacyEventConsumer::ResolvedDomain> resolved);
  // Restarts the timer only when the interval actually changes.
  void SetDrainInterval(base::TimeDelta interval);

  // Appends to the timeline when persistence exists. Silently does nothing
  // otherwise, which is the documented in-memory-only mode (§5.3).
  void AppendTimelineEvent(const std::string& site_key,
                           EventType type,
                           TrackerStatus status);

  SEQUENCE_CHECKER(sequence_checker_);

  scoped_refptr<PrivacyEventSink> sink_;
  scoped_refptr<DomainStringTable> strings_;
  scoped_refptr<PrivacyCnameCache> cname_cache_;
  base::SequenceBound<PrivacyEventConsumer> consumer_;
  // Created only once an encryptor arrives. An unbound SequenceBound means
  // "no persistence", which is a legitimate steady state.
  base::SequenceBound<PrivacyDatabase> db_;

  const base::FilePath profile_path_;
  raw_ptr<PrefService> prefs_;
  base::RepeatingTimer flush_timer_ GUARDED_BY_CONTEXT(sequence_checker_);
  // Retention (§5.2). Without this the schema's day windows and size cap
  // are decoration: nothing ever deletes and the file grows for as long as
  // the profile lives.
  base::RepeatingTimer prune_timer_ GUARDED_BY_CONTEXT(sequence_checker_);
  bool persistence_ready_ GUARDED_BY_CONTEXT(sequence_checker_) = false;
  bool persistence_refused_ GUARDED_BY_CONTEXT(sequence_checker_) = false;
  uint64_t rows_flushed_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;
  // Bumped by every clear. A flush that was already in flight when the user
  // cleared carries the old value and is dropped on arrival — otherwise its
  // rows, gathered BEFORE the clear, would be written to the database AFTER it
  // and quietly resurrect the data the user just deleted.
  uint32_t clear_generation_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;
  // When this service started collecting. A ranged clear ending after this
  // overlaps the current session, so the in-memory counts have to go too.
  const base::Time session_start_;
  // Provenance of whatever dataset is live, mirrored here so GetStats() does
  // not have to hop to the privacy sequence for it.
  // Whether the dataset load has finished, successfully or not. Until it has,
  // flushing is held back: a row written before the resolver arrives is
  // persisted with entity_id = NULL and stays that way forever, so every
  // browser start would permanently lose attribution for the first seconds of
  // browsing — exactly the window in which a page loads all its trackers.
  bool dataset_load_settled_ GUARDED_BY_CONTEXT(sequence_checker_) = false;
  bool has_dataset_ GUARDED_BY_CONTEXT(sequence_checker_) = false;

  // Live per-site page signals, keyed by eTLD+1. Small by construction: one
  // entry per site visited this session, each a few bytes.
  //
  // Capped, because the key comes indirectly from page content and a session
  // that visits a great many sites must not grow it without bound. On reaching
  // the cap new sites stop being recorded rather than evicting old ones: the
  // popup then shows nothing for that site, which is honest, whereas eviction
  // would make an already-recorded site silently revert to "nothing observed".
  static constexpr size_t kMaxTrackedSites = 1024;
  std::map<std::string, PageSignals> page_signals_
      GUARDED_BY_CONTEXT(sequence_checker_);
  DatasetId dataset_id_ GUARDED_BY_CONTEXT(sequence_checker_) =
      DatasetId::kUnknown;
  size_t dataset_entries_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;
  uint64_t dataset_built_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;
  std::string dataset_version_ GUARDED_BY_CONTEXT(sequence_checker_);
  std::string dataset_source_ GUARDED_BY_CONTEXT(sequence_checker_);
  std::string dataset_licence_ GUARDED_BY_CONTEXT(sequence_checker_);
  uint64_t dataset_published_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;
  DatasetFreshness dataset_freshness_ GUARDED_BY_CONTEXT(sequence_checker_) =
      DatasetFreshness::kAbsent;
  base::RepeatingTimer drain_timer_ GUARDED_BY_CONTEXT(sequence_checker_);
  base::TimeDelta drain_interval_ GUARDED_BY_CONTEXT(sequence_checker_);
  int consecutive_empty_drains_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;
  bool shut_down_ GUARDED_BY_CONTEXT(sequence_checker_) = false;

  base::WeakPtrFactory<PrivacyIntelligenceService> weak_factory_{this};
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_INTELLIGENCE_SERVICE_H_
