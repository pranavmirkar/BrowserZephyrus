// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_LOCAL_VISION_CLIENT_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_LOCAL_VISION_CLIENT_H_

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"

namespace network {
class SimpleURLLoader;
}  // namespace network

namespace zephyrus::agent {

// Which model on the local server can see.
//
//   --zephyrus-agent-vision-model=moondream
//
// A separate switch from the reasoning model on purpose: they are different
// models with different costs, and the small one that looks at pictures is not
// the one that should be deciding what to do.
inline constexpr char kAgentVisionModelSwitch[] = "zephyrus-agent-vision-model";

// Turns a picture of the page into a sentence or two, on this machine.
//
// This is the whole point of the design, so it is worth stating plainly: the
// screenshot goes to a model running on the user's own computer, and what
// leaves this class is TEXT. The image itself never reaches the cloud, never
// crosses a process boundary, and never touches the network beyond loopback.
//
// Redaction is not what makes that safe -- it is defence in depth. The picture
// is masked before it is ever encoded, so even this local model cannot read a
// password or a card number out of it, and therefore cannot repeat one into a
// description that does travel.
//
// Compare with sending the screenshot to a cloud model directly: that is a
// filter with a miss rate, and one miss puts real pixels of someone's bank page
// on someone else's server, permanently. Describing it locally removes the
// channel rather than policing it.
//
// The cost, stated honestly: whatever the small local model fails to notice,
// the big model can never recover. That is why this is a SECOND channel. The
// accessibility tree still carries the precise, addressable truth about what is
// on the page; this carries only what the tree cannot describe.
class LocalVisionClient {
 public:
  // Null unless the endpoint, the vision switch and a vision model are all
  // present AND the endpoint is loopback. Null is the ordinary case.
  static std::unique_ptr<LocalVisionClient> CreateIfConfigured(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);

  LocalVisionClient(GURL endpoint,
                    std::string model,
                    scoped_refptr<network::SharedURLLoaderFactory> factory);
  ~LocalVisionClient();

  LocalVisionClient(const LocalVisionClient&) = delete;
  LocalVisionClient& operator=(const LocalVisionClient&) = delete;

  // Describes `jpeg`. The reply is a short plain-text description, or empty.
  //
  // Empty on every failure, deliberately. A page the local model could not
  // describe is a page the agent still has an accessibility tree for, and a
  // task that stopped because a description was unavailable would be worse than
  // one that carried on with the channel that works.
  using DescribeCallback = base::OnceCallback<void(std::string)>;
  void Describe(const std::vector<uint8_t>& jpeg, DescribeCallback callback);

 private:
  using LoaderList = std::list<std::unique_ptr<network::SimpleURLLoader>>;

  void OnResponse(LoaderList::iterator loader,
                  DescribeCallback callback,
                  std::optional<std::string> body);

  const GURL endpoint_;
  const std::string model_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  LoaderList loaders_;
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_LOCAL_VISION_CLIENT_H_
