// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/domain_string_table.h"

#include "base/memory/scoped_refptr.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

class DomainStringTableTest : public testing::Test {
 protected:
  scoped_refptr<DomainStringTable> table_ =
      base::MakeRefCounted<DomainStringTable>();
};

TEST_F(DomainStringTableTest, CarriesANameAcrossToTheConsumer) {
  table_->Record(7, "ads.example");
  EXPECT_EQ("ads.example", table_->Lookup(7));
  EXPECT_EQ(std::string(), table_->Lookup(8));
}

// Re-recording refreshes the epoch but must not rewrite the string: this is
// the hot path, and it should cost a find plus an integer store, no allocation.
TEST_F(DomainStringTableTest, FirstRecordWins) {
  table_->Record(7, "first.example");
  table_->Record(7, "second.example");
  EXPECT_EQ("first.example", table_->Lookup(7));
}

TEST_F(DomainStringTableTest, EmptyNamesAreNotRecorded) {
  table_->Record(7, "");
  table_->RecordSite(7, "");
  EXPECT_EQ(0u, table_->size());
}

// The whole point of splitting the namespaces: a site and a domain that hash to
// the same value must not be handed each other's name.
TEST_F(DomainStringTableTest, SitesAndDomainsDoNotShareAKeyspace) {
  table_->Record(42, "tracker.example");
  table_->RecordSite(42, "news.example");
  EXPECT_EQ("tracker.example", table_->Lookup(42));
  EXPECT_EQ("news.example", table_->LookupSite(42));
}

// Two epochs of grace. A name recorded now must survive the flush that happens
// immediately after it, because the event it belongs to may not be aggregated
// until the flush after that.
TEST_F(DomainStringTableTest, SurvivesExactlyOneSweepThenGoes) {
  table_->Record(7, "ads.example");

  EXPECT_EQ(0u, table_->SweepAndAdvance());
  EXPECT_EQ("ads.example", table_->Lookup(7))
      << "a name must outlive the first flush after it was recorded";

  EXPECT_EQ(1u, table_->SweepAndAdvance());
  EXPECT_EQ(std::string(), table_->Lookup(7));
  EXPECT_EQ(0u, table_->size());
}

// An actively requested domain re-records on every request, which must reset
// the clock — otherwise a long-lived page would lose its own names.
TEST_F(DomainStringTableTest, ReRecordingRefreshesTheGrace) {
  table_->Record(7, "ads.example");
  for (int i = 0; i < 10; ++i) {
    table_->SweepAndAdvance();
    table_->Record(7, "ads.example");
  }
  EXPECT_EQ("ads.example", table_->Lookup(7));
  EXPECT_EQ(1u, table_->size());
}

TEST_F(DomainStringTableTest, SweepCoversSiteNamesToo) {
  table_->RecordSite(3, "news.example");
  table_->SweepAndAdvance();
  table_->SweepAndAdvance();
  EXPECT_EQ(std::string(), table_->LookupSite(3));
}

// §11.3: a page firing requests at thousands of unique subdomains must not be
// able to grow this without limit.
TEST_F(DomainStringTableTest, DropsPastTheCapInsteadOfGrowing) {
  for (uint32_t i = 0; i < DomainStringTable::kMaxEntries + 500; ++i) {
    table_->Record(i, "d.example");
  }
  EXPECT_EQ(DomainStringTable::kMaxEntries, table_->size());
  EXPECT_EQ(500u, table_->dropped_count());
}

// The reason sites got their own cap: a subdomain flood used to be able to
// exhaust the shared table and starve out the site name, turning a real site
// into "(unknown site)" in the database.
TEST_F(DomainStringTableTest, ADomainFloodCannotStarveOutSiteNames) {
  for (uint32_t i = 0; i < DomainStringTable::kMaxEntries * 2; ++i) {
    table_->Record(i, "flood.example");
  }
  table_->RecordSite(1, "news.example");
  EXPECT_EQ("news.example", table_->LookupSite(1));
}

TEST_F(DomainStringTableTest, ClearEmptiesBothNamespaces) {
  table_->Record(7, "ads.example");
  table_->RecordSite(3, "news.example");
  table_->Clear();
  EXPECT_EQ(0u, table_->size());
  EXPECT_EQ(std::string(), table_->Lookup(7));
  EXPECT_EQ(std::string(), table_->LookupSite(3));

  // Clear resets the epoch; a name recorded afterwards still gets full grace
  // rather than being swept immediately.
  table_->Record(7, "ads.example");
  table_->SweepAndAdvance();
  EXPECT_EQ("ads.example", table_->Lookup(7));
}

}  // namespace
}  // namespace zephyrus_privacy
