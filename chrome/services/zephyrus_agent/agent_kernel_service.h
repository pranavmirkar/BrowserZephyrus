// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_SERVICES_ZEPHYRUS_AGENT_AGENT_KERNEL_SERVICE_H_
#define CHROME_SERVICES_ZEPHYRUS_AGENT_AGENT_KERNEL_SERVICE_H_

#include "chrome/services/zephyrus_agent/kernel/src/lib.rs.h"
#include "chrome/services/zephyrus_agent/public/mojom/agent_kernel.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "third_party/rust/cxx/v1/cxx.h"

namespace zephyrus::agent {

// Hosts the Rust agent kernel in a sandboxed utility process.
//
// This class is deliberately thin. It translates mojo structs into the cxx
// bridge types and back, and holds no policy of its own -- every decision comes
// from the Rust side, which owns the compiled-in tool contract. If this file
// ever grows a rule about what the agent may do, that rule is in the wrong
// place.
class AgentKernelService : public mojom::AgentKernel {
 public:
  explicit AgentKernelService(
      mojo::PendingReceiver<mojom::AgentKernel> receiver);
  ~AgentKernelService() override;

  AgentKernelService(const AgentKernelService&) = delete;
  AgentKernelService& operator=(const AgentKernelService&) = delete;

  // mojom::AgentKernel:
  void GetContractInfo(GetContractInfoCallback callback) override;
  void Decide(mojom::PolicyRequestPtr request,
              DecideCallback callback) override;
  void RunTask(const std::string& task,
               mojo::PendingRemote<mojom::ToolRunner> runner,
               mojo::PendingRemote<mojom::AgentModel> model,
               uint32_t max_steps,
               mojom::PendingApprovalPtr approved,
               RunTaskCallback callback) override;

 private:
  mojo::Receiver<mojom::AgentKernel> receiver_;

  // Built once at construction. A kernel that failed to load its contract
  // reports an empty version and denies every call, which is the only safe
  // reading of "I do not know what the rules are".
  ::rust::Box<Kernel> kernel_;
};

}  // namespace zephyrus::agent

#endif  // CHROME_SERVICES_ZEPHYRUS_AGENT_AGENT_KERNEL_SERVICE_H_
