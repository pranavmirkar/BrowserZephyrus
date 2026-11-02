// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_STARTUP_GATE_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_STARTUP_GATE_H_

#include <cstddef>
#include <vector>

#include "base/feature_list.h"
#include "base/functional/callback.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

namespace zephyrus_adblock {

// Whether the first requests of a session wait for the full filter list.
// On by default; `--disable-features=ZephyrusAdblockHoldUntilReady` is the
// kill switch.
BASE_DECLARE_FEATURE(kZephyrusAdblockHoldUntilReady);

// The startup gap, closed.
//
// For the first few hundred milliseconds of a session only the small starter
// list is in force, because the full list is read and parsed off-thread. A
// restored tab fires its requests in exactly that window, so its trackers
// loaded before the blocker knew about them: measured, 5 of 600 test requests
// blocked on a relaunch with the whole list not yet loaded.
//
// This gate holds requests until the lists are ready, the way uBlock Origin
// suspends them, and releases them in order. It never holds longer than
// `timeout`: if the lists cannot load, protection falls back to the starter
// list (what happened before) and browsing goes on.
//
// UI thread only.
class AdblockStartupGate {
 public:
  // `holding` false (the feature is off, or the blocker is) starts it open.
  AdblockStartupGate(bool holding, base::TimeDelta timeout);
  AdblockStartupGate(const AdblockStartupGate&) = delete;
  AdblockStartupGate& operator=(const AdblockStartupGate&) = delete;
  ~AdblockStartupGate();

  bool is_open() const { return open_; }
  // How many callbacks are waiting.
  size_t waiting() const { return waiting_.size(); }

  // Runs `callback` now if the gate is open, otherwise when it opens, in the
  // order the callbacks arrived.
  void RunWhenOpen(base::OnceClosure callback);

  // The lists are ready. Releases everything that is waiting. Idempotent.
  void Open();

 private:
  void OnTimeout();
  void Release(bool timed_out);

  bool open_;
  bool releasing_ = false;
  base::TimeTicks started_;
  base::OneShotTimer timeout_timer_;
  std::vector<base::OnceClosure> waiting_;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_STARTUP_GATE_H_
