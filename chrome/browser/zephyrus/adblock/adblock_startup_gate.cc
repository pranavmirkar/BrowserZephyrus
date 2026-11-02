// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_startup_gate.h"

#include <utility>

#include "base/logging.h"

namespace zephyrus_adblock {

BASE_FEATURE(kZephyrusAdblockHoldUntilReady,
             "ZephyrusAdblockHoldUntilReady",
             base::FEATURE_ENABLED_BY_DEFAULT);

AdblockStartupGate::AdblockStartupGate(bool holding, base::TimeDelta timeout)
    : open_(!holding), started_(base::TimeTicks::Now()) {
  if (holding) {
    // The fail-open: a list that never loads must not stop browsing.
    timeout_timer_.Start(FROM_HERE, timeout,
                         base::BindOnce(&AdblockStartupGate::OnTimeout,
                                        base::Unretained(this)));
  }
}

AdblockStartupGate::~AdblockStartupGate() = default;

void AdblockStartupGate::RunWhenOpen(base::OnceClosure callback) {
  // While a release is in progress a newcomer joins the end of the queue, so
  // requests start in the order they arrived even if a released one asks for
  // another.
  if (open_ && !releasing_) {
    std::move(callback).Run();
    return;
  }
  waiting_.push_back(std::move(callback));
}

void AdblockStartupGate::Open() {
  Release(/*timed_out=*/false);
}

void AdblockStartupGate::OnTimeout() {
  Release(/*timed_out=*/true);
}

void AdblockStartupGate::Release(bool timed_out) {
  if (open_) {
    return;
  }
  open_ = true;
  timeout_timer_.Stop();
  size_t released_count = 0;
  releasing_ = true;
  while (!waiting_.empty()) {
    std::vector<base::OnceClosure> batch = std::move(waiting_);
    waiting_.clear();
    released_count += batch.size();
    for (base::OnceClosure& callback : batch) {
      std::move(callback).Run();
    }
  }
  releasing_ = false;
  VLOG(1) << "[Zephyrus] adblock: startup gate opened after "
          << (base::TimeTicks::Now() - started_).InMilliseconds() << " ms"
          << (timed_out ? " (TIMED OUT; the full list was not ready)"
                        : " (full list ready)")
          << ", released " << released_count << " held request(s)";
}

}  // namespace zephyrus_adblock
