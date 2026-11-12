// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_shared_page_rules.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/scoped_feature_list.h"
#include "base/threading/platform_thread.h"
#include "base/threading/simple_thread.h"
#include "chrome/browser/zephyrus/adblock/adblock_cosmetic_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_csp_filter.h"
#include "chrome/browser/zephyrus/adblock/adblock_param_filter.h"
#include "chrome/browser/zephyrus/adblock/adblock_scriptlet_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_urlskip_filter.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_adblock {
namespace {

// What a profile does when it loads the lists, counted.
struct Builder {
  explicit Builder(std::string rules) : rules(std::move(rules)) {}

  AdblockPageRules operator()() const {
    ++runs;
    auto cosmetic = std::make_unique<AdblockCosmeticEngine>();
    cosmetic->AddRules(rules);
    auto scriptlet = std::make_unique<AdblockScriptletEngine>();
    scriptlet->AddRules(rules);
    auto params = std::make_unique<AdblockParamFilter>();
    params->AddRules(rules);
    auto urlskip = std::make_unique<AdblockUrlSkipFilter>();
    auto csp = std::make_unique<AdblockCspFilter>();
    csp->AddRules(rules);
    AdblockPageRules out;
    out.cosmetic = std::move(cosmetic);
    out.scriptlet = std::move(scriptlet);
    out.params = std::move(params);
    out.urlskip = std::move(urlskip);
    out.csp = std::move(csp);
    return out;
  }

  std::string rules;
  mutable std::atomic<int> runs{0};
};

constexpr char kListA[] = "news.example.org##.ad-slot\n";
constexpr char kListB[] = "news.example.org##.sponsored\n";

std::vector<std::string> HiddenOn(const AdblockPageRules& rules) {
  return rules.cosmetic->GetSelectorsForUrl(GURL("https://news.example.org/"),
                                            /*include_generic=*/false);
}

class AdblockSharedPageRulesTest : public testing::Test {
 protected:
  void SetUp() override { ResetSharedPageRulesForTesting(); }
  void TearDown() override { ResetSharedPageRulesForTesting(); }
};

TEST_F(AdblockSharedPageRulesTest, TheSecondProfileTakesTheFirstOnesCopy) {
  Builder build(kListA);
  const AdblockPageRules first = GetOrBuildSharedPageRules(7, build);
  const AdblockPageRules second = GetOrBuildSharedPageRules(7, build);

  // Parsed once, and the very same objects: no second 19 MB.
  EXPECT_EQ(build.runs.load(), 1);
  EXPECT_EQ(first.cosmetic.get(), second.cosmetic.get());
  EXPECT_EQ(first.scriptlet.get(), second.scriptlet.get());
  EXPECT_EQ(first.params.get(), second.params.get());
  EXPECT_EQ(first.urlskip.get(), second.urlskip.get());
  EXPECT_EQ(first.csp.get(), second.csp.get());
  // And they are the rules that were asked for.
  ASSERT_EQ(HiddenOn(second).size(), 1u);
  EXPECT_EQ(HiddenOn(second)[0], ".ad-slot");
}

TEST_F(AdblockSharedPageRulesTest, ANewListIsBuiltAndTheOldOneOutlivesIt) {
  Builder build_a(kListA);
  Builder build_b(kListB);
  const AdblockPageRules old_rules = GetOrBuildSharedPageRules(7, build_a);
  const AdblockPageRules new_rules = GetOrBuildSharedPageRules(8, build_b);

  EXPECT_EQ(build_b.runs.load(), 1);
  EXPECT_NE(old_rules.cosmetic.get(), new_rules.cosmetic.get());
  ASSERT_EQ(HiddenOn(new_rules).size(), 1u);
  EXPECT_EQ(HiddenOn(new_rules)[0], ".sponsored");
  // A profile that has not reloaded yet still reads the list it was given,
  // not freed memory and not the new list.
  ASSERT_EQ(HiddenOn(old_rules).size(), 1u);
  EXPECT_EQ(HiddenOn(old_rules)[0], ".ad-slot");
}

TEST_F(AdblockSharedPageRulesTest, KeyZeroNeverShares) {
  Builder build(kListA);
  const AdblockPageRules first = GetOrBuildSharedPageRules(0, build);
  const AdblockPageRules second = GetOrBuildSharedPageRules(0, build);
  EXPECT_EQ(build.runs.load(), 2);
  EXPECT_NE(first.cosmetic.get(), second.cosmetic.get());
}

TEST_F(AdblockSharedPageRulesTest, TheKillSwitchGivesEveryProfileItsOwn) {
  base::test::ScopedFeatureList off;
  off.InitAndDisableFeature(kZephyrusAdblockSharedPageRules);
  Builder build(kListA);
  const AdblockPageRules first = GetOrBuildSharedPageRules(7, build);
  const AdblockPageRules second = GetOrBuildSharedPageRules(7, build);
  EXPECT_EQ(build.runs.load(), 2);
  EXPECT_NE(first.cosmetic.get(), second.cosmetic.get());
  EXPECT_EQ(HiddenOn(second).size(), 1u);
}

// Profiles opening at the same moment.
class Opener : public base::SimpleThread {
 public:
  Opener(Builder* build, base::WaitableEvent* go)
      : base::SimpleThread("opener"), build_(build), go_(go) {}

  void Run() override {
    go_->Wait();
    rules_ = GetOrBuildSharedPageRules(7, *build_);
    // Read it from this thread too, as a profile would.
    hidden_ = HiddenOn(rules_).size();
  }

  const AdblockPageRules& rules() const { return rules_; }
  size_t hidden() const { return hidden_; }

 private:
  raw_ptr<Builder> build_;
  raw_ptr<base::WaitableEvent> go_;
  AdblockPageRules rules_;
  size_t hidden_ = 0;
};

TEST_F(AdblockSharedPageRulesTest, ProfilesOpeningTogetherBuildItOnce) {
  Builder build(kListA);
  base::WaitableEvent go(base::WaitableEvent::ResetPolicy::MANUAL,
                         base::WaitableEvent::InitialState::NOT_SIGNALED);
  std::vector<std::unique_ptr<Opener>> openers;
  for (int i = 0; i < 8; ++i) {
    openers.push_back(std::make_unique<Opener>(&build, &go));
    openers.back()->Start();
  }
  go.Signal();
  for (auto& opener : openers) {
    opener->Join();
  }
  EXPECT_EQ(build.runs.load(), 1);
  for (auto& opener : openers) {
    EXPECT_EQ(opener->rules().cosmetic.get(),
              openers[0]->rules().cosmetic.get());
    EXPECT_EQ(opener->hidden(), 1u);
  }
}

}  // namespace
}  // namespace zephyrus_adblock
