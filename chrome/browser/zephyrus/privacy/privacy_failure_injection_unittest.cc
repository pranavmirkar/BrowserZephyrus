// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §12.3 failure injection: one test per row of the §10 failure table.
//
// "Failure paths that are never run are failure paths that do not work."
// Every case here asserts the same contract §10 opens with: the feature
// degrades, and browsing is untouched. Nothing below may crash, hang, or
// retry-loop, and nothing below may write plaintext.

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/zephyrus/privacy/entity_resolver.h"
#include "chrome/browser/zephyrus/privacy/privacy_crypto.h"
#include "chrome/browser/zephyrus/privacy/privacy_database.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "chrome/browser/zephyrus/privacy/privacy_event_sink.h"
#include "sql/database.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

// A crypto that works, so these tests exercise the failure being injected
// rather than the keystore path. Deterministic and reversible: this is about
// storage behaviour, not cipher strength.
class FakeCrypto : public PrivacyCrypto {
 public:
  std::optional<std::vector<uint8_t>> Encrypt(std::string_view plain) override {
    std::vector<uint8_t> out(plain.begin(), plain.end());
    for (uint8_t& b : out) {
      b ^= 0x5a;
    }
    return out;
  }
  std::optional<std::string> Decrypt(
      base::span<const uint8_t> cipher) override {
    std::string out(cipher.begin(), cipher.end());
    for (char& c : out) {
      c ^= 0x5a;
    }
    return out;
  }
  bool key_was_rotated() const override { return rotated; }
  bool rotated = false;

  std::optional<int64_t> KeyedHash(std::string_view value) override {
    int64_t h = 1125899906842597;
    for (char c : value) {
      h = h * 31 + static_cast<unsigned char>(c);
    }
    return h & 0x7fffffffffffffffLL;
  }
};

class PrivacyFailureInjectionTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    db_path_ = dir_.GetPath().AppendASCII("ZephyrusPrivacy.db");
  }

  std::unique_ptr<PrivacyDatabase> Open(
      std::unique_ptr<PrivacyCrypto> crypto = std::make_unique<FakeCrypto>()) {
    return std::make_unique<PrivacyDatabase>(db_path_, std::move(crypto));
  }

  DailyRow Row(const std::string& site, const std::string& domain) {
    DailyRow row;
    row.day = 20000;
    row.site_etld1 = site;
    row.request_domain = domain;
    row.detected = 1;
    return row;
  }

  base::ScopedTempDir dir_;
  base::FilePath db_path_;
};

// §10: DB corruption -> raze and recreate. Never crash, never retry-loop.
TEST_F(PrivacyFailureInjectionTest, CorruptFileIsRazedAndRecreated) {
  {
    auto db = Open();
    ASSERT_TRUE(db->EnsureOpen());
    ASSERT_TRUE(db->FlushDaily(std::vector<DailyRow>{Row("a.example",
                                                         "t.example")}));
  }
  // Keep a valid SQLite header so the file opens and fails on READ, which is
  // the real corruption shape. Garbage from byte zero fails at open instead and
  // exercises a different branch.
  std::string blob;
  ASSERT_TRUE(base::ReadFileToString(db_path_, &blob));
  ASSERT_GT(blob.size(), 2048u);
  for (size_t i = 1024; i < blob.size(); ++i) {
    blob[i] = static_cast<char>(0xA5);
  }
  ASSERT_TRUE(base::WriteFile(db_path_, blob));

  auto db = Open();
  EXPECT_TRUE(db->EnsureOpen()) << "§10: corruption must be recovered from, "
                                   "not surfaced as a dead feature";
  // Recreated means empty, and empty means writable again.
  EXPECT_TRUE(db->FlushDaily(std::vector<DailyRow>{Row("b.example",
                                                       "t2.example")}));
  EXPECT_GT(db->GetStats().daily_rows, 0);
}

// §10: unknown future schema version -> raze and recreate (§5.4). A profile
// moved back to an older build must not brick the feature, and must never be
// read with the wrong schema assumptions.
TEST_F(PrivacyFailureInjectionTest, FutureSchemaVersionIsRazed) {
  {
    auto db = Open();
    ASSERT_TRUE(db->EnsureOpen());
    ASSERT_TRUE(db->FlushDaily(std::vector<DailyRow>{Row("a.example",
                                                         "t.example")}));
  }
  {
    sql::Database raw(sql::DatabaseOptions(), sql::Database::Tag("Test"));
    ASSERT_TRUE(raw.Open(db_path_));
    ASSERT_TRUE(raw.Execute("UPDATE meta SET value=9999 WHERE key='version'"));
    ASSERT_TRUE(raw.Execute(
        "UPDATE meta SET value=9999 WHERE key='last_compatible_version'"));
  }

  auto db = Open();
  EXPECT_TRUE(db->EnsureOpen());
  EXPECT_EQ(0, db->GetStats().daily_rows)
      << "a database from the future must be razed, not read";
  EXPECT_TRUE(db->FlushDaily(std::vector<DailyRow>{Row("b.example",
                                                       "t2.example")}));
}

// §10: disk full (SQLITE_FULL) -> stop writing, stay alive, do not corrupt.
// PRAGMA max_page_count makes SQLite return SQLITE_FULL for real, which is a
// truer simulation than mocking the return value.
TEST_F(PrivacyFailureInjectionTest, DiskFullStopsWritingWithoutCorrupting) {
  auto db = Open();
  ASSERT_TRUE(db->EnsureOpen());
  ASSERT_TRUE(db->FlushDaily(std::vector<DailyRow>{Row("a.example",
                                                       "t.example")}));
  ASSERT_TRUE(db->SetMaxPageCountForTesting(2));

  std::vector<DailyRow> flood;
  for (int i = 0; i < 400; ++i) {
    flood.push_back(Row("site" + base::NumberToString(i) + ".example",
                        "tracker" + base::NumberToString(i) + ".example"));
  }
  // The contract is "returns false and the process survives", not "succeeds".
  // Whether this particular batch happens to fit is not the point.
  const bool ok = db->FlushDaily(flood);
  EXPECT_FALSE(ok) << "a full disk must be reported, not silently swallowed";

  // The transaction rolled back, so the pre-existing row is still readable and
  // the file is still a valid database -- that is what "never corrupt" means.
  ASSERT_TRUE(db->SetMaxPageCountForTesting(0));
  EXPECT_GT(db->GetStats().daily_rows, 0);
  EXPECT_TRUE(db->FlushDaily(std::vector<DailyRow>{Row("z.example",
                                                       "t3.example")}))
      << "once space is available again the feature must resume, not stay "
         "poisoned for the session";
}

// §10, and the hard rule from §5.3: keystore unavailable -> DO NOT WRITE
// PLAINTEXT. Memory-only for the session.
TEST_F(PrivacyFailureInjectionTest, NoKeystoreWritesNothingAtAll) {
  auto db = Open(/*crypto=*/nullptr);
  EXPECT_FALSE(db->EnsureOpen());
  EXPECT_FALSE(db->FlushDaily(std::vector<DailyRow>{Row("secret.example",
                                                        "tracker.example")}));
  EXPECT_FALSE(base::PathExists(db_path_))
      << "no encryptor must mean no file, not an unencrypted one";
}

// The same, but the keystore dies MID-SESSION after the database is already
// open and holding rows. The dangerous shape: a half-encrypted file.
TEST_F(PrivacyFailureInjectionTest, KeystoreLostMidSessionWritesNoPlaintext) {
  class DyingCrypto : public FakeCrypto {
   public:
    std::optional<std::vector<uint8_t>> Encrypt(
        std::string_view plain) override {
      return dead ? std::nullopt : FakeCrypto::Encrypt(plain);
    }
    std::optional<int64_t> KeyedHash(std::string_view value) override {
      return dead ? std::nullopt : FakeCrypto::KeyedHash(value);
    }
    bool dead = false;
  };
  auto crypto = std::make_unique<DyingCrypto>();
  DyingCrypto* raw_crypto = crypto.get();
  auto db = Open(std::move(crypto));
  ASSERT_TRUE(db->EnsureOpen());
  ASSERT_TRUE(db->FlushDaily(std::vector<DailyRow>{Row("a.example",
                                                       "t.example")}));
  const int64_t before = db->GetStats().daily_rows;

  raw_crypto->dead = true;
  EXPECT_FALSE(db->FlushDaily(std::vector<DailyRow>{Row("secret.example",
                                                        "secret.tracker")}));
  EXPECT_EQ(before, db->GetStats().daily_rows)
      << "a failed encrypt must abort the whole transaction, never land a "
         "partially-encrypted row";

  std::string blob;
  ASSERT_TRUE(base::ReadFileToString(db_path_, &blob));
  EXPECT_EQ(std::string::npos, blob.find("secret.example"));
  EXPECT_EQ(std::string::npos, blob.find("secret.tracker"));
}

// §10: component dataset fails verification -> fall back, never crash. §4.1
// makes the null resolver a first-class state, so every one of these must
// produce a working resolver that simply attributes nothing.
TEST_F(PrivacyFailureInjectionTest, InvalidComponentPayloadFallsBack) {
  struct Case {
    const char* name;
    std::string bytes;
  };
  const std::string kMagic = "ZEPHENT1";
  std::vector<Case> cases = {
      {"empty", ""},
      {"truncated header", "ZEPH"},
      {"bad magic", std::string("NOTMAGIC") + std::string(56, '\0')},
      {"header only, claims entries", kMagic + std::string(56, '\xff')},
      {"garbage", std::string(4096, '\xa5')},
  };
  int n = 0;
  for (const Case& c : cases) {
    const base::FilePath path =
        dir_.GetPath().AppendASCII("artifact" + base::NumberToString(n++));
    ASSERT_TRUE(base::WriteFile(path, c.bytes));
    std::unique_ptr<EntityResolver> resolver =
        EntityResolver::CreateFromFile(path);
    ASSERT_TRUE(resolver) << c.name << ": must fall back, never return null";
    // Usable, and attributes nothing.
    const EntityInfo info = resolver->Lookup(12345);
    EXPECT_EQ(kNoEntity, info.entity_id) << c.name;
    EXPECT_EQ(Category::kUnknown, info.category) << c.name;
  }

  // A path that does not exist at all is the same story.
  std::unique_ptr<EntityResolver> missing = EntityResolver::CreateFromFile(
      dir_.GetPath().AppendASCII("does-not-exist"));
  ASSERT_TRUE(missing);
  EXPECT_EQ(kNoEntity, missing->Lookup(1).entity_id);
}

// §10: sustained ring overflow -> count the drops, keep serving. The producer
// must never block and must never overwrite unread events.
TEST_F(PrivacyFailureInjectionTest, SustainedRingOverflowDropsAndRecovers) {
  auto sink = base::MakeRefCounted<PrivacyEventSink>();
  RawEvent event{};
  event.site_id = 1;
  event.domain_hash = 2;

  int accepted = 0;
  for (int i = 0; i < 100000; ++i) {
    if (sink->Record(event)) {
      ++accepted;
    }
  }
  EXPECT_GT(sink->dropped_count(), 0u);
  EXPECT_EQ(100000u - sink->dropped_count(), static_cast<uint64_t>(accepted))
      << "every event is either accepted or counted as dropped; none may "
         "vanish unaccounted for";

  // Draining must make room again: a full ring is a backlog, not a dead end.
  std::vector<RawEvent> out(8192);
  const size_t drained = sink->Drain(base::span<RawEvent>(out));
  EXPECT_GT(drained, 0u);
  EXPECT_TRUE(sink->Record(event));
}


// §10 has no row for this, but the case is real and the previous behaviour was
// wrong in a way a comment actively hid: when the lookup key cannot be
// unsealed (a profile copied to another machine, or a keystore reset) the code
// rotated the key and claimed "the database's own version/raze path discards
// the stale rows". It does not — rotation does not change the schema version,
// so nothing razed and rows keyed by a key that can never match again simply
// accumulated alongside the new ones, splitting every count in two.
TEST_F(PrivacyFailureInjectionTest, RotatedLookupKeyDiscardsUnreadableRows) {
  {
    auto db = Open();
    ASSERT_TRUE(db->EnsureOpen());
    ASSERT_TRUE(db->FlushDaily(std::vector<DailyRow>{Row("a.example",
                                                         "t.example")}));
    ASSERT_GT(db->GetStats().daily_rows, 0);
  }

  auto crypto = std::make_unique<FakeCrypto>();
  crypto->rotated = true;
  auto db = Open(std::move(crypto));
  ASSERT_TRUE(db->EnsureOpen());
  EXPECT_EQ(0, db->GetStats().daily_rows)
      << "rows the new key cannot read must be discarded, not left to "
         "accumulate beside a second set";
  EXPECT_EQ(0, db->GetStats().site_rows);
  // And the database is immediately usable again.
  EXPECT_TRUE(db->FlushDaily(std::vector<DailyRow>{Row("b.example",
                                                       "t2.example")}));
  EXPECT_GT(db->GetStats().daily_rows, 0);
}

}  // namespace
}  // namespace zephyrus_privacy
