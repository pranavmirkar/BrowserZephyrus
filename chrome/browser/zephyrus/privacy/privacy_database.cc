// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_database.h"

#include <algorithm>
#include <utility>

#include "base/files/file_util.h"
#include "base/hash/hash.h"
#include "base/strings/cstring_view.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace zephyrus_privacy {

namespace {

// Reserved site names. Parentheses and spaces cannot appear in an eTLD+1, so
// neither can be forged by a real site.
constexpr char kUnknownSiteName[] = "(unknown site)";

// Bumped only for a real schema change, with a migration step per bump (§5.4).
// v2 replaced the unkeyed lookup hashes with keyed HMACs (§5.3) and aligned
// lifetime_counters with §5.2.1. Nothing has shipped, so v1 files are razed
// rather than migrated — the existing version-mismatch path handles it.
constexpr int kCurrentVersion = 2;
constexpr int kCompatibleVersion = 2;

// §5.2. Oldest days are dropped until the file is back under the cap.
constexpr int64_t kMaxDatabaseBytes = 50 * 1024 * 1024;

// §5.2's "Timeline rows | 5,000 | Ring buffer, oldest dropped".
constexpr int64_t kMaxTimelineRows = 5000;

// ~800 KB of page cache (§8.8).
constexpr int kPageSize = 4096;
constexpr int kCacheSizePages = 200;

int64_t ToUtcDay(base::Time time) {
  // UTC days, so a timezone change or DST cannot duplicate or corrupt a day's
  // aggregates (§9.6).
  return time.InMillisecondsSinceUnixEpoch() / base::Time::kMillisecondsPerDay;
}

}  // namespace

RetentionConfig ClampToHistoryRetention(RetentionConfig config) {
  const int history_days = std::max(0, config.history_retention_days);
  if (config.daily_days > history_days) {
    config.daily_days = history_days;
  }
  // Compared in hours so a short history retention also caps the timeline.
  // Guard the multiply: a caller passing a huge history_retention_days must
  // not overflow into a negative cap.
  const int64_t history_hours = int64_t{history_days} * 24;
  if (config.timeline_hours > history_hours) {
    config.timeline_hours = static_cast<int>(history_hours);
  }
  return config;
}

PrivacyDatabase::PrivacyDatabase(base::FilePath db_path,
                                 std::unique_ptr<PrivacyCrypto> crypto)
    : db_path_(std::move(db_path)),
      crypto_(std::move(crypto)),
      db_(sql::DatabaseOptions()
              .set_wal_mode(true)
              // Durability of privacy statistics does not justify an fsync per
              // write; at worst a crash loses the last flush (§8.8, §10).
              .set_flush_to_media(false)
              .set_page_size(kPageSize)
              .set_cache_size(kCacheSizePages),
          sql::Database::Tag("ZephyrusPrivacy")) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

PrivacyDatabase::~PrivacyDatabase() = default;

base::Time PrivacyDatabase::Now() const {
  return fake_now_for_testing_.value_or(base::Time::Now());
}

bool PrivacyDatabase::EnsureOpen() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (open_) {
    return true;
  }
  if (poisoned_) {
    return false;
  }
  // No encryptor means no writing. Plaintext browsing history on disk is not
  // an acceptable degradation (§10).
  if (!crypto_) {
    poisoned_ = true;
    LOG(ERROR) << "Zephyrus privacy: no encryptor; refusing to open the "
                  "database rather than storing domains in plaintext";
    return false;
  }

  if (!db_.Open(db_path_) || !InitSchema() || !MigrateIfNeeded()) {
    // Corrupt, or written by a newer build. Privacy statistics are expendable;
    // a bad read is not (§5.4, §10).
    //
    // Raze() alone is not enough and was the bug here: it works through SQLite,
    // so a file SQLite can no longer parse cannot be razed, and the recovery
    // failed exactly in the case it existed for. Try it first because it is the
    // cheap path that preserves the open handle, then fall back to deleting the
    // file outright — Database::Delete also removes the -wal and -shm, which a
    // half-written WAL makes necessary.
    if (!db_.Raze()) {
      db_.Close();
      meta_table_.Reset();
      if (!sql::Database::Delete(db_path_)) {
        poisoned_ = true;
        LOG(ERROR) << "Zephyrus privacy: cannot remove an unusable database; "
                      "running in-memory only for this session";
        return false;
      }
    }
    db_.Close();
    meta_table_.Reset();
    if (!db_.Open(db_path_) || !InitSchema()) {
      poisoned_ = true;
      return false;
    }
  }
  // A rotated lookup key makes every stored row unreadable: its HMACs were
  // computed with the old key and can never match again, and its ciphertext was
  // sealed by a keystore this profile no longer has. Keeping them would grow a
  // second parallel set of rows and split every count across the two.
  //
  // This is what the rotation comment in privacy_crypto_impl.cc used to claim
  // the schema-version path did. It does not: rotation does not change the
  // version, so nothing razed and the stale rows simply accumulated.
  if (crypto_->key_was_rotated()) {
    LOG(ERROR) << "Zephyrus privacy: lookup key rotated; discarding rows the "
                  "new key cannot read";
    // Reset() before re-initialising: sql::MetaTable DCHECKs if Init() is
    // called on an instance that already holds a database, and Raze() does not
    // clear that state. The corruption path above gets this right; this one
    // did not, and out/Release is an official build with DCHECKs compiled out,
    // so the test passed there while double-initialising the meta table.
    meta_table_.Reset();
    if (!db_.Raze() || !InitSchema()) {
      poisoned_ = true;
      return false;
    }
  }

  open_ = true;
  return true;
}

bool PrivacyDatabase::SetMaxPageCountForTesting(int pages) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureOpen()) {
    return false;
  }
  // 1073741823 is SQLite's own default ceiling, so 0 means "no cap".
  // A PRAGMA takes no bound parameters, so the value is formatted in. Safe
  // here: `pages` is an int from test code, never anything user-supplied.
  // 1073741823 is SQLite's own default ceiling, so 0 means "no cap".
  const std::string sql = base::StringPrintf(
      "PRAGMA max_page_count=%d", pages > 0 ? pages : 1073741823);
  return db_.Execute(sql);
}

bool PrivacyDatabase::InitSchema() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!meta_table_.Init(&db_, kCurrentVersion, kCompatibleVersion)) {
    return false;
  }

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return false;
  }

  // Domains are stored twice: a hash for keys and lookups, and the encrypted
  // text for display. The hash is necessary because platform keystore output
  // is not deterministic (Windows DPAPI in particular), so ciphertext cannot
  // be a primary key or a UNIQUE column.
  static constexpr char kSiteSql[] =
      "CREATE TABLE IF NOT EXISTS site("
      "site_id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "etld1_hmac INTEGER NOT NULL UNIQUE,"
      "etld1_cipher BLOB NOT NULL)";

  static constexpr char kDailySql[] =
      "CREATE TABLE IF NOT EXISTS tracker_daily("
      "day INTEGER NOT NULL,"
      "site_id INTEGER NOT NULL,"
      "domain_hmac INTEGER NOT NULL,"
      "domain_cipher BLOB NOT NULL,"
      "entity_id INTEGER,"
      "category INTEGER NOT NULL,"
      "detected_count INTEGER NOT NULL DEFAULT 0,"
      "blocked_count INTEGER NOT NULL DEFAULT 0,"
      "allowed_count INTEGER NOT NULL DEFAULT 0,"
      "randomized_count INTEGER NOT NULL DEFAULT 0,"
      "PRIMARY KEY(day,site_id,domain_hmac)) WITHOUT ROWID";

  static constexpr char kTimelineSql[] =
      "CREATE TABLE IF NOT EXISTS timeline_event("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "timestamp_ms INTEGER NOT NULL,"
      "site_id INTEGER NOT NULL,"
      "entity_id INTEGER,"
      "event_type INTEGER NOT NULL,"
      "status INTEGER NOT NULL)";

  // The "1,247,392 blocked since March" headline.
  //
  // It has no site, domain, entity or timestamp column, and the CHECK pins it
  // to a single row. That is deliberate and structural: the table cannot grow
  // a per-site breakdown later without a schema migration that someone has to
  // justify, so "this discloses nothing about where you went" stays true
  // rather than being a comment that rots.
  //
  // It IS cleared by a full Clear Browsing Data (§5.2.1) and spared only by a
  // TIME-RANGED clear (§5.2.2) — a range cannot meaningfully subtract from a
  // running total, and §5.2.2 expects "today reads zero beside a large
  // lifetime figure" after a partial clear.
  static constexpr char kLifetimeSql[] =
      "CREATE TABLE IF NOT EXISTS lifetime_counters("
      "id INTEGER PRIMARY KEY CHECK(id=1),"
      "total_detected INTEGER NOT NULL DEFAULT 0,"
      "total_blocked INTEGER NOT NULL DEFAULT 0,"
      "total_randomized INTEGER NOT NULL DEFAULT 0,"
      "since_ms INTEGER NOT NULL DEFAULT 0)";

  if (!db_.Execute(kSiteSql) || !db_.Execute(kDailySql) ||
      !db_.Execute(kTimelineSql) || !db_.Execute(kLifetimeSql)) {
    LOG(ERROR) << "Zephyrus privacy: schema creation failed: "
               << db_.GetErrorMessage();
    return false;
  }
  if (!db_.Execute("CREATE INDEX IF NOT EXISTS idx_timeline_ts ON "
                   "timeline_event(timestamp_ms)") ||
      !db_.Execute("CREATE INDEX IF NOT EXISTS idx_daily_entity ON "
                   "tracker_daily(entity_id,day)")) {
    return false;
  }
  return transaction.Commit();
}

bool PrivacyDatabase::MigrateIfNeeded() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const int version = meta_table_.GetVersionNumber();
  if (version == kCurrentVersion) {
    return true;
  }
  // A file written by a NEWER build. Do not guess at its shape — raze and
  // recreate. Statistics are expendable; misreading them is not (§5.4).
  if (version > kCurrentVersion) {
    return false;
  }
  // Forward-only migrations land here, one function per version step, each
  // independently tested (§12.5). Version 1 is the first, so there is nothing
  // to migrate from yet.
  return false;
}

std::optional<int64_t> PrivacyDatabase::InternSite(std::string_view etld1) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureOpen() || etld1.empty()) {
    return std::nullopt;
  }
  const std::optional<int64_t> hmac = crypto_->KeyedHash(etld1);
  if (!hmac) {
    return std::nullopt;  // No key: same rule as no encryptor.
  }

  sql::Statement find(db_.GetCachedStatement(
      SQL_FROM_HERE, "SELECT site_id FROM site WHERE etld1_hmac=?"));
  find.BindInt64(0, *hmac);
  if (find.Step()) {
    return find.ColumnInt64(0);
  }

  std::optional<std::vector<uint8_t>> cipher = crypto_->Encrypt(etld1);
  if (!cipher) {
    // Keystore went away mid-session. Drop the write rather than fall back to
    // plaintext (§10).
    return std::nullopt;
  }

  sql::Statement insert(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO site(etld1_hmac,etld1_cipher) VALUES(?,?)"));
  insert.BindInt64(0, *hmac);
  insert.BindBlob(1, *cipher);
  if (!insert.Run()) {
    LOG(ERROR) << "Zephyrus privacy: site insert failed: "
               << db_.GetErrorMessage();
    return std::nullopt;
  }
  return db_.GetLastInsertRowId();
}

bool PrivacyDatabase::FlushDaily(base::span<const DailyRow> rows) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureOpen() || rows.empty()) {
    return EnsureOpen();
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return false;
  }

  // One InternSite round trip per distinct site per flush, not per row. A busy
  // page produces hundreds of rows against a single site.
  std::map<std::string, int64_t> site_ids;
  for (const DailyRow& row : rows) {
    const std::string& site_name =
        row.site_etld1.empty() ? kUnknownSiteName : row.site_etld1;
    int64_t site_id = 0;
    if (auto it = site_ids.find(site_name); it != site_ids.end()) {
      site_id = it->second;
    } else {
      const std::optional<int64_t> interned = InternSite(site_name);
      if (!interned) {
        // No key, or the insert failed. Dropping the row is correct: a row with
        // a bogus site_id would silently misattribute someone's browsing.
        return false;
      }
      site_id = *interned;
      site_ids.emplace(site_name, site_id);
    }

    std::optional<std::vector<uint8_t>> cipher =
        crypto_->Encrypt(row.request_domain);
    if (!cipher) {
      return false;
    }
    const std::optional<int64_t> domain_hmac =
        crypto_->KeyedHash(row.request_domain);
    if (!domain_hmac) {
      return false;
    }

    // UPDATE first, INSERT only if it matched nothing.
    //
    // §8.8 prescribes `INSERT ... ON CONFLICT DO UPDATE`, but Chromium builds
    // SQLite with SQLITE_OMIT_UPSERT (see
    // third_party/sqlite/sqlite_chromium_configuration_flags.gni), so that
    // syntax does not parse here at all. This is the standard Chromium
    // substitute and keeps the spirit of the rule: no read-then-write, and in
    // the steady state exactly one statement, because a day's row already
    // exists after the first flush and the UPDATE hits.
    sql::Statement update(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "UPDATE tracker_daily SET "
        "detected_count=detected_count+?,blocked_count=blocked_count+?,"
        "allowed_count=allowed_count+?,randomized_count=randomized_count+? "
        "WHERE day=? AND site_id=? AND domain_hmac=?"));
    update.BindInt64(0, row.detected);
    update.BindInt64(1, row.blocked);
    update.BindInt64(2, row.allowed);
    update.BindInt64(3, row.randomized);
    update.BindInt64(4, row.day);
    update.BindInt64(5, site_id);
    update.BindInt64(6, *domain_hmac);
    if (!update.Run()) {
      LOG(ERROR) << "Zephyrus privacy: daily update failed: "
                 << db_.GetErrorMessage();
      return false;
    }
    if (db_.GetLastChangeCount() > 0) {
      continue;  // Existing row accumulated; nothing more to do.
    }

    sql::Statement insert(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT INTO tracker_daily(day,site_id,domain_hmac,domain_cipher,"
        "entity_id,category,detected_count,blocked_count,allowed_count,"
        "randomized_count) VALUES(?,?,?,?,?,?,?,?,?,?)"));
    insert.BindInt64(0, row.day);
    insert.BindInt64(1, site_id);
    insert.BindInt64(2, *domain_hmac);
    insert.BindBlob(3, *cipher);
    if (row.entity_id == kNoEntity) {
      insert.BindNull(4);
    } else {
      insert.BindInt64(4, row.entity_id);
    }
    insert.BindInt64(5, static_cast<int64_t>(row.category));
    insert.BindInt64(6, row.detected);
    insert.BindInt64(7, row.blocked);
    insert.BindInt64(8, row.allowed);
    insert.BindInt64(9, row.randomized);
    if (!insert.Run()) {
      LOG(ERROR) << "Zephyrus privacy: daily insert failed: "
                 << db_.GetErrorMessage();
      return false;
    }
  }
  if (!transaction.Commit()) {
    LOG(ERROR) << "Zephyrus privacy: daily flush commit failed: "
               << db_.GetErrorMessage();
    return false;
  }
  return true;
}

bool PrivacyDatabase::FlushDailyRows(std::vector<DailyRow> rows) {
  return FlushDaily(rows);
}

std::vector<PrivacyDatabase::CrossSiteRow> PrivacyDatabase::GetCrossSiteRows(
    int64_t since_day) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<CrossSiteRow> rows;
  if (!EnsureOpen()) {
    return rows;
  }
  // Grouped in SQL rather than in C++: the row count is the product of days,
  // sites and domains, and pulling all of it across to sum it would be the
  // kind of work §8.9 keeps off this path.
  sql::Statement select(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT t.entity_id, s.etld1_cipher, SUM(t.detected_count), "
      "SUM(t.blocked_count), SUM(t.allowed_count) "
      "FROM tracker_daily t JOIN site s ON s.site_id = t.site_id "
      "WHERE t.day >= ? AND t.entity_id IS NOT NULL "
      "GROUP BY t.entity_id, t.site_id"));
  select.BindInt64(0, since_day);

  while (select.Step()) {
    CrossSiteRow row;
    row.entity_id = static_cast<uint16_t>(select.ColumnInt64(0));
    const std::vector<uint8_t> cipher = select.ColumnBlobAsVector(1);
    std::optional<std::string> name = crypto_->Decrypt(cipher);
    if (!name || name->empty()) {
      continue;  // Unreadable after a key rotation; see the header.
    }
    row.site_etld1 = std::move(*name);
    row.detected = static_cast<uint32_t>(select.ColumnInt64(2));
    row.blocked = static_cast<uint32_t>(select.ColumnInt64(3));
    row.allowed = static_cast<uint32_t>(select.ColumnInt64(4));
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<PrivacyDatabase::TimelineEntry> PrivacyDatabase::GetTimeline(
    int limit) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<TimelineEntry> out;
  if (limit <= 0 || !EnsureOpen()) {
    return out;
  }
  // Named columns, ordered by the indexed timestamp, bounded by LIMIT — the
  // three things §8.9 asks for on this table.
  sql::Statement select(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT e.timestamp_ms, s.etld1_cipher, e.entity_id, e.event_type, "
      "e.status FROM timeline_event e JOIN site s ON s.site_id = e.site_id "
      "ORDER BY e.timestamp_ms DESC, e.id DESC LIMIT ?"));
  select.BindInt64(0, limit);

  while (select.Step()) {
    TimelineEntry entry;
    entry.when =
        base::Time::FromMillisecondsSinceUnixEpoch(select.ColumnInt64(0));
    std::optional<std::string> name =
        crypto_->Decrypt(select.ColumnBlobAsVector(1));
    if (!name || name->empty()) {
      continue;  // Unreadable after a key rotation; see GetCrossSiteRows.
    }
    entry.site_etld1 = std::move(*name);
    // NULL entity_id is the common case: most timeline events are about the
    // page, not about a company we resolved.
    entry.entity_id = select.GetColumnType(2) == sql::ColumnType::kNull
                          ? kNoEntity
                          : static_cast<uint16_t>(select.ColumnInt64(2));
    const int64_t type = select.ColumnInt64(3);
    const int64_t status = select.ColumnInt64(4);
    // Values come off disk, so they are untrusted in the same sense a file is:
    // a corrupted or downgraded row must not index past the enum.
    if (type < 0 || type > static_cast<int64_t>(EventType::kMaxValue) ||
        status < 0 ||
        status > static_cast<int64_t>(TrackerStatus::kMaxValue)) {
      continue;
    }
    entry.event_type = static_cast<EventType>(type);
    entry.status = static_cast<TrackerStatus>(status);
    out.push_back(std::move(entry));
  }
  return out;
}

bool PrivacyDatabase::AddTimelineEvent(base::Time when,
                                       int64_t site_id,
                                       uint16_t entity_id,
                                       EventType event_type,
                                       TrackerStatus status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureOpen()) {
    return false;
  }
  sql::Statement insert(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO timeline_event(timestamp_ms,site_id,entity_id,event_type,"
      "status) VALUES(?,?,?,?,?)"));
  insert.BindInt64(0, when.InMillisecondsSinceUnixEpoch());
  insert.BindInt64(1, site_id);
  if (entity_id == kNoEntity) {
    insert.BindNull(2);
  } else {
    insert.BindInt64(2, entity_id);
  }
  insert.BindInt64(3, static_cast<int64_t>(event_type));
  insert.BindInt64(4, static_cast<int64_t>(status));
  return insert.Run();
}

bool PrivacyDatabase::AddTimelineEventForSite(base::Time when,
                                              std::string site_etld1,
                                              EventType event_type,
                                              TrackerStatus status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (site_etld1.empty()) {
    return false;
  }
  const std::optional<int64_t> site_id = InternSite(site_etld1);
  if (!site_id.has_value()) {
    return false;
  }
  // kNoEntity: these events describe something the PAGE did, not a third party
  // we resolved to a company. Attributing a fingerprinting attempt to whichever
  // entity happens to own the site would be an invention of exactly the kind
  // §4.2 forbids.
  return AddTimelineEvent(when, *site_id, kNoEntity, event_type, status);
}

bool PrivacyDatabase::AddLifetime(uint64_t detected,
                                  uint64_t blocked,
                                  uint64_t randomized) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureOpen()) {
    return false;
  }
  // UPDATE-then-INSERT, for the same reason as FlushDaily: Chromium's SQLite
  // is built with SQLITE_OMIT_UPSERT.
  sql::Statement update(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE lifetime_counters SET total_detected=total_detected+?,"
      "total_blocked=total_blocked+?,total_randomized=total_randomized+? "
      "WHERE id=1"));
  update.BindInt64(0, static_cast<int64_t>(detected));
  update.BindInt64(1, static_cast<int64_t>(blocked));
  update.BindInt64(2, static_cast<int64_t>(randomized));
  if (!update.Run()) {
    LOG(ERROR) << "Zephyrus privacy: lifetime update failed: "
               << db_.GetErrorMessage();
    return false;
  }
  if (db_.GetLastChangeCount() > 0) {
    return true;
  }

  // First ever write: stamp when counting began, so the UI can say "since".
  sql::Statement insert(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO lifetime_counters(id,total_detected,total_blocked,"
      "total_randomized,since_ms) VALUES(1,?,?,?,?)"));
  insert.BindInt64(0, static_cast<int64_t>(detected));
  insert.BindInt64(1, static_cast<int64_t>(blocked));
  insert.BindInt64(2, static_cast<int64_t>(randomized));
  insert.BindInt64(3, Now().InMillisecondsSinceUnixEpoch());
  if (!insert.Run()) {
    LOG(ERROR) << "Zephyrus privacy: lifetime insert failed: "
               << db_.GetErrorMessage();
    return false;
  }
  return true;
}

std::optional<int64_t> PrivacyDatabase::NewestStoredDay() {
  sql::Statement query(
      db_.GetCachedStatement(SQL_FROM_HERE, "SELECT MAX(day) FROM tracker_daily"));
  if (query.Step() && query.GetColumnType(0) != sql::ColumnType::kNull) {
    return query.ColumnInt64(0);
  }
  return std::nullopt;
}

bool PrivacyDatabase::Prune(const RetentionConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureOpen()) {
    return false;
  }
  // §5.2.2: never keep privacy data longer than the browser keeps the history
  // it describes.
  const RetentionConfig effective = ClampToHistoryRetention(config);

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return false;
  }

  // Prune relative to the NEWEST STORED DAY, not to "now" alone. A clock moved
  // backwards would otherwise make every stored row look ancient and wipe the
  // database; one moved forwards would stop pruning entirely (§9.6).
  const std::optional<int64_t> newest = NewestStoredDay();
  const int64_t today = ToUtcDay(Now());
  const int64_t reference = newest ? std::max(*newest, today) : today;

  if (effective.daily_days > 0) {
    sql::Statement drop(db_.GetCachedStatement(
        SQL_FROM_HERE, "DELETE FROM tracker_daily WHERE day < ?"));
    drop.BindInt64(0, reference - effective.daily_days + 1);
    if (!drop.Run()) {
      return false;
    }
  }

  // timeline_hours == 0 means the user turned the timeline off: drop all of it.
  const int64_t cutoff_ms =
      effective.timeline_hours > 0
          ? (Now() - base::Hours(effective.timeline_hours))
                .InMillisecondsSinceUnixEpoch()
          : std::numeric_limits<int64_t>::max();
  sql::Statement drop_events(db_.GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM timeline_event WHERE timestamp_ms < ?"));
  drop_events.BindInt64(0, cutoff_ms);
  if (!drop_events.Run()) {
    return false;
  }

  // §5.2's ring: at most kMaxTimelineRows, oldest dropped. Retention by age
  // alone does not bound this — a burst inside the 48-hour window can produce
  // any number of rows, and §5.2 calls the timeline "the highest sensitivity
  // per byte in the whole feature". An unbounded narrative of where the user
  // went is exactly what the cap exists to prevent.
  //
  // Deleting by id rather than timestamp: id is the AUTOINCREMENT insertion
  // order, so two events sharing a millisecond still have a total order and the
  // cap cannot leave kMaxTimelineRows+1 rows behind because it could not choose
  // between them.
  sql::Statement ring(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "DELETE FROM timeline_event WHERE id NOT IN ("
      "SELECT id FROM timeline_event ORDER BY id DESC LIMIT ?)"));
  ring.BindInt64(0, kMaxTimelineRows);
  if (!ring.Run()) {
    return false;
  }

  // Sites nothing refers to any more. Left behind, these are a standing record
  // of everywhere the user has ever been, long after the counts expired.
  if (!db_.Execute("DELETE FROM site WHERE site_id NOT IN "
                   "(SELECT DISTINCT site_id FROM tracker_daily UNION "
                   "SELECT DISTINCT site_id FROM timeline_event)")) {
    return false;
  }
  if (!transaction.Commit()) {
    return false;
  }

  // Size cap: drop whole days, oldest first, until under it.
  for (;;) {
    const std::optional<int64_t> size = base::GetFileSize(db_path_);
    if (!size || *size <= kMaxDatabaseBytes) {
      break;
    }
    const std::optional<int64_t> oldest = [&]() -> std::optional<int64_t> {
      sql::Statement query(db_.GetCachedStatement(
          SQL_FROM_HERE, "SELECT MIN(day) FROM tracker_daily"));
      if (query.Step() && query.GetColumnType(0) != sql::ColumnType::kNull) {
        return query.ColumnInt64(0);
      }
      return std::nullopt;
    }();
    if (!oldest) {
      break;  // Nothing left to drop; the file is large for another reason.
    }
    sql::Statement drop(db_.GetCachedStatement(
        SQL_FROM_HERE, "DELETE FROM tracker_daily WHERE day = ?"));
    drop.BindInt64(0, *oldest);
    if (!drop.Run()) {
      break;
    }
  }
  return true;
}

bool PrivacyDatabase::DeleteAllBrowsingData() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureOpen()) {
    return false;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return false;
  }
  // A FULL clear takes the lifetime total too (§5.2.1). Only a time-ranged
  // clear spares it, because a range cannot meaningfully subtract from a
  // running total.
  if (!db_.Execute("DELETE FROM tracker_daily") ||
      !db_.Execute("DELETE FROM timeline_event") ||
      !db_.Execute("DELETE FROM site") ||
      !db_.Execute("DELETE FROM lifetime_counters")) {
    return false;
  }
  return transaction.Commit();
}

bool PrivacyDatabase::DeleteRange(base::Time begin, base::Time end) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureOpen()) {
    return false;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return false;
  }

  sql::Statement events(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "DELETE FROM timeline_event WHERE timestamp_ms>=? AND timestamp_ms<?"));
  events.BindInt64(0, begin.InMillisecondsSinceUnixEpoch());
  events.BindInt64(1, end.InMillisecondsSinceUnixEpoch());
  if (!events.Run()) {
    return false;
  }

  // Daily rows are whole-day aggregates, so a partial day cannot be split.
  // Any day the range touches is deleted ENTIRELY — deleting too much is the
  // safe direction for a privacy clear; keeping a partially-cleared day would
  // leave data the user asked to be gone.
  sql::Statement days(db_.GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM tracker_daily WHERE day>=? AND day<=?"));
  days.BindInt64(0, ToUtcDay(begin));
  days.BindInt64(1, ToUtcDay(end));
  if (!days.Run()) {
    return false;
  }

  if (!db_.Execute("DELETE FROM site WHERE site_id NOT IN "
                   "(SELECT DISTINCT site_id FROM tracker_daily UNION "
                   "SELECT DISTINCT site_id FROM timeline_event)")) {
    return false;
  }
  return transaction.Commit();
}

DatabaseStats PrivacyDatabase::GetStats() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DatabaseStats stats;
  if (!EnsureOpen()) {
    return stats;
  }
  // Each query needs its OWN SQL_FROM_HERE. Wrapping these in a lambda gave
  // all three the lambda body's source location, so they shared one cache slot
  // while asking different questions — the second and third silently returned
  // the first one's statement.
  {
    sql::Statement query(db_.GetCachedStatement(
        SQL_FROM_HERE, "SELECT COUNT(*) FROM tracker_daily"));
    stats.daily_rows = query.Step() ? query.ColumnInt64(0) : 0;
  }
  {
    sql::Statement query(db_.GetCachedStatement(
        SQL_FROM_HERE, "SELECT COUNT(*) FROM timeline_event"));
    stats.timeline_rows = query.Step() ? query.ColumnInt64(0) : 0;
  }
  {
    sql::Statement query(
        db_.GetCachedStatement(SQL_FROM_HERE, "SELECT COUNT(*) FROM site"));
    stats.site_rows = query.Step() ? query.ColumnInt64(0) : 0;
  }

  sql::Statement life(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT total_detected,total_blocked,total_randomized FROM "
      "lifetime_counters WHERE id=1"));
  if (life.Step()) {
    stats.lifetime_detected = static_cast<uint64_t>(life.ColumnInt64(0));
    stats.lifetime_blocked = static_cast<uint64_t>(life.ColumnInt64(1));
    stats.lifetime_randomized = static_cast<uint64_t>(life.ColumnInt64(2));
  }
  // The -wal and -shm files count. In WAL mode the main file can sit at a few
  // KB for a long time while megabytes of committed data live in the log, so
  // measuring db_path_ alone under-reports the real footprint by orders of
  // magnitude — and §8.1's size budget is enforced against what is actually on
  // the user's disk.
  const base::FilePath companions[] = {
      db_path_,
      // Plain concatenation, not AddExtension(): SQLite's companion files are
      // "<db>-wal" and "<db>-shm", and AddExtension would insert a separator
      // and produce "<db>.-wal", which exists nowhere and silently measures 0.
      base::FilePath(db_path_.value() + FILE_PATH_LITERAL("-wal")),
      base::FilePath(db_path_.value() + FILE_PATH_LITERAL("-shm")),
  };
  for (const base::FilePath& path : companions) {
    if (const std::optional<int64_t> size = base::GetFileSize(path)) {
      stats.file_size_bytes += *size;
    }
  }
  return stats;
}

}  // namespace zephyrus_privacy
