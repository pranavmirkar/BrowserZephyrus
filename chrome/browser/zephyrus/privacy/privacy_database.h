// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_DATABASE_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_DATABASE_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "chrome/browser/zephyrus/privacy/privacy_crypto.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "sql/database.h"
#include "sql/meta_table.h"

namespace zephyrus_privacy {

// One aggregated row: a (day, site, request domain) triple and its counts.
// NOT one row per request — a content-heavy page fires 200-400 requests and
// per-request rows would grow without bound and jank the browser (§5.1).
struct DailyRow {
  int64_t day = 0;  // UTC days since epoch.
  // The site's eTLD+1 as TEXT, not the emission-side hash. The hash is public
  // knowledge (the domain space is enumerable), so storing it would hand an
  // attacker with the file a precomputed-table attack on exactly the column the
  // encryption exists to protect — the same argument that put an HMAC on the
  // domain column (§5.3). FlushDaily routes this through InternSite, which
  // writes the keyed hash plus the encrypted name and hands back the real
  // site_id. Reserved names that no eTLD+1 can collide with: "(no site)" for
  // requests with no top-level document, "(unknown site)" when the name was
  // dropped at the string table's cap.
  std::string site_etld1;
  std::string request_domain;
  uint16_t entity_id = kNoEntity;
  Category category = Category::kUnknown;
  uint32_t detected = 0;
  uint32_t blocked = 0;
  uint32_t allowed = 0;
  uint32_t randomized = 0;
};

// Per-table retention. Pranav's amendment to §5.2, 2026-08-11: driven by how
// fast the data stops being useful and how sensitive it is, NOT by size — 30
// days of aggregates is under 2 MB against a 50 MB cap, so size decides
// nothing here.
struct RetentionConfig {
  // Aggregated counts. Cross-site detection only needs 3 sites inside the
  // window, so a week is plenty.
  int daily_days = 7;
  // A timestamped narrative of where you went and when: the highest
  // sensitivity per byte in the feature, and nobody reviews a privacy timeline
  // from three weeks ago. 0 disables the timeline entirely.
  int timeline_hours = 48;

  // §5.2.2: privacy retention must NEVER exceed the browser's own history
  // retention — a privacy feature outliving the history it describes is
  // indefensible. Prune() takes the minimum of this and the values above.
  //
  // Chromium's history retention is a fixed 90 days
  // (history::HistoryBackend::kExpireDaysThreshold); it is not user
  // configurable, so this is a constant today rather than a pref read. Kept as
  // a field, not a hardcoded constant, so a future configurable history
  // retention is a caller change and the clamp is testable.
  int history_retention_days = 90;
};

// Applies the §5.2.2 coupling. Pure and free-standing so it can be tested
// without a database.
RetentionConfig ClampToHistoryRetention(RetentionConfig config);

struct DatabaseStats {
  int64_t daily_rows = 0;
  int64_t timeline_rows = 0;
  int64_t site_rows = 0;
  int64_t file_size_bytes = 0;
  uint64_t lifetime_detected = 0;
  uint64_t lifetime_blocked = 0;
  uint64_t lifetime_randomized = 0;
};

// The cold path (§8.2). Every method blocks and must run on a ThreadPool
// sequence with MayBlock + BEST_EFFORT. Nothing here may be called from the UI
// thread or the privacy sequence.
class PrivacyDatabase {
 public:
  // `crypto` must not be null: see PrivacyCrypto. The file is opened LAZILY on
  // first write, so a session with no browsing never touches the disk (§8.8).
  PrivacyDatabase(base::FilePath db_path,
                  std::unique_ptr<PrivacyCrypto> crypto);
  PrivacyDatabase(const PrivacyDatabase&) = delete;
  PrivacyDatabase& operator=(const PrivacyDatabase&) = delete;
  ~PrivacyDatabase();

  // Opens and migrates if needed. Safe to call repeatedly. Returns false if
  // the database is unusable, in which case every other method is a no-op and
  // the caller should run memory-only for the session (§10).
  bool EnsureOpen();

  // Stable across restarts, which is what lets in-memory aggregates keep using
  // integers (§8.6). Returns nullopt if the database is unusable.
  std::optional<int64_t> InternSite(std::string_view etld1);

  // One transaction for the whole batch (§8.8). Counts are ADDED to any
  // existing row for the same (day, site, domain).
  bool FlushDaily(base::span<const DailyRow> rows);

  // By-value overload for base::SequenceBound, which cannot forward a span to
  // another sequence — the pointed-to storage would not survive the hop.
  bool FlushDailyRows(std::vector<DailyRow> rows);

  // One (entity, site) pair the user actually encountered, for §6.4.
  struct CrossSiteRow {
    uint16_t entity_id = kNoEntity;
    // DECRYPTED. This is the first read path in the feature that opens what it
    // stored — everything before it only ever wrote. See GetCrossSiteRows.
    std::string site_etld1;
    uint32_t detected = 0;
    uint32_t blocked = 0;
    uint32_t allowed = 0;
  };

  // Every attributed (entity, site) pair since `since_day`, summed.
  //
  // **The first production read that decrypts.** PrivacyCrypto::Decrypt existed
  // and was unit-tested from the start, but nothing had ever called it: the
  // database was write-only in production, so a site name went in and could
  // never come out. §6.4 is what needs it, because a cross-site claim has to
  // name the sites.
  //
  // Rows whose site name cannot be decrypted are DROPPED, not substituted. A
  // key rotation leaves ciphertext that will never open again (see
  // key_was_rotated), and naming a site "(unknown)" in a sentence accusing a
  // company of tracking the user across it would be worse than saying nothing.
  std::vector<CrossSiteRow> GetCrossSiteRows(int64_t since_day);

  // One entry of §6.8's timeline.
  struct TimelineEntry {
    base::Time when;
    std::string site_etld1;  // Decrypted; see GetCrossSiteRows.
    uint16_t entity_id = kNoEntity;
    EventType event_type = EventType::kFirstSeenOnSite;
    TrackerStatus status = TrackerStatus::kDetected;
  };

  // The most recent `limit` events, newest first.
  //
  // §8.9: "Paginate the timeline. Never SELECT * on the event table." The table
  // is a 5,000-row ring, and a UI that asks for all of it will get all of it —
  // so the bound is a required argument rather than an optional convenience.
  std::vector<TimelineEntry> GetTimeline(int limit);

  bool AddTimelineEvent(base::Time when,
                        int64_t site_id,
                        uint16_t entity_id,
                        EventType event_type,
                        TrackerStatus status);

  // Interns `site_etld1` and appends a timeline event for it, in one hop.
  //
  // Exists because the callers that produce these events (§9.2.1 WebRTC, §6.5
  // fingerprinting) live on the UI thread and hold a URL, not a site_id. Doing
  // it as two SequenceBound calls would mean a round trip to the database
  // sequence and back just to learn an integer, and would leave a window in
  // which a clear could land between the intern and the insert.
  //
  // Takes the string by value: base::SequenceBound cannot forward a view whose
  // storage would not survive the hop.
  bool AddTimelineEventForSite(base::Time when,
                               std::string site_etld1,
                               EventType event_type,
                               TrackerStatus status);

  // The "since March" headline. Never pruned, and structurally incapable of
  // holding anything identifying — see the schema comment.
  // No `allowed`: §5.2.1 defines the lifetime row as detected/blocked/
  // randomized only.
  bool AddLifetime(uint64_t detected, uint64_t blocked, uint64_t randomized);

  // Applies `config` and enforces the size cap. Idle-time work (§8.8).
  bool Prune(const RetentionConfig& config);

  // Clear Browsing Data, whole history. Empties EVERYTHING including the
  // lifetime counters (§5.2.1).
  bool DeleteAllBrowsingData();

  // Clear Browsing Data for a time range. The spec only covered a full clear;
  // a time-ranged clear that left privacy rows behind would be a real leak
  // (Pranav's amendment).
  bool DeleteRange(base::Time begin, base::Time end);

  DatabaseStats GetStats();

  // Test seam: lets a test point the pruner at a fixed "today" so day-boundary
  // behaviour is deterministic.
  void SetClockForTesting(base::Time now) { fake_now_for_testing_ = now; }

  // Test seam for the §10 disk-full row. Caps the file at `pages` so SQLite
  // returns a real SQLITE_FULL, rather than the test mocking a return value and
  // proving only that the mock works. 0 restores the default.
  bool SetMaxPageCountForTesting(int pages);

 private:
  bool InitSchema();
  bool MigrateIfNeeded();
  base::Time Now() const;
  // Newest stored day, not "now": a clock moved backwards must not wipe the
  // database, and must not stop pruning either (§9.6).
  std::optional<int64_t> NewestStoredDay();

  SEQUENCE_CHECKER(sequence_checker_);

  const base::FilePath db_path_;
  std::unique_ptr<PrivacyCrypto> crypto_;
  sql::Database db_;
  sql::MetaTable meta_table_;
  bool open_ = false;
  bool poisoned_ = false;  // Unrecoverable; stop trying for this session.
  std::optional<base::Time> fake_now_for_testing_;
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_DATABASE_H_
