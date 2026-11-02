// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_shared_index.h"

#include <atomic>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/synchronization/lock.h"
#include "base/task/thread_pool.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_adblock {
namespace {

GURL Page() {
  return GURL("https://news.example.org/");
}

class AdblockSharedIndexTest : public testing::Test {
 protected:
  void SetUp() override {
    AdblockSharedIndex::ResetForTesting();
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    cache_file_ = dir_.GetPath().AppendASCII("filters.zidx");
  }
  void TearDown() override { AdblockSharedIndex::ResetForTesting(); }

  static std::unique_ptr<AdblockFilterEngine> EngineFor(
      const std::string& rules) {
    auto engine = std::make_unique<AdblockFilterEngine>();
    engine->AddRules(rules);
    return engine;
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir dir_;
  base::FilePath cache_file_;
};

TEST_F(AdblockSharedIndexTest, TheSecondProfileSharesTheFirstProfilesIndex) {
  const std::string list = "||ads.example^\n@@||ads.example/ok^\n";
  const uint64_t key = AdblockSharedIndex::KeyFor(list);
  auto engine = EngineFor(list);
  EXPECT_FALSE(AdblockSharedIndex::GetCached(key, base::FilePath()))
      << "nothing is built yet";
  scoped_refptr<const AdblockSharedIndex> first =
      AdblockSharedIndex::GetOrBuild(key, *engine, base::FilePath());
  ASSERT_TRUE(first);
  EXPECT_EQ(AdblockSharedIndex::GetCached(key, base::FilePath()).get(),
            first.get());
  EXPECT_EQ(AdblockSharedIndex::GetOrBuild(key, *engine, base::FilePath()).get(),
            first.get());
  EXPECT_EQ(first->block_rule_count(), 1u);
  EXPECT_EQ(first->exception_rule_count(), 1u);
  EXPECT_TRUE(first->ShouldBlock(GURL("https://ads.example/a.js"), Page(),
                                 kTypeScript));
  EXPECT_FALSE(first->ShouldBlock(GURL("https://ads.example/ok/a.js"), Page(),
                                  kTypeScript));
}

TEST_F(AdblockSharedIndexTest, ANewListBuildsANewIndexAndTheOldOneStaysValid) {
  const std::string list_a = "||a.example^\n";
  const std::string list_b = "||b.example^\n";
  auto engine_a = EngineFor(list_a);
  auto engine_b = EngineFor(list_b);
  scoped_refptr<const AdblockSharedIndex> a = AdblockSharedIndex::GetOrBuild(
      AdblockSharedIndex::KeyFor(list_a), *engine_a, base::FilePath());
  scoped_refptr<const AdblockSharedIndex> b = AdblockSharedIndex::GetOrBuild(
      AdblockSharedIndex::KeyFor(list_b), *engine_b, base::FilePath());
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  EXPECT_NE(a.get(), b.get());
  // A profile still holding the old list keeps answering from it.
  EXPECT_TRUE(a->ShouldBlock(GURL("https://a.example/x.js"), Page(), kTypeScript));
  EXPECT_FALSE(a->ShouldBlock(GURL("https://b.example/x.js"), Page(), kTypeScript));
  EXPECT_TRUE(b->ShouldBlock(GURL("https://b.example/x.js"), Page(), kTypeScript));
  // Only the newest is remembered.
  EXPECT_FALSE(AdblockSharedIndex::GetCached(
      AdblockSharedIndex::KeyFor(list_a), base::FilePath()));
}

TEST_F(AdblockSharedIndexTest, TheNextStartLoadsWhatThisOneCached) {
  const std::string list = "||ads.example^\n||tracker.example/pixel^\n";
  const uint64_t key = AdblockSharedIndex::KeyFor(list);
  auto engine = EngineFor(list);
  ASSERT_TRUE(AdblockSharedIndex::GetOrBuild(key, *engine, cache_file_));
  ASSERT_TRUE(base::PathExists(cache_file_));

  AdblockSharedIndex::ResetForTesting();  // a new process
  scoped_refptr<const AdblockSharedIndex> loaded =
      AdblockSharedIndex::GetCached(key, cache_file_);
  ASSERT_TRUE(loaded) << "the cache file should be a hit";
  EXPECT_EQ(loaded->block_rule_count(), 2u);
  EXPECT_TRUE(loaded->ShouldBlock(GURL("https://tracker.example/pixel?x=1"),
                                  Page(), kTypeScript));
  EXPECT_FALSE(loaded->ShouldBlock(GURL("https://tracker.example/other"), Page(),
                                   kTypeScript));

  // A different list is a miss, not somebody else's rules.
  AdblockSharedIndex::ResetForTesting();
  EXPECT_FALSE(AdblockSharedIndex::GetCached(
      AdblockSharedIndex::KeyFor("||other.example^\n"), cache_file_));
}

TEST_F(AdblockSharedIndexTest, ADamagedCacheFileIsAMissAndNeverCrashes) {
  const std::string list = "||ads.example^\n||tracker.example/pixel^\n";
  const uint64_t key = AdblockSharedIndex::KeyFor(list);
  auto engine = EngineFor(list);
  ASSERT_TRUE(AdblockSharedIndex::GetOrBuild(key, *engine, cache_file_));
  std::optional<std::vector<uint8_t>> good = base::ReadFileToBytes(cache_file_);
  ASSERT_TRUE(good);

  // Truncated, empty, garbage, every one-bit flip in the header, and one-bit
  // flips scattered through the blob.
  std::vector<std::vector<uint8_t>> bad;
  bad.emplace_back(good->begin(), good->begin() + good->size() / 2);
  bad.emplace_back();
  bad.emplace_back(good->size(), 0xAB);
  for (size_t i = 0; i < 32; ++i) {
    std::vector<uint8_t> flipped = *good;
    flipped[i] ^= 0x01;
    bad.push_back(std::move(flipped));
  }
  for (size_t i = 0; i < 40; ++i) {
    std::vector<uint8_t> flipped = *good;
    flipped[32 + (i * 7919) % (good->size() - 32)] ^= 0x10;
    bad.push_back(std::move(flipped));
  }
  int accepted = 0;
  for (const std::vector<uint8_t>& file : bad) {
    AdblockSharedIndex::ResetForTesting();
    ASSERT_TRUE(base::WriteFile(cache_file_, file));
    scoped_refptr<const AdblockSharedIndex> got =
        AdblockSharedIndex::GetCached(key, cache_file_);
    if (got) {
      ++accepted;
      got->ShouldBlock(GURL("https://ads.example/a.js"), Page(), kTypeScript);
    }
  }
  EXPECT_EQ(accepted, 0) << "a damaged file must be rebuilt, not trusted";
}

TEST_F(AdblockSharedIndexTest, ProfilesAskingAtOnceShareOneIndex) {
  const std::string list = "||ads.example^\n||tracker.example^\n";
  const uint64_t key = AdblockSharedIndex::KeyFor(list);
  base::Lock lock;
  std::set<const AdblockSharedIndex*> seen;
  std::atomic<int> blocked{0};
  constexpr int kProfiles = 6;
  for (int i = 0; i < kProfiles; ++i) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock()}, base::BindLambdaForTesting([&] {
          auto engine = EngineFor(list);  // each profile parsed its own
          scoped_refptr<const AdblockSharedIndex> index =
              AdblockSharedIndex::GetOrBuild(key, *engine, base::FilePath());
          ASSERT_TRUE(index);
          {
            base::AutoLock hold(lock);
            seen.insert(index.get());
          }
          if (index->ShouldBlock(GURL("https://ads.example/a.js"), Page(),
                                 kTypeScript)) {
            ++blocked;
          }
        }));
  }
  base::ThreadPoolInstance::Get()->FlushForTesting();
  EXPECT_EQ(seen.size(), 1u) << "every profile must end up on the same index";
  EXPECT_EQ(blocked.load(), kProfiles);
}

TEST_F(AdblockSharedIndexTest, TheStarterRulesAreAPartOfTheKey) {
  EXPECT_NE(AdblockSharedIndex::KeyFor("||a.example^\n", "||x.example^\n"),
            AdblockSharedIndex::KeyFor("||a.example^\n", "||y.example^\n"));
  EXPECT_NE(AdblockSharedIndex::KeyFor("||a.example^\n", "||x.example^\n"),
            AdblockSharedIndex::KeyFor("||a.example^\n"));
  EXPECT_EQ(AdblockSharedIndex::KeyFor("||a.example^\n", "||x.example^\n"),
            AdblockSharedIndex::KeyFor("||a.example^\n", "||x.example^\n"));
}

TEST_F(AdblockSharedIndexTest, DifferentListsHaveDifferentKeys) {
  EXPECT_NE(AdblockSharedIndex::KeyFor("||a.example^\n"),
            AdblockSharedIndex::KeyFor("||b.example^\n"));
  EXPECT_NE(AdblockSharedIndex::KeyFor("||a.example^\n"),
            AdblockSharedIndex::KeyFor("||a.example^\n||b.example^\n"));
  EXPECT_EQ(AdblockSharedIndex::KeyFor("||a.example^\n"),
            AdblockSharedIndex::KeyFor("||a.example^\n"));
}

}  // namespace
}  // namespace zephyrus_adblock
