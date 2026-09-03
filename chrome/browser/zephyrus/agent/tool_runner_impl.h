// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_TOOL_RUNNER_IMPL_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_TOOL_RUNNER_IMPL_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/zephyrus/agent/tool_executor.h"
#include "chrome/services/zephyrus_agent/public/mojom/agent_kernel.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"

namespace zephyrus::agent {

// The browser, as the running task sees it.
//
// Handed to the kernel for the length of one task so its loop can look at the
// page and act on it. Everything goes through ToolExecutor, which means it goes
// through the kernel's own policy check on the way -- the loop does not get a
// private door. Policy is enforced in exactly one place regardless of who
// asked, and that place is not here.
class ToolRunnerImpl : public mojom::ToolRunner {
 public:
  // Watches the task go past.
  //
  // No new plumbing was needed for this: the browser is the one PERFORMING
  // every step, so it already knows about each one. A progress channel back
  // through the kernel would have been a second source of truth for something
  // this side already has.
  class Observer {
   public:
    virtual ~Observer() = default;
    virtual void OnAgentLooked() {}
    // `target` names the thing being acted on in the user's terms -- the
    // element's accessible name, or the destination URL. Empty when the call
    // does not point at anything. "Clicking something" is not a useful thing
    // to read while a browser clicks things on its own.
    virtual void OnAgentToolStarted(const std::string& tool,
                                    const std::string& arguments_json,
                                    const std::string& target) {}
    virtual void OnAgentToolFinished(const std::string& tool,
                                     const mojom::ToolOutcome& outcome) {}
  };

  // `executor` must outlive this. `task` is the user's own words, forwarded so
  // the kernel can judge a destination against what was actually asked for.
  ToolRunnerImpl(ToolExecutor* executor, std::string task);
  ~ToolRunnerImpl() override;

  ToolRunnerImpl(const ToolRunnerImpl&) = delete;
  ToolRunnerImpl& operator=(const ToolRunnerImpl&) = delete;

  // Not owned; must outlive this.
  void SetObserver(Observer* observer) { observer_ = observer; }

  mojo::PendingRemote<mojom::ToolRunner> BindNewPipeAndPassRemote();

  // mojom::ToolRunner:
  void Observe(int32_t level, ObserveCallback callback) override;
  void Execute(const std::string& tool,
               const std::string& arguments_json,
               ExecuteCallback callback) override;
  void ExecuteApproved(const std::string& tool,
                       const std::string& arguments_json,
                       ExecuteApprovedCallback callback) override;

 private:
  void OnFinished(std::string tool,
                  ExecuteCallback callback,
                  ToolExecutor::Result result);

  raw_ptr<ToolExecutor> executor_;
  raw_ptr<Observer> observer_ = nullptr;
  const std::string task_;
  mojo::Receiver<mojom::ToolRunner> receiver_{this};
  base::WeakPtrFactory<ToolRunnerImpl> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_TOOL_RUNNER_IMPL_H_
