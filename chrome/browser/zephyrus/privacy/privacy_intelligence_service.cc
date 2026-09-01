// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include <bit>
#include "chrome/browser/zephyrus/privacy/fingerprint_attempt.h"

#include <algorithm>
#include <bit>
#include <set>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/task/thread_pool.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "chrome/browser/zephyrus/privacy/privacy_crypto_impl.h"
#include "chrome/browser/zephyrus/privacy/privacy_host_canon.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/browser/zephyrus/privacy/privacy_ring_buffer.h"

namespace zephyrus_privacy {

namespace {

// The ring holds 4096 events. Draining twice a second keeps occupancy far
// below that while a page is loading, so drops mean the consumer is genuinely
// behind rather than the timer being lazy. This is NOT the DB flush interval —
// §5.2's 5 seconds applies to writes, further down the cold path.
constexpr base::TimeDelta kActiveDrainInterval = base::Milliseconds(500);

// What an idle profile falls back to. A timer firing twice a second for the
// lifetime of a profile that is recording nothing is pure battery cost, and
// Chromium is rightly strict about idle wakeups. Worst case this delays the
// first events of a burst by a few seconds; the ring holds them.
constexpr base::TimeDelta kIdleDrainInterval = base::Seconds(4);
constexpr int kEmptyDrainsBeforeIdle = 6;

// §13.3 kill-switch thresholds.
//
// Every one of these is set so that ONLY a pipeline that is already failing to
// do its job can reach it. That asymmetry is deliberate: a false trip silently
// costs the user their privacy history, while a missed trip costs some browsing
// smoothness that the user can at least feel. Neither is free, so the bar is
// "this is already broken", not "this looks slow".

// A drain interval that loses more than a FULL RING is not a burst, it is a
// consumer that cannot keep up: at 4096 events lost between drains, the events
// being counted are already an arbitrary subset, so the numbers shown to the
// user would be wrong anyway.
constexpr uint64_t kOverflowDropsPerDrain = PrivacyRingBuffer::kCapacity;
// Three intervals running, so one pathological page load cannot condemn the
// session. At the active interval that is ~1.5 seconds of sustained loss.
constexpr int kOverflowDrainsBeforeTrip = 3;

// The drain round trip is one task hop to the privacy sequence and back. It is
// normally sub-millisecond; two seconds means that sequence is starved or
// blocked behind something far more important than this feature.
constexpr base::TimeDelta kSlowDrainRoundTrip = base::Seconds(2);
constexpr int kSlowDrainsBeforeTrip = 3;

// Writes fail for reasons that do not clear up by themselves — a full disk, a
// revoked keystore, a razed database. Five consecutive failures is past the
// point where retrying forever helps.
constexpr int kDbFailuresBeforeTrip = 5;

// Bounded per-drain work, twice over: a batch is 1024 events, and a tick takes
// at most kMaxBatchesPerTick of them. Without the second bound a page flooding
// the ring (§11.3) could keep one USER_VISIBLE task running indefinitely,
// because the loop would exit only when the producer paused. Overflow spills
// to the next tick, which is what the ring is for.
constexpr size_t kMaxDrainPerBatch = 1024;
constexpr int kMaxBatchesPerTick = 8;

// §5.2: one transaction per flush, every 5 seconds. Separate from the drain
// timer — draining keeps the ring from overflowing, flushing writes to disk,
// and the two want very different rates.
constexpr base::TimeDelta kFlushInterval = base::Seconds(5);

// Retention sweep. §8.8 calls this idle-time work, so it runs rarely and on a
// BEST_EFFORT-adjacent cadence rather than on a schedule anyone waits for. The
// first sweep is deliberately delayed: startup is the worst moment to touch the
// disk, and nothing expires in the first few minutes of a session anyway.
constexpr base::TimeDelta kPruneInterval = base::Minutes(30);

// How long a flush waits for the entity dataset before giving up and writing
// rows unattributed. The artifact is ~800 KB and its signature covers every
// byte, so the load is not instant — and the first page of a session fires all
// its tracker requests well inside that window.
//
// A deadline rather than an unbounded wait: if the load task never runs, events
// must still reach disk. Nothing is lost while waiting, because the consumer
// keeps draining the ring into the aggregator throughout; the only cost is that
// those rows land a few seconds later.
constexpr base::TimeDelta kDatasetWaitDeadline = base::Seconds(20);
constexpr base::TimeDelta kFirstPruneDelay = base::Minutes(3);

// Where a dataset artifact lives. Browser-wide, not per-profile: it is
// read-only reference data, identical for every profile, and mapping one copy
// is the whole point of a standalone mmap'd file (§4.1).
//
// The user-data-dir location is deliberately the one a Component Updater
// install would write to, so wiring that up later changes who PUTS the file
// there and nothing about who reads it. The directory beside the executable is
// the fallback for a snapshot shipped with the build.
constexpr base::FilePath::CharType kDatasetDirName[] =
    FILE_PATH_LITERAL("ZephyrusEntities");
constexpr base::FilePath::CharType kDatasetFileName[] =
    FILE_PATH_LITERAL("zephyrus_entities.dat");
constexpr char kDatasetPathSwitch[] = "zephyrus-entity-dataset";

// Blocking. Runs on a MayBlock sequence, never on the UI thread.
std::unique_ptr<EntityResolver> LoadDataset() {
  const base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(kDatasetPathSwitch)) {
    // Explicit path wins, for testing and for a hand-installed dataset.
    return EntityResolver::CreateFromFile(
        cmd->GetSwitchValuePath(kDatasetPathSwitch));
  }
  base::FilePath dir;
  if (base::PathService::Get(chrome::DIR_USER_DATA, &dir)) {
    const base::FilePath path =
        dir.Append(kDatasetDirName).Append(kDatasetFileName);
    if (base::PathExists(path)) {
      return EntityResolver::CreateFromFile(path);
    }
  }
  if (base::PathService::Get(base::DIR_MODULE, &dir)) {
    const base::FilePath path =
        dir.Append(kDatasetDirName).Append(kDatasetFileName);
    if (base::PathExists(path)) {
      return EntityResolver::CreateFromFile(path);
    }
  }
  // No artifact installed. Not an error: §4.1 makes running without a dataset
  // a supported state, and CreateNull keeps every caller on one path.
  return EntityResolver::CreateNull();
}

int64_t UtcDayNow() {
  // UTC, so a timezone change or DST cannot duplicate a day (§9.6).
  return base::Time::Now().InMillisecondsSinceUnixEpoch() /
         base::Time::kMillisecondsPerDay;
}

}  // namespace

// ---------------------------------------------------------------------------
// PrivacyEventConsumer — privacy sequence
// ---------------------------------------------------------------------------

PrivacyEventConsumer::PrivacyEventConsumer(
    scoped_refptr<PrivacyEventSink> sink,
    scoped_refptr<DomainStringTable> strings)
    : sink_(std::move(sink)),
      resolver_(EntityResolver::CreateNull()),
      aggregator_(std::make_unique<PrivacyAggregator>(std::move(strings),
                                                      resolver_.get())) {
  // Constructed on the caller's sequence and used on the bound one; the first
  // call on the bound sequence establishes affinity.
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

PrivacyEventConsumer::~PrivacyEventConsumer() = default;

size_t PrivacyEventConsumer::DrainNow() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!sink_) {
    return 0;
  }
  if (batch_.empty()) {
    batch_.resize(kMaxDrainPerBatch);
  }
  size_t total = 0;
  for (int i = 0; i < kMaxBatchesPerTick; ++i) {
    const size_t count = sink_->Drain(base::span<RawEvent>(batch_));
    if (count == 0) {
      break;
    }
    total += count;
    events_drained_ += count;

    aggregator_->AddBatch(
        base::span<const RawEvent>(batch_).first(count));

    if (count < batch_.size()) {
      break;  // Ring is dry; do not spin against a live producer.
    }
  }
  return total;
}

uint64_t PrivacyEventConsumer::events_drained() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return events_drained_;
}

PrivacyEventConsumer::FlushBatch::FlushBatch() = default;
PrivacyEventConsumer::FlushBatch::FlushBatch(FlushBatch&&) = default;
PrivacyEventConsumer::FlushBatch& PrivacyEventConsumer::FlushBatch::operator=(
    FlushBatch&&) = default;
PrivacyEventConsumer::FlushBatch::~FlushBatch() = default;

PrivacyEventConsumer::FlushBatch PrivacyEventConsumer::TakeRows(int64_t day) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Drain first so a flush never leaves freshly-arrived events behind for
  // another five seconds.
  DrainNow();
  FlushBatch batch;
  batch.rows = aggregator_->TakeRows(day);
  batch.lifetime = aggregator_->TakeLifetimeDelta();
  return batch;
}

void PrivacyEventConsumer::SetResolver(
    std::unique_ptr<EntityResolver> resolver) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!resolver) {
    return;
  }
  // Hold the outgoing one until the aggregator has been repointed, or the
  // aggregator would briefly hold a dangling pointer to a freed table.
  std::unique_ptr<EntityResolver> previous = std::move(resolver_);
  resolver_ = std::move(resolver);
  aggregator_->SetResolver(resolver_.get());
}

void PrivacyEventConsumer::ClearInMemory() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Drain before clearing: anything still queued was emitted before the user
  // asked for the clear, and would otherwise land in the aggregates right
  // after it.
  DrainNow();
  aggregator_->Clear();
}

PrivacyAggregator::SessionTotals PrivacyEventConsumer::session_totals() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return aggregator_->session_totals();
}

std::vector<PrivacyEventConsumer::ResolvedDomain>
PrivacyEventConsumer::ResolveDomains(std::vector<std::string> domains) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<ResolvedDomain> out;
  out.reserve(domains.size());
  for (const std::string& domain : domains) {
    ResolvedDomain entry;
    const EntityInfo info = LookupEntityWithFallback(
        resolver_.get(), base::PersistentHash(CanonicalHostForHash(domain)),
        domain);
    entry.entity_id = info.entity_id;
    entry.category = info.category;
    if (info.resolved() && resolver_) {
      // Copied out of the artifact rather than returned as a view: the string
      // lives in a buffer this sequence owns and a dataset swap (§4.4) can free
      // it the moment this call returns.
      entry.entity_name = std::string(resolver_->GetEntityName(info.entity_id));
    }
    out.push_back(std::move(entry));
  }
  return out;
}

// ---------------------------------------------------------------------------
// PrivacyIntelligenceService — UI sequence
// ---------------------------------------------------------------------------

PrivacyIntelligenceService::PrivacyIntelligenceService(
    base::FilePath profile_path,
    PrefService* prefs,
    os_crypt_async::OSCryptAsync* os_crypt)
    : sink_(base::MakeRefCounted<PrivacyEventSink>()),
      strings_(base::MakeRefCounted<DomainStringTable>()),
      cname_cache_(base::MakeRefCounted<PrivacyCnameCache>()),
      profile_path_(std::move(profile_path)),
      prefs_(prefs),
      session_start_(base::Time::Now()) {
  // USER_VISIBLE per §3.2: this feeds UI the user is looking at, but it must
  // never outrank the work of actually rendering pages.
  consumer_ = base::SequenceBound<PrivacyEventConsumer>(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}),
      sink_, strings_);

  SetDrainInterval(kActiveDrainInterval);

  // Attribution is reference data, not user data: loading it has nothing to do
  // with the keystore and must not wait behind it.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&LoadDataset),
      base::BindOnce(&PrivacyIntelligenceService::OnDatasetLoaded,
                     weak_factory_.GetWeakPtr()));

  // Counting starts now and never waits for the keystore. Persistence begins
  // if and when an encryptor arrives; if it never does, the session stays
  // in-memory, which §5.3 defines as a supported state rather than a failure.
  flush_timer_.Start(FROM_HERE, kFlushInterval,
                     base::BindRepeating(
                         &PrivacyIntelligenceService::OnFlushTimer,
                         base::Unretained(this)));

  if (os_crypt) {
    os_crypt->GetInstance(
        base::BindOnce(&PrivacyIntelligenceService::OnEncryptorReady,
                       weak_factory_.GetWeakPtr()));
  } else {
    persistence_refused_ = true;
  }
}

void PrivacyIntelligenceService::OnEncryptorReady(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shut_down_) {
    return;
  }
  std::unique_ptr<PrivacyCrypto> crypto =
      PrivacyCryptoImpl::Create(std::move(encryptor), prefs_);
  if (!crypto) {
    // No key, no database (§5.3). In-memory statistics keep working; the
    // internals page reports why the history is empty.
    persistence_refused_ = true;
    LOG(ERROR) << "Zephyrus privacy: no usable encryptor; running in-memory "
                  "only for this session.";
    return;
  }

  // Retention starts only once there is a database to prune.
  prune_timer_.Start(FROM_HERE, kFirstPruneDelay,
                     base::BindRepeating(
                         &PrivacyIntelligenceService::OnPruneTimer,
                         base::Unretained(this)));

  db_ = base::SequenceBound<PrivacyDatabase>(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}),
      profile_path_.AppendASCII("ZephyrusPrivacy.db"), std::move(crypto));
  persistence_ready_ = true;
}

void PrivacyIntelligenceService::OnFlushTimer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shut_down_) {
    return;
  }
  if (!dataset_load_settled_ &&
      base::Time::Now() - session_start_ < kDatasetWaitDeadline) {
    // Hold these rows in memory a moment longer rather than persisting them
    // permanently unattributed.
    return;
  }
  consumer_.AsyncCall(&PrivacyEventConsumer::TakeRows)
      .WithArgs(UtcDayNow())
      .Then(base::BindOnce(&PrivacyIntelligenceService::OnRowsReady,
                           weak_factory_.GetWeakPtr(), clear_generation_));
}

void PrivacyIntelligenceService::OnDatasetLoaded(
    std::unique_ptr<EntityResolver> resolver) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!resolver) {
    // LoadDataset always returns something today, but a null must never wedge
    // flushing until the deadline on every single start.
    dataset_load_settled_ = true;
    return;
  }
  if (shut_down_) {
    return;
  }
  dataset_load_settled_ = true;
  has_dataset_ = resolver->has_dataset();
  dataset_id_ = resolver->dataset_id();
  dataset_entries_ = resolver->entry_count();
  dataset_built_ = resolver->built_unix_seconds();
  dataset_version_ = std::string(resolver->dataset_version());
  dataset_source_ = std::string(resolver->dataset_source());
  dataset_licence_ = std::string(resolver->dataset_licence());
  dataset_published_ = resolver->dataset_published_unix_seconds();
  // Snapshotted at load, not recomputed per read: a session running past
  // midnight on the 60-day boundary should not have surfaces disagreeing about
  // the wording mid-session.
  dataset_freshness_ = resolver->Freshness(base::Time::Now());
  consumer_.AsyncCall(&PrivacyEventConsumer::SetResolver)
      .WithArgs(std::move(resolver));
}

void PrivacyIntelligenceService::OnPruneTimer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shut_down_ || !db_) {
    return;
  }
  // The first shot was a one-off short delay; settle into the real cadence.
  if (prune_timer_.GetCurrentDelay() != kPruneInterval) {
    prune_timer_.Start(FROM_HERE, kPruneInterval,
                       base::BindRepeating(
                           &PrivacyIntelligenceService::OnPruneTimer,
                           base::Unretained(this)));
  }
  // Defaults, which Prune clamps against the browser's own history retention
  // (§5.2.2) before applying.
  db_.AsyncCall(base::IgnoreResult(&PrivacyDatabase::Prune))
      .WithArgs(RetentionConfig());
}

void PrivacyIntelligenceService::OnRowsReady(
    uint32_t clear_generation,
    PrivacyEventConsumer::FlushBatch batch) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (clear_generation != clear_generation_) {
    return;  // Gathered before a clear that has since happened.
  }
  if (shut_down_ || (batch.rows.empty() && batch.lifetime.empty())) {
    return;
  }
  if (!db_) {
    // Aggregation happened anyway; the rows are simply discarded. This is the
    // in-memory-only path and is deliberately silent — it is reported once via
    // internals, not logged every five seconds.
    return;
  }
  rows_flushed_ += batch.rows.size();
  if (!batch.lifetime.empty()) {
    // Before the rows, so a crash between the two under-reports the headline
    // rather than showing a lifetime total smaller than the stored history.
    db_.AsyncCall(base::IgnoreResult(&PrivacyDatabase::AddLifetime))
        .WithArgs(batch.lifetime.detected, batch.lifetime.blocked,
                  batch.lifetime.randomized);
  }
  if (!batch.rows.empty()) {
    // Observed, not ignored: repeated write failures are one of §13.3's three
    // kill-switch triggers, and IgnoreResult made them invisible. Note the
    // shape — AsyncCall on a bool-returning method needs EITHER IgnoreResult or
    // a .Then() that takes the bool; neither one is optional.
    db_.AsyncCall(&PrivacyDatabase::FlushDailyRows)
        .WithArgs(std::move(batch.rows))
        .Then(base::BindOnce(&PrivacyIntelligenceService::OnFlushResult,
                             weak_factory_.GetWeakPtr()));
  }
}

void PrivacyIntelligenceService::GetFullStats(FullStatsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  consumer_.AsyncCall(&PrivacyEventConsumer::events_drained)
      .Then(base::BindOnce(
          &PrivacyIntelligenceService::OnDrainedCountForStats,
          weak_factory_.GetWeakPtr(), std::move(callback), GetStats()));
}

void PrivacyIntelligenceService::OnDrainedCountForStats(
    FullStatsCallback callback,
    PipelineStats stats,
    uint64_t events_drained) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  stats.events_drained = events_drained;
  GetDatabaseStats(base::BindOnce(
      [](FullStatsCallback cb, PipelineStats s,
         std::optional<DatabaseStats> db) {
        std::move(cb).Run(std::move(s), std::move(db));
      },
      std::move(callback), std::move(stats)));
}

void PrivacyIntelligenceService::GetDatabaseStats(
    DatabaseStatsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!db_) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  db_.AsyncCall(&PrivacyDatabase::GetStats)
      .Then(base::BindOnce(
          [](DatabaseStatsCallback cb, DatabaseStats stats) {
            std::move(cb).Run(std::move(stats));
          },
          std::move(callback)));
}

void PrivacyIntelligenceService::DeleteAllBrowsingData() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++clear_generation_;
  // In-memory state goes too: clearing history must not leave this session's
  // counts visible in the popup. TakeRows would only reset the per-site
  // aggregates and leave the session TOTALS — the numbers the popup actually
  // shows — climbing from where they were.
  strings_->Clear();
  page_signals_.clear();
  consumer_.AsyncCall(&PrivacyEventConsumer::ClearInMemory);
  if (db_) {
    db_.AsyncCall(base::IgnoreResult(&PrivacyDatabase::DeleteAllBrowsingData));
  }
}

uint32_t PrivacyIntelligenceService::PageSignals::fingerprint_surface_count()
    const {
  return static_cast<uint32_t>(std::popcount(fingerprint_surface_mask));
}

uint32_t
PrivacyIntelligenceService::PageSignals::fingerprint_randomized_count() const {
  return static_cast<uint32_t>(std::popcount(fingerprint_randomized_mask));
}

// static
std::string PrivacyIntelligenceService::SiteKeyFor(const GURL& page_url) {
  if (!page_url.is_valid() || !page_url.SchemeIsHTTPOrHTTPS()) {
    return std::string();
  }
  // §9.7: a host with no registrable domain — an IP literal, "localhost", a
  // single-label intranet name — has no site to attribute anything to. Folding
  // it to its last two labels would attribute the page's behaviour to whatever
  // those happen to look like, which is the invention §4.2 forbids.
  if (!HostCanHaveOwner(page_url.host())) {
    return std::string();
  }
  return net::registry_controlled_domains::GetDomainAndRegistry(
      page_url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

void PrivacyIntelligenceService::AppendTimelineEvent(
    const std::string& site_key,
    EventType type,
    TrackerStatus status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!db_) {
    return;
  }
  db_.AsyncCall(base::IgnoreResult(&PrivacyDatabase::AddTimelineEventForSite))
      .WithArgs(base::Time::Now(), site_key, type, status);
}

void PrivacyIntelligenceService::RecordFingerprintSurface(
    const GURL& page_url,
    FingerprintSurface surface,
    bool randomized) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string site_key = SiteKeyFor(page_url);
  if (site_key.empty()) {
    return;
  }

  const uint32_t bit = 1u << static_cast<uint32_t>(surface);
  auto it = page_signals_.find(site_key);
  if (it == page_signals_.end()) {
    if (page_signals_.size() >= kMaxTrackedSites) {
      return;
    }
    it = page_signals_.emplace(site_key, PageSignals{}).first;
  }
  if (it->second.fingerprint_surface_mask & bit) {
    // Already seen on this site this session. Dropping the repeat is what
    // bounds a hostile renderer to one timeline row per surface per site.
    //
    // The randomized bit is still OR-ed in first: the same surface can be
    // touched before and after a seed becomes available, and the honest answer
    // to "was this perturbed on this site" is yes if it ever was.
    if (randomized) {
      it->second.fingerprint_randomized_mask |= bit;
    }
    return;
  }
  if (it->second.first_surface_at.is_null()) {
    it->second.first_surface_at = base::TimeTicks::Now();
  }
  it->second.fingerprint_surface_mask |= bit;
  if (randomized) {
    it->second.fingerprint_randomized_mask |= bit;
  }

  // §6.5's attempt heuristic decides the status. Before this, every surface
  // touch was recorded as kDetected — which accused a charting library that
  // read one canvas of fingerprinting, exactly the false positive §16 budgets
  // zero of.
  //
  AttemptInputs inputs;
  inputs.distinct_surfaces =
      static_cast<uint32_t>(std::popcount(it->second.fingerprint_surface_mask));
  inputs.elapsed = base::TimeTicks::Now() - it->second.first_surface_at;
  TrackerStatus status = ClassifyFingerprintAttempt(inputs);

  // kRandomized means precisely "an API returned perturbed data instead of true
  // values", so it may be claimed only when the browser actually did that — and
  // `randomized` is the browser's own answer, not the renderer's.
  //
  // kPotential is deliberately NOT upgraded. §2.1 defines it as a heuristic
  // match that is not certain and excludes it from headline numbers; turning an
  // uncertain match into a confident protection claim would smuggle it back in
  // through the reassuring door. Uncertainty about WHAT happened outranks
  // certainty about what we did to it.
  if (randomized && status == TrackerStatus::kDetected) {
    status = TrackerStatus::kRandomized;
  }
  AppendTimelineEvent(site_key, EventType::kFingerprintAttempt, status);
}

void PrivacyIntelligenceService::RecordWebrtcAddressRequest(
    const GURL& page_url,
    bool local_addresses_withheld) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string site_key = SiteKeyFor(page_url);
  if (site_key.empty()) {
    return;
  }

  auto it = page_signals_.find(site_key);
  if (it == page_signals_.end()) {
    if (page_signals_.size() >= kMaxTrackedSites) {
      return;
    }
    it = page_signals_.emplace(site_key, PageSignals{}).first;
  }
  // Saturating: the count is displayed, and a page opening billions of peer
  // connections should read as "a lot", not wrap to zero.
  if (it->second.webrtc_address_requests < UINT32_MAX) {
    ++it->second.webrtc_address_requests;
  }
  // AND, not assignment: the reassuring string may only be shown if EVERY
  // request on this site ran under a policy that withheld local addresses.
  it->second.local_addresses_withheld &= local_addresses_withheld;

  AppendTimelineEvent(site_key, EventType::kWebrtcAddressRequest,
                      TrackerStatus::kDetected);
}

std::vector<CrossSiteEntity> PrivacyEventConsumer::GroupCrossSiteRows(
    std::vector<PrivacyDatabase::CrossSiteRow> rows) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // §9.5 needs to know who owns each VISITED site, so resolve the site names
  // themselves against the dataset. Memoised: a session's rows repeat the same
  // handful of sites many times over.
  std::map<std::string, uint16_t> site_owner;
  // Hoisted out of the lambda: the thread-safety analyser cannot see that the
  // sequence check above covers a guarded member read inside a closure.
  EntityResolver* const resolver = resolver_.get();
  auto owner_of = [&site_owner, resolver](const std::string& etld1) {
    const auto it = site_owner.find(etld1);
    if (it != site_owner.end()) {
      return it->second;
    }
    const EntityInfo info = LookupEntityWithFallback(
        resolver, base::PersistentHash(CanonicalHostForHash(etld1)), etld1);
    site_owner.emplace(etld1, info.entity_id);
    return info.entity_id;
  };

  std::map<uint16_t, CrossSiteEntity> by_entity;
  for (const PrivacyDatabase::CrossSiteRow& row : rows) {
    if (row.entity_id == kNoEntity || row.site_etld1.empty()) {
      continue;
    }
    CrossSiteEntity& entity = by_entity[row.entity_id];
    entity.entity_id = row.entity_id;
    if (entity.entity_name.empty() && resolver) {
      entity.entity_name = std::string(resolver->GetEntityName(row.entity_id));
    }
    CrossSiteSite site;
    site.etld1 = row.site_etld1;
    // The §9.5 test: is this visited site owned by the very entity we are
    // about to accuse of following the user across it?
    site.owned_by_entity = owner_of(row.site_etld1) == row.entity_id;
    site.detected = row.detected;
    site.blocked = row.blocked;
    site.allowed = row.allowed;
    entity.sites.push_back(std::move(site));
  }

  std::vector<CrossSiteEntity> candidates;
  candidates.reserve(by_entity.size());
  for (auto& [id, entity] : by_entity) {
    candidates.push_back(std::move(entity));
  }
  return candidates;
}

std::vector<CrossSiteEntity> PrivacyEventConsumer::BuildCrossSite(
    std::vector<PrivacyDatabase::CrossSiteRow> rows) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // The threshold, the ownership exclusion and the ordering all live in one
  // tested place; the grouping above only gathers the inputs for it.
  return SelectCrossSiteEntities(GroupCrossSiteRows(std::move(rows)));
}

std::vector<ExposureEntity> PrivacyEventConsumer::BuildExposure(
    std::vector<PrivacyDatabase::CrossSiteRow> rows) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // BuildCrossSite groups, names and marks ownership; SelectCrossSiteEntities
  // is what applies the >=3 rule, and it is deliberately not used here.
  return SelectExposure(GroupCrossSiteRows(std::move(rows)));
}

std::vector<PrivacyIntelligenceServiceTimelineItem>
PrivacyEventConsumer::NameTimeline(
    std::vector<PrivacyDatabase::TimelineEntry> entries) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  EntityResolver* const resolver = resolver_.get();
  std::vector<PrivacyIntelligenceServiceTimelineItem> out;
  out.reserve(entries.size());
  for (PrivacyDatabase::TimelineEntry& e : entries) {
    PrivacyIntelligenceServiceTimelineItem item;
    item.when = e.when;
    item.site_etld1 = std::move(e.site_etld1);
    // Left empty when unattributed. §4.2: the UI shows the bare site rather
    // than a guessed owner.
    if (e.entity_id != kNoEntity && resolver) {
      item.entity_name = std::string(resolver->GetEntityName(e.entity_id));
    }
    item.event_type = e.event_type;
    item.status = e.status;
    out.push_back(std::move(item));
  }
  return out;
}

void PrivacyIntelligenceService::GetTimeline(int limit,
                                             TimelineCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!db_ || !consumer_ || limit <= 0) {
    std::move(callback).Run({});
    return;
  }
  db_.AsyncCall(&PrivacyDatabase::GetTimeline)
      .WithArgs(limit)
      .Then(base::BindOnce(&PrivacyIntelligenceService::OnTimelineRows,
                           weak_factory_.GetWeakPtr(), std::move(callback)));
}

void PrivacyIntelligenceService::OnTimelineRows(
    TimelineCallback callback,
    std::vector<PrivacyDatabase::TimelineEntry> entries) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (entries.empty() || !consumer_) {
    std::move(callback).Run({});
    return;
  }
  consumer_.AsyncCall(&PrivacyEventConsumer::NameTimeline)
      .WithArgs(std::move(entries))
      .Then(std::move(callback));
}

void PrivacyIntelligenceService::GetCrossSiteEntities(
    CrossSiteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!db_ || !consumer_) {
    std::move(callback).Run({});
    return;
  }
  // §6.4's "within the retention window". Reading further back than retention
  // would report sites the rest of the feature has already forgotten.
  const RetentionConfig config = ClampToHistoryRetention(RetentionConfig());
  const int64_t since_day = UtcDayNow() - config.daily_days;
  db_.AsyncCall(&PrivacyDatabase::GetCrossSiteRows)
      .WithArgs(since_day)
      .Then(base::BindOnce(&PrivacyIntelligenceService::OnCrossSiteRows,
                           weak_factory_.GetWeakPtr(), std::move(callback)));
}

void PrivacyIntelligenceService::GetExposureToday(
    ExposureCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!db_ || !consumer_) {
    std::move(callback).Run({});
    return;
  }
  db_.AsyncCall(&PrivacyDatabase::GetCrossSiteRows)
      .WithArgs(UtcDayNow())  // Today only.
      .Then(base::BindOnce(&PrivacyIntelligenceService::OnExposureRows,
                           weak_factory_.GetWeakPtr(), std::move(callback)));
}

void PrivacyIntelligenceService::OnExposureRows(
    ExposureCallback callback,
    std::vector<PrivacyDatabase::CrossSiteRow> rows) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (rows.empty() || !consumer_) {
    std::move(callback).Run({});
    return;
  }
  // Reuses BuildCrossSite for grouping and naming, then applies §6.7's own
  // rules instead of §6.4's — no threshold, no ownership suppression.
  consumer_.AsyncCall(&PrivacyEventConsumer::BuildExposure)
      .WithArgs(std::move(rows))
      .Then(std::move(callback));
}

void PrivacyIntelligenceService::OnCrossSiteRows(
    CrossSiteCallback callback,
    std::vector<PrivacyDatabase::CrossSiteRow> rows) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (rows.empty() || !consumer_) {
    std::move(callback).Run({});
    return;
  }
  consumer_.AsyncCall(&PrivacyEventConsumer::BuildCrossSite)
      .WithArgs(std::move(rows))
      .Then(std::move(callback));
}

void PrivacyIntelligenceService::RecordUserAllowedSite(const GURL& page_url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string site_key = SiteKeyFor(page_url);
  if (site_key.empty()) {
    return;
  }
  // kDetected: the event records that the user made a choice, and no claim is
  // being made about a tracker. There is no §2.1 status for "the user turned us
  // off", and inventing one would put a non-status into the same column every
  // headline number is computed from.
  AppendTimelineEvent(site_key, EventType::kUserAllowedSite,
                      TrackerStatus::kDetected);
}

void PrivacyIntelligenceService::AnalyzeCurrentPage(
    const GURL& page_url,
    std::vector<PrivacyTabHelper::DomainRow> rows,
    PrivacyTabHelper::PageTotals totals,
    PageAnalysisCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const PageSignals signals = GetPageSignals(page_url);

  std::vector<std::string> domains;
  domains.reserve(rows.size());
  for (const PrivacyTabHelper::DomainRow& row : rows) {
    domains.push_back(row.domain);
  }

  if (!consumer_) {
    // No pipeline, so no attribution. The counts are still real and still
    // worth showing — every row simply renders as its bare domain, which §4.2
    // already requires for anything the dataset does not cover.
    OnDomainsResolved(std::move(rows), totals, signals, std::move(callback),
                      {});
    return;
  }
  consumer_.AsyncCall(&PrivacyEventConsumer::ResolveDomains)
      .WithArgs(std::move(domains))
      .Then(base::BindOnce(&PrivacyIntelligenceService::OnDomainsResolved,
                           weak_factory_.GetWeakPtr(), std::move(rows), totals,
                           signals, std::move(callback)));
}

void PrivacyIntelligenceService::OnDomainsResolved(
    std::vector<PrivacyTabHelper::DomainRow> rows,
    PrivacyTabHelper::PageTotals totals,
    PageSignals signals,
    PageAnalysisCallback callback,
    std::vector<PrivacyEventConsumer::ResolvedDomain> resolved) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  PageAnalysis analysis;
  analysis.detected = totals.detected;
  analysis.blocked = totals.blocked;
  analysis.allowed = totals.allowed;
  analysis.cookies_blocked = totals.cookies_blocked;

  std::set<uint16_t> distinct_entities;
  analysis.rows.reserve(rows.size());
  for (size_t i = 0; i < rows.size(); ++i) {
    const PrivacyTabHelper::DomainRow& in = rows[i];
    TrackerRow row;
    row.domain = in.domain;
    row.requests = in.requests();
    row.blocked = in.blocked;
    // A resolution result may be missing entirely if the pipeline was gone;
    // that is the unknown-domain case, not an error.
    if (i < resolved.size()) {
      row.entity_name = resolved[i].entity_name;
      row.category = resolved[i].category;
      if (resolved[i].entity_id != kNoEntity) {
        distinct_entities.insert(resolved[i].entity_id);
      }
    }
    // kBlocked only when nothing got through. A row with 6 blocked and 2 not
    // is not a blocked row, and labelling it one is the §2 violation this
    // whole feature exists to avoid.
    if (in.blocked > 0 && in.blocked == in.requests()) {
      row.status = TrackerStatus::kBlocked;
    } else if (in.allowed > 0) {
      row.status = TrackerStatus::kAllowed;
    } else {
      row.status = TrackerStatus::kDetected;
    }
    analysis.rows.push_back(std::move(row));
  }

  // §6.2: blocked first, then by request count descending. Domain breaks ties
  // so that reopening the popup on an unchanged page yields the same order —
  // rows shuffling between opens reads as data changing when it has not.
  std::sort(analysis.rows.begin(), analysis.rows.end(),
            [](const TrackerRow& a, const TrackerRow& b) {
              const bool a_blocked = a.status == TrackerStatus::kBlocked;
              const bool b_blocked = b.status == TrackerStatus::kBlocked;
              if (a_blocked != b_blocked) {
                return a_blocked;
              }
              if (a.requests != b.requests) {
                return a.requests > b.requests;
              }
              return a.domain < b.domain;
            });

  analysis.protection =
      ComputeProtectionApplied(totals.blocked, totals.detected + totals.allowed);

  IntensityInputs inputs;
  inputs.distinct_entities = static_cast<uint32_t>(distinct_entities.size());
  inputs.distinct_third_party_domains = totals.distinct_domains;
  // §6.5 counts distinct surfaces touched, not calls made; the WebRTC address
  // request is one more surface on top (§9.2.1).
  inputs.fingerprinting_attempts =
      signals.fingerprint_surface_count() +
      (signals.webrtc_address_requests > 0 ? 1u : 0u);
  // Third-party cookie accesses seen on this page (§6.3's fourth input).
  inputs.tracking_cookies_attempted = totals.cookies_attempted;
  analysis.intensity = ComputeTrackingIntensity(inputs);

  std::move(callback).Run(std::move(analysis));
}

PrivacyIntelligenceService::PageSignals
PrivacyIntelligenceService::GetPageSignals(const GURL& page_url) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string site_key = SiteKeyFor(page_url);
  if (site_key.empty()) {
    return PageSignals();
  }
  const auto it = page_signals_.find(site_key);
  return it == page_signals_.end() ? PageSignals() : it->second;
}

void PrivacyIntelligenceService::DeleteRange(base::Time begin,
                                             base::Time end) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++clear_generation_;
  if (end >= session_start_) {
    // The range overlaps this session, so the popup's live counts describe time
    // the user just erased. They are not time-sliceable — the aggregates carry
    // a day, not a timestamp — so the whole session resets. Over-clearing here
    // is the safe direction: it under-reports a number, rather than continuing
    // to display browsing the user asked us to forget.
    strings_->Clear();
    // Same reasoning: these carry no timestamp, so a range overlapping the
    // session cannot be subtracted from them and the whole set resets.
    page_signals_.clear();
    consumer_.AsyncCall(&PrivacyEventConsumer::ClearInMemory);
  }
  if (db_) {
    db_.AsyncCall(base::IgnoreResult(&PrivacyDatabase::DeleteRange))
        .WithArgs(begin, end);
  }
}

PrivacyIntelligenceService::~PrivacyIntelligenceService() = default;

void PrivacyIntelligenceService::SetDrainInterval(base::TimeDelta interval) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (drain_timer_.IsRunning() && drain_interval_ == interval) {
    return;
  }
  drain_interval_ = interval;
  drain_timer_.Start(
      FROM_HERE, interval,
      base::BindRepeating(&PrivacyIntelligenceService::OnDrainTimer,
                          base::Unretained(this)));
}

void PrivacyIntelligenceService::OnDrainTimer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shut_down_) {
    return;
  }
  drain_issued_at_ = base::TimeTicks::Now();
  // WeakPtr, not Unretained: the reply arrives from another sequence and the
  // service can be destroyed at profile shutdown while it is in flight.
  consumer_.AsyncCall(&PrivacyEventConsumer::DrainNow)
      .Then(base::BindOnce(&PrivacyIntelligenceService::OnDrained,
                           weak_factory_.GetWeakPtr()));
}

void PrivacyIntelligenceService::OnDrained(size_t count) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shut_down_) {
    return;
  }
  // §13.3, checked before the backoff below: a pipeline that is dropping
  // events or answering slowly should not have that hidden by going idle.
  if (!drain_issued_at_.is_null()) {
    EvaluateDrainLatency(base::TimeTicks::Now() - drain_issued_at_);
  }
  EvaluateRingOverflow();
  if (kill_switch_ != KillSwitchReason::kNone) {
    return;
  }
  if (count > 0) {
    consecutive_empty_drains_ = 0;
    SetDrainInterval(kActiveDrainInterval);
    return;
  }
  if (++consecutive_empty_drains_ >= kEmptyDrainsBeforeIdle) {
    SetDrainInterval(kIdleDrainInterval);
  }
}

void PrivacyIntelligenceService::EvaluateRingOverflow() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const uint64_t total = sink_->dropped_count();
  // Unsigned subtraction against a monotonic counter; the sink never resets it.
  const uint64_t since_last = total - last_dropped_sample_;
  last_dropped_sample_ = total;

  if (since_last < kOverflowDropsPerDrain) {
    consecutive_overflow_drains_ = 0;
    return;
  }
  if (++consecutive_overflow_drains_ >= kOverflowDrainsBeforeTrip) {
    TripKillSwitch(KillSwitchReason::kRingOverflow);
  }
}

void PrivacyIntelligenceService::EvaluateDrainLatency(
    base::TimeDelta round_trip) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (round_trip < kSlowDrainRoundTrip) {
    consecutive_slow_drains_ = 0;
    return;
  }
  if (++consecutive_slow_drains_ >= kSlowDrainsBeforeTrip) {
    TripKillSwitch(KillSwitchReason::kFlushLatency);
  }
}

void PrivacyIntelligenceService::OnFlushResult(bool ok) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shut_down_) {
    return;
  }
  if (ok) {
    consecutive_db_failures_ = 0;
    return;
  }
  if (++consecutive_db_failures_ >= kDbFailuresBeforeTrip) {
    TripKillSwitch(KillSwitchReason::kDatabaseFailures);
  }
}

void PrivacyIntelligenceService::TripKillSwitch(KillSwitchReason reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (kill_switch_ != KillSwitchReason::kNone) {
    return;  // First reason wins; it is the one that describes the failure.
  }
  kill_switch_ = reason;

  // Producer first. Until this lands, the network thread is still paying to
  // push events that nothing will ever drain.
  sink_->DisableForSession();

  // Then stop our own timers, so an off pipeline is genuinely idle rather than
  // merely quiet.
  drain_timer_.Stop();
  flush_timer_.Stop();

  // Loud, once. §13.3's whole argument is that turning off silently is the
  // failure mode to avoid; the reason is also in chrome://privacy-internals via
  // GetStats(), which is where a user or a bug report can actually find it.
  LOG(WARNING) << "Zephyrus privacy collection disabled for this session ("
               << "§13.3 kill switch, reason="
               << static_cast<int>(reason) << ")";
}

PipelineStats PrivacyIntelligenceService::GetStats() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  PipelineStats stats;
  stats.mode = GetMode();
  stats.has_dataset = has_dataset_;
  stats.dataset_id = dataset_id_;
  stats.dataset_entries = dataset_entries_;
  stats.dataset_built_unix_seconds = dataset_built_;
  stats.dataset_version = dataset_version_;
  stats.dataset_source = dataset_source_;
  stats.dataset_licence = dataset_licence_;
  stats.dataset_freshness = dataset_freshness_;
  // Same helper the resolver uses, so the number on the internals page and the
  // bucket the UI wording keys off can never disagree.
  stats.dataset_age_days =
      DatasetAgeDaysFrom(dataset_published_, base::Time::Now());
  stats.persistence_ready = persistence_ready_;
  stats.kill_switch = kill_switch_;
  stats.persistence_refused = persistence_refused_;
  stats.rows_flushed = rows_flushed_;
  stats.known_domain_strings = strings_ ? strings_->size() : 0;
  if (sink_) {
    stats.events_recorded = sink_->recorded_count();
    stats.events_dropped = sink_->dropped_count();
    stats.peak_ring_depth = sink_->peak_depth();
    stats.current_ring_depth = sink_->ApproximateDepth();
  }
  // events_drained lives on the privacy sequence and is deliberately not
  // fetched synchronously here: blocking the UI thread on another sequence to
  // populate a diagnostic counter is exactly the trade §8.9 forbids. The
  // internals page asks for it asynchronously.
  return stats;
}

void PrivacyIntelligenceService::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  shut_down_ = true;
  drain_timer_.Stop();
  flush_timer_.Stop();
  prune_timer_.Stop();
  // Drop the database first: its sequence is SKIP_ON_SHUTDOWN, so a pending
  // flush is abandoned rather than blocking teardown. Up to one flush interval
  // of statistics can be lost, which §10 already documents as acceptable.
  db_.Reset();

  // Drop the consumer, which destroys it on its own sequence. The sink stays
  // alive as long as the network side holds a reference; events pushed after
  // this simply go into a ring nobody drains. Dropping late events is correct;
  // tearing down a buffer the network thread is still writing to is not.
  consumer_.Reset();
}

}  // namespace zephyrus_privacy
