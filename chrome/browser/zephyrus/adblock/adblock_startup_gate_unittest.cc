// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_startup_gate.h"

#include <string>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_adblock {
namespace {

class AdblockStartupGateTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(AdblockStartupGateTest, RequestsWaitUntilTheListsAreReadyAndGoInOrder) {
  AdblockStartupGate gate(/*holding=*/true, base::Seconds(2));
  std::string order;
  gate.RunWhenOpen(base::BindLambdaForTesting([&] { order += "a"; }));
  gate.RunWhenOpen(base::BindLambdaForTesting([&] { order += "b"; }));
  gate.RunWhenOpen(base::BindLambdaForTesting([&] { order += "c"; }));
  EXPECT_FALSE(gate.is_open());
  EXPECT_EQ(gate.waiting(), 3u);
  EXPECT_EQ(order, "") << "nothing may start before the lists are ready";

  gate.Open();
  EXPECT_TRUE(gate.is_open());
  EXPECT_EQ(order, "abc");
  EXPECT_EQ(gate.waiting(), 0u);
}

TEST_F(AdblockStartupGateTest, AnOpenGateRunsAtOnce) {
  AdblockStartupGate gate(/*holding=*/true, base::Seconds(2));
  gate.Open();
  bool ran = false;
  gate.RunWhenOpen(base::BindLambdaForTesting([&] { ran = true; }));
  EXPECT_TRUE(ran);
}

TEST_F(AdblockStartupGateTest, ANotHoldingGateStartsOpen) {
  AdblockStartupGate gate(/*holding=*/false, base::Seconds(2));
  EXPECT_TRUE(gate.is_open());
  bool ran = false;
  gate.RunWhenOpen(base::BindLambdaForTesting([&] { ran = true; }));
  EXPECT_TRUE(ran);
}

TEST_F(AdblockStartupGateTest, ItNeverHoldsLongerThanTheTimeout) {
  // The fail-open: if the lists never load, browsing goes on.
  AdblockStartupGate gate(/*holding=*/true, base::Seconds(2));
  bool ran = false;
  gate.RunWhenOpen(base::BindLambdaForTesting([&] { ran = true; }));
  task_environment_.FastForwardBy(base::Milliseconds(1999));
  EXPECT_FALSE(ran);
  EXPECT_FALSE(gate.is_open());
  task_environment_.FastForwardBy(base::Milliseconds(2));
  EXPECT_TRUE(ran) << "the held request must be released at the timeout";
  EXPECT_TRUE(gate.is_open());
}

TEST_F(AdblockStartupGateTest, OpeningTwiceOrAfterTheTimeoutDoesNothingMore) {
  AdblockStartupGate gate(/*holding=*/true, base::Seconds(2));
  int runs = 0;
  gate.RunWhenOpen(base::BindLambdaForTesting([&] { ++runs; }));
  gate.Open();
  gate.Open();
  task_environment_.FastForwardBy(base::Seconds(5));
  EXPECT_EQ(runs, 1) << "a request is started exactly once";
}

TEST_F(AdblockStartupGateTest, AHeldRequestWhoseOwnerIsGoneIsSkipped) {
  // A held request is bound to its factory by a weak pointer; the factory dying
  // (its page closed) must make the release a no-op, not a crash.
  struct Owner {
    int started = 0;
    void Start() { ++started; }
    base::WeakPtrFactory<Owner> weak_factory{this};
  };
  AdblockStartupGate gate(/*holding=*/true, base::Seconds(2));
  auto live = std::make_unique<Owner>();
  auto dead = std::make_unique<Owner>();
  gate.RunWhenOpen(
      base::BindOnce(&Owner::Start, live->weak_factory.GetWeakPtr()));
  gate.RunWhenOpen(
      base::BindOnce(&Owner::Start, dead->weak_factory.GetWeakPtr()));
  dead.reset();
  gate.Open();
  EXPECT_EQ(live->started, 1);
}

TEST_F(AdblockStartupGateTest, ARequestMadeDuringTheReleaseGoesToTheBack) {
  AdblockStartupGate gate(/*holding=*/true, base::Seconds(2));
  std::string order;
  gate.RunWhenOpen(base::BindLambdaForTesting([&] {
    order += "a";
    gate.RunWhenOpen(base::BindLambdaForTesting([&] { order += "x"; }));
  }));
  gate.RunWhenOpen(base::BindLambdaForTesting([&] { order += "b"; }));
  gate.Open();
  EXPECT_EQ(order, "abx") << "requests start in the order they arrived";
}

}  // namespace
}  // namespace zephyrus_adblock
