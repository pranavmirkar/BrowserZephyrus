// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_database.h"

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/stringprintf.h"
#include "base/memory/raw_ptr.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/browser/zephyrus/privacy/privacy_crypto.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

// Reversible, not secure — the point is to exercise the storage path, not the
// keystore. Deliberately NOT the identity function: a XOR means a test that
// accidentally reads plaintext out of the file would fail rather than pass.
class FakeCrypto : public PrivacyCrypto {
 public:
  // These tests are about storage, never about key rotation.
  bool key_was_rotated() const override { return false; }
  std::optional<std::vector<uint8_t>> Encrypt(
      std::string_view plaintext) override {
    if (fail_) {
      return std::nullopt;
    }
    std::vector<uint8_t> out;
    out.reserve(plaintext.size());
    for (char c : plaintext) {
      out.push_back(static_cast<uint8_t>(c) ^ 0x5A);
    }
    return out;
  }

  std::optional<std::string> Decrypt(
      base::span<const uint8_t> ciphertext) override {
    if (fail_) {
      return std::nullopt;
    }
    std::string out;
    out.reserve(ciphertext.size());
    for (uint8_t b : ciphertext) {
      out.push_back(static_cast<char>(b ^ 0x5A));
    }
    return out;
  }

  // Stand-in for HMAC: keyed, so a different key gives different digests.
  // Not cryptographic — the property under test is that the lookup column
  // depends on the key, not the strength of the primitive.
  std::optional<int64_t> KeyedHash(std::string_view value) override {
    if (fail_) {
      return std::nullopt;
    }
    uint64_t h = 1469598103934665603ull ^ key_;
    for (char c : value) {
      h ^= static_cast<uint8_t>(c);
      h *= 1099511628211ull;
    }
    return static_cast<int64_t>(h >> 1);  // Positive, 63-bit.
  }

  void set_fail(bool fail) { fail_ = fail; }
  void set_key(uint64_t key) { key_ = key; }

 private:
  bool fail_ = false;
  uint64_t key_ = 0xA5A5A5A5A5A5A5A5ull;
};

int64_t DayOf(base::Time t) {
  return t.InMillisecondsSinceUnixEpoch() / base::Time::kMillisecondsPerDay;
}

class PrivacyDatabaseTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    MakeDatabase();
  }

  void MakeDatabase(bool with_crypto = true, uint64_t key = 0) {
    std::unique_ptr<FakeCrypto> crypto;
    if (with_crypto) {
      crypto = std::make_unique<FakeCrypto>();
      if (key) {
        crypto->set_key(key);
      }
    }
    crypto_ = crypto.get();
    db_ = std::make_unique<PrivacyDatabase>(DbPath(), std::move(crypto));
    db_->SetClockForTesting(now_);
  }

  base::FilePath DbPath() const {
    return temp_dir_.GetPath().AppendASCII("privacy.db");
  }

  // A row on `day`, defaulting to one blocked request.
  DailyRow Row(int64_t day, int64_t site_id, const std::string& domain) {
    DailyRow row;
    row.day = day;
    // The helper still takes an integer so existing cases read the same;
    // it becomes a distinct site NAME, which is what the row carries now.
    row.site_etld1 = base::StringPrintf("site%d.example", static_cast<int>(site_id));
    row.request_domain = domain;
    row.category = Category::kAdvertising;
    row.blocked = 1;
    return row;
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<PrivacyDatabase> db_;
  raw_ptr<FakeCrypto> crypto_ = nullptr;
  // A fixed "now" so day-boundary behaviour is deterministic.
  base::Time now_ = base::Time::UnixEpoch() + base::Days(20000);
};

// --- Opening, encryption, and the refusal path ------------------------------

TEST_F(PrivacyDatabaseTest, OpensLazilyAndCreatesSchema) {
  // Nothing has been written, so the file must not exist yet: a session with
  // no browsing never touches the disk (§8.8).
  EXPECT_FALSE(base::PathExists(DbPath()));
  EXPECT_TRUE(db_->EnsureOpen());
  EXPECT_TRUE(base::PathExists(DbPath()));
}

// §10: a missing keystore must mean no database, never a plaintext one.
TEST_F(PrivacyDatabaseTest, RefusesToOpenWithoutAnEncryptor) {
  MakeDatabase(/*with_crypto=*/false);
  EXPECT_FALSE(db_->EnsureOpen());
  EXPECT_FALSE(base::PathExists(DbPath()));
  // And stays refused rather than retrying into a half state.
  EXPECT_FALSE(db_->EnsureOpen());
  EXPECT_FALSE(db_->InternSite("example.com").has_value());
}

// A keystore that disappears mid-session drops the write; it does not fall
// back to storing the domain in the clear.
TEST_F(PrivacyDatabaseTest, DropsWritesWhenEncryptionStartsFailing) {
  ASSERT_TRUE(db_->EnsureOpen());
  crypto_->set_fail(true);
  EXPECT_FALSE(db_->InternSite("example.com").has_value());
  EXPECT_EQ(0, db_->GetStats().site_rows);
}

TEST_F(PrivacyDatabaseTest, DomainsAreNotStoredInPlaintext) {
  ASSERT_TRUE(db_->EnsureOpen());
  const auto site = db_->InternSite("supersecret-example.com");
  ASSERT_TRUE(site.has_value());
  ASSERT_TRUE(db_->FlushDaily(
      std::vector<DailyRow>{Row(DayOf(now_), *site, "tracker-example.net")}));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(DbPath(), &contents));
  EXPECT_EQ(std::string::npos, contents.find("supersecret-example.com"))
      << "site domain found in plaintext on disk";
  EXPECT_EQ(std::string::npos, contents.find("tracker-example.net"))
      << "request domain found in plaintext on disk";
}

// --- Interning --------------------------------------------------------------

TEST_F(PrivacyDatabaseTest, InternSiteIsStableAndDeduplicates) {
  ASSERT_TRUE(db_->EnsureOpen());
  const auto first = db_->InternSite("example.com");
  const auto again = db_->InternSite("example.com");
  const auto other = db_->InternSite("other.com");
  ASSERT_TRUE(first && again && other);
  EXPECT_EQ(*first, *again);
  EXPECT_NE(*first, *other);
  EXPECT_EQ(2, db_->GetStats().site_rows);
}

// --- Aggregation ------------------------------------------------------------

TEST_F(PrivacyDatabaseTest, FlushAccumulatesCountsForTheSameKey) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t site = *db_->InternSite("news.example");
  const int64_t day = DayOf(now_);

  ASSERT_TRUE(db_->FlushDaily(std::vector<DailyRow>{Row(day, site, "ads.example")}));
  ASSERT_TRUE(db_->FlushDaily(std::vector<DailyRow>{Row(day, site, "ads.example")}));
  ASSERT_TRUE(db_->FlushDaily(std::vector<DailyRow>{Row(day, site, "ads.example")}));

  // Three flushes of the same (day, site, domain) must be ONE row, not three:
  // per-request rows are what §5.1 exists to prevent.
  EXPECT_EQ(1, db_->GetStats().daily_rows);
}

TEST_F(PrivacyDatabaseTest, DistinctDomainsGetDistinctRows) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t site = *db_->InternSite("news.example");
  const int64_t day = DayOf(now_);
  ASSERT_TRUE(db_->FlushDaily(std::vector<DailyRow>{
      Row(day, site, "a.example"), Row(day, site, "b.example")}));
  EXPECT_EQ(2, db_->GetStats().daily_rows);
}

// --- Retention --------------------------------------------------------------

TEST_F(PrivacyDatabaseTest, PruneKeepsExactlyTheRetentionWindow) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t site = *db_->InternSite("news.example");
  const int64_t today = DayOf(now_);

  std::vector<DailyRow> rows;
  for (int age = 0; age < 10; ++age) {
    rows.push_back(Row(today - age, site, "ads.example"));
  }
  ASSERT_TRUE(db_->FlushDaily(rows));
  ASSERT_EQ(10, db_->GetStats().daily_rows);

  RetentionConfig config;
  config.daily_days = 7;
  ASSERT_TRUE(db_->Prune(config));

  // Days today-6 .. today inclusive: seven days, not six or eight.
  EXPECT_EQ(7, db_->GetStats().daily_rows);
}

// §9.6: a clock moved backwards must not wipe the database.
TEST_F(PrivacyDatabaseTest, PruneSurvivesTheClockMovingBackwards) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t site = *db_->InternSite("news.example");
  const int64_t today = DayOf(now_);

  std::vector<DailyRow> rows;
  for (int age = 0; age < 5; ++age) {
    rows.push_back(Row(today - age, site, "ads.example"));
  }
  ASSERT_TRUE(db_->FlushDaily(rows));

  // The user (or NTP) sets the clock back a year.
  db_->SetClockForTesting(now_ - base::Days(365));
  RetentionConfig config;
  config.daily_days = 7;
  ASSERT_TRUE(db_->Prune(config));

  // Pruning is relative to the newest STORED day, so nothing is lost.
  EXPECT_EQ(5, db_->GetStats().daily_rows);
}

// §5.2: "Timeline rows | 5,000 | Ring buffer, oldest dropped", and §16's
// acceptance line "timeline stays within its ring".
//
// Age-based retention does NOT bound this on its own, which is what the ring
// exists for: every event below is well inside the 48-hour window, so the
// timestamp sweep keeps all of them. Before the ring was enforced, a busy two
// days produced an unbounded timestamped narrative of where the user went —
// §5.2 calls that the highest sensitivity per byte in the feature.
TEST_F(PrivacyDatabaseTest, TimelineStaysWithinItsRing) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t site = *db_->InternSite("news.example");

  // 5,200 recent events: over the cap, none of them old enough to expire.
  for (int i = 0; i < 5200; ++i) {
    ASSERT_TRUE(db_->AddTimelineEvent(now_ - base::Minutes(1), site, kNoEntity,
                                      EventType::kFingerprintAttempt,
                                      TrackerStatus::kDetected));
  }
  ASSERT_EQ(5200, db_->GetStats().timeline_rows);

  RetentionConfig config;
  config.timeline_hours = 48;
  ASSERT_TRUE(db_->Prune(config));

  EXPECT_EQ(5000, db_->GetStats().timeline_rows)
      << "the ring must bound the timeline even when nothing has aged out";
}

// §8.9: "Paginate the timeline. Never SELECT * on the event table."
TEST_F(PrivacyDatabaseTest, TimelineReadIsBoundedAndNewestFirst) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t site = *db_->InternSite("news.example");
  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(db_->AddTimelineEvent(now_ - base::Minutes(20 - i), site,
                                      kNoEntity, EventType::kWebrtcAddressRequest,
                                      TrackerStatus::kDetected));
  }

  const std::vector<PrivacyDatabase::TimelineEntry> page = db_->GetTimeline(5);
  ASSERT_EQ(5u, page.size()) << "the limit must be honoured";
  EXPECT_EQ("news.example", page[0].site_etld1)
      << "the site name must come back decrypted, not as ciphertext";
  // Newest first, so the page the UI shows is the most recent slice.
  for (size_t i = 1; i < page.size(); ++i) {
    EXPECT_LE(page[i].when, page[i - 1].when);
  }
  EXPECT_EQ(EventType::kWebrtcAddressRequest, page[0].event_type);
  EXPECT_TRUE(db_->GetTimeline(0).empty());
}

TEST_F(PrivacyDatabaseTest, TimelinePrunesOnItsOwnMuchShorterClock) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t site = *db_->InternSite("news.example");

  ASSERT_TRUE(db_->AddTimelineEvent(now_ - base::Hours(1), site, kNoEntity,
                                    EventType::kFirstSeenOnSite,
                                    TrackerStatus::kBlocked));
  ASSERT_TRUE(db_->AddTimelineEvent(now_ - base::Hours(72), site, kNoEntity,
                                    EventType::kCrossSiteDetected,
                                    TrackerStatus::kBlocked));
  ASSERT_EQ(2, db_->GetStats().timeline_rows);

  RetentionConfig config;
  config.daily_days = 30;    // Aggregates keep a month...
  config.timeline_hours = 48;  // ...the narrative keeps two days.
  ASSERT_TRUE(db_->Prune(config));
  EXPECT_EQ(1, db_->GetStats().timeline_rows);
}

// "Off" means gone, not paused.
TEST_F(PrivacyDatabaseTest, TimelineDisabledDropsEverything) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t site = *db_->InternSite("news.example");
  ASSERT_TRUE(db_->AddTimelineEvent(now_, site, kNoEntity,
                                    EventType::kFirstSeenOnSite,
                                    TrackerStatus::kBlocked));
  RetentionConfig config;
  config.timeline_hours = 0;
  ASSERT_TRUE(db_->Prune(config));
  EXPECT_EQ(0, db_->GetStats().timeline_rows);
}

TEST_F(PrivacyDatabaseTest, PruneCollectsSitesNothingRefersToAnyMore) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t today = DayOf(now_);
  // The row names its own site now; FlushDaily interns it. Interning up front
  // would create a SECOND site row and this test would be measuring nothing.
  DailyRow row = Row(today - 30, 1, "ads.example");
  row.site_etld1 = "ancient.example";
  ASSERT_TRUE(db_->FlushDaily(std::vector<DailyRow>{row}));
  ASSERT_EQ(1, db_->GetStats().site_rows);

  RetentionConfig config;
  config.daily_days = 7;
  ASSERT_TRUE(db_->Prune(config));

  // The counts expired, so the site name must go too: a site table outliving
  // its rows is a standing record of everywhere the user has ever been.
  EXPECT_EQ(0, db_->GetStats().daily_rows);
  EXPECT_EQ(0, db_->GetStats().site_rows);
}

// --- §5.2.2 coupling to history retention -----------------------------------

TEST(PrivacyRetentionTest, ClampLeavesShorterRetentionAlone) {
  RetentionConfig config;
  config.daily_days = 7;
  config.timeline_hours = 48;
  config.history_retention_days = 90;
  const RetentionConfig out = ClampToHistoryRetention(config);
  EXPECT_EQ(7, out.daily_days);
  EXPECT_EQ(48, out.timeline_hours);
}

// The case that matters: privacy data must never outlive the history it
// describes, even if the user picks the longest privacy window.
TEST(PrivacyRetentionTest, ClampCapsRetentionAtHistoryRetention) {
  RetentionConfig config;
  config.daily_days = 30;
  config.timeline_hours = 24 * 30;
  config.history_retention_days = 14;  // A browser keeping only two weeks.
  const RetentionConfig out = ClampToHistoryRetention(config);
  EXPECT_EQ(14, out.daily_days);
  EXPECT_EQ(14 * 24, out.timeline_hours);
}

TEST(PrivacyRetentionTest, ClampHandlesZeroHistoryRetention) {
  RetentionConfig config;
  config.daily_days = 7;
  config.timeline_hours = 48;
  config.history_retention_days = 0;  // Browser keeps no history at all.
  const RetentionConfig out = ClampToHistoryRetention(config);
  EXPECT_EQ(0, out.daily_days);
  EXPECT_EQ(0, out.timeline_hours);
}

// End to end through Prune(), not just the pure function.
TEST_F(PrivacyDatabaseTest, PruneHonoursHistoryRetentionOverTheConfiguredWindow) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t site = *db_->InternSite("news.example");
  const int64_t today = DayOf(now_);

  std::vector<DailyRow> rows;
  for (int age = 0; age < 10; ++age) {
    rows.push_back(Row(today - age, site, "ads.example"));
  }
  ASSERT_TRUE(db_->FlushDaily(rows));

  RetentionConfig config;
  config.daily_days = 30;             // The user asked for a month...
  config.history_retention_days = 3;  // ...but history only keeps three days.
  ASSERT_TRUE(db_->Prune(config));

  EXPECT_EQ(3, db_->GetStats().daily_rows);
}

// --- Lifetime counters ------------------------------------------------------

TEST_F(PrivacyDatabaseTest, LifetimeCountersAccumulate) {
  ASSERT_TRUE(db_->EnsureOpen());
  ASSERT_TRUE(db_->AddLifetime(10, 7, 1));
  ASSERT_TRUE(db_->AddLifetime(5, 4, 0));
  const DatabaseStats stats = db_->GetStats();
  EXPECT_EQ(15u, stats.lifetime_detected);
  EXPECT_EQ(11u, stats.lifetime_blocked);
  EXPECT_EQ(1u, stats.lifetime_randomized);
}

// §5.2.1: a FULL clear takes the lifetime total as well. Only a time-ranged
// clear spares it (§5.2.2), because a range cannot subtract from a running
// total.
TEST_F(PrivacyDatabaseTest, FullClearAlsoClearsLifetimeCounters) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t site = *db_->InternSite("news.example");
  ASSERT_TRUE(db_->FlushDaily(
      std::vector<DailyRow>{Row(DayOf(now_), site, "ads.example")}));
  ASSERT_TRUE(db_->AddTimelineEvent(now_, site, kNoEntity,
                                    EventType::kFirstSeenOnSite,
                                    TrackerStatus::kBlocked));
  ASSERT_TRUE(db_->AddLifetime(100, 90, 0));

  ASSERT_TRUE(db_->DeleteAllBrowsingData());

  const DatabaseStats stats = db_->GetStats();
  EXPECT_EQ(0, stats.daily_rows);
  EXPECT_EQ(0, stats.timeline_rows);
  EXPECT_EQ(0, stats.site_rows);
  EXPECT_EQ(0u, stats.lifetime_blocked);
}

// The partial-clear case §5.2.2 calls out: today reads zero, the running total
// does not.
TEST_F(PrivacyDatabaseTest, TimeRangedClearSparesLifetimeCounters) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t site = *db_->InternSite("news.example");
  ASSERT_TRUE(db_->FlushDaily(
      std::vector<DailyRow>{Row(DayOf(now_), site, "ads.example")}));
  ASSERT_TRUE(db_->AddLifetime(100, 90, 0));

  ASSERT_TRUE(db_->DeleteRange(now_ - base::Hours(1), now_));

  const DatabaseStats stats = db_->GetStats();
  EXPECT_EQ(0, stats.daily_rows) << "the touched day should be gone";
  EXPECT_EQ(90u, stats.lifetime_blocked) << "the running total should remain";
}

// §5.3 / §15: the lookup column must be a KEYED digest. Reopening the same
// file with a different key must not find the site — if it does, the column is
// an unkeyed hash and the encrypted column beside it is decoration.
TEST_F(PrivacyDatabaseTest, LookupColumnIsKeyedNotAPlainHash) {
  MakeDatabase(/*with_crypto=*/true, /*key=*/0x1111111111111111ull);
  ASSERT_TRUE(db_->EnsureOpen());
  const auto with_key_a = db_->InternSite("news.example");
  ASSERT_TRUE(with_key_a.has_value());
  EXPECT_EQ(1, db_->GetStats().site_rows);
  db_.reset();

  MakeDatabase(/*with_crypto=*/true, /*key=*/0x2222222222222222ull);
  ASSERT_TRUE(db_->EnsureOpen());
  const auto with_key_b = db_->InternSite("news.example");
  ASSERT_TRUE(with_key_b.has_value());
  // A different key produced a different lookup value, so the existing row was
  // not matched and a second one was inserted.
  EXPECT_NE(*with_key_a, *with_key_b);
  EXPECT_EQ(2, db_->GetStats().site_rows);
}

// A keystore that cannot produce the lookup key is the same as no encryptor:
// nothing is written.
TEST_F(PrivacyDatabaseTest, KeyedHashFailureDropsTheWrite) {
  ASSERT_TRUE(db_->EnsureOpen());
  crypto_->set_fail(true);
  EXPECT_FALSE(db_->InternSite("news.example").has_value());
  EXPECT_EQ(0, db_->GetStats().site_rows);
}

// --- Time-ranged clear ------------------------------------------------------

TEST_F(PrivacyDatabaseTest, DeleteRangeRemovesOnlyTheMatchingTimelineEvents) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t site = *db_->InternSite("news.example");
  ASSERT_TRUE(db_->AddTimelineEvent(now_ - base::Hours(10), site, kNoEntity,
                                    EventType::kFirstSeenOnSite,
                                    TrackerStatus::kBlocked));
  ASSERT_TRUE(db_->AddTimelineEvent(now_ - base::Hours(1), site, kNoEntity,
                                    EventType::kFirstSeenOnSite,
                                    TrackerStatus::kBlocked));

  ASSERT_TRUE(db_->DeleteRange(now_ - base::Hours(2), now_));
  EXPECT_EQ(1, db_->GetStats().timeline_rows);
}

// Documents the deliberate over-deletion: daily rows are whole-day aggregates
// and cannot be split, so a range touching today removes today entirely.
// Erring toward deleting more is the only safe direction for a privacy clear.
TEST_F(PrivacyDatabaseTest, DeleteRangeRemovesAnyDayItTouchesInFull) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t site = *db_->InternSite("news.example");
  const int64_t today = DayOf(now_);
  ASSERT_TRUE(db_->FlushDaily(std::vector<DailyRow>{
      Row(today, site, "ads.example"), Row(today - 5, site, "ads.example")}));
  ASSERT_EQ(2, db_->GetStats().daily_rows);

  // "Clear the last hour."
  ASSERT_TRUE(db_->DeleteRange(now_ - base::Hours(1), now_));

  // Today's whole row is gone; the older day is untouched.
  EXPECT_EQ(1, db_->GetStats().daily_rows);
}

// --- Reopening --------------------------------------------------------------

TEST_F(PrivacyDatabaseTest, DataAndSiteIdsSurviveReopen) {
  ASSERT_TRUE(db_->EnsureOpen());
  const int64_t site = *db_->InternSite("news.example");
  ASSERT_TRUE(db_->FlushDaily(
      std::vector<DailyRow>{Row(DayOf(now_), site, "ads.example")}));
  ASSERT_TRUE(db_->AddLifetime(1, 1, 0));
  db_.reset();

  MakeDatabase();
  ASSERT_TRUE(db_->EnsureOpen());
  // Site ids must be stable across restart — the in-memory aggregates keep
  // using them (§8.6).
  EXPECT_EQ(site, db_->InternSite("news.example").value());
  EXPECT_EQ(1, db_->GetStats().daily_rows);
  EXPECT_EQ(1u, db_->GetStats().lifetime_blocked);
}

}  // namespace
}  // namespace zephyrus_privacy
