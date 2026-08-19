// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §9.1, the harvest step.
//
// This is the half of CNAME uncloaking that could not be verified against live
// sites. Every *known* cloaked host — smetrics.walmart.com, metrics.cnn.com and
// the rest — is blocked by hostname by the filter lists, and a blocked request
// is cancelled before it is ever resolved, so no canonical name exists to read.
// The uncloaker therefore only ever runs on cloaking the lists do NOT know,
// which is precisely the set there is no ready list of to test against.
//
// So the DNS answer is supplied directly here. `URLResponseHead::dns_aliases`
// is the only input the harvester takes, which makes a fabricated response an
// exact stand-in for a real resolution rather than an approximation of one.

#include "chrome/browser/zephyrus/adblock/zephyrus_cname_harvester.h"

#include <string>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "base/test/task_environment.h"
#include "chrome/browser/zephyrus/privacy/privacy_cname_cache.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_adblock {
namespace {

using zephyrus_privacy::PrivacyCnameCache;

// The loader client the harvester wraps. Records that the response was passed
// through, because a harvester that swallowed responses would break every page
// while still passing a cache assertion.
class RecordingClient : public network::mojom::URLLoaderClient {
 public:
  mojo::PendingRemote<network::mojom::URLLoaderClient> Bind() {
    return receiver_.BindNewPipeAndPassRemote();
  }
  int responses() const { return responses_; }

  void OnReceiveEarlyHints(network::mojom::EarlyHintsPtr) override {}
  void OnReceiveResponse(network::mojom::URLResponseHeadPtr,
                         mojo::ScopedDataPipeConsumerHandle,
                         std::optional<mojo_base::BigBuffer>) override {
    ++responses_;
  }
  void OnReceiveRedirect(const net::RedirectInfo&,
                         network::mojom::URLResponseHeadPtr) override {}
  void OnUploadProgress(int64_t,
                        int64_t,
                        OnUploadProgressCallback callback) override {
    std::move(callback).Run();
  }
  void OnTransferSizeUpdated(int32_t) override {}
  void OnComplete(const network::URLLoaderCompletionStatus&) override {}

 private:
  int responses_ = 0;
  mojo::Receiver<network::mojom::URLLoaderClient> receiver_{this};
};

class ZephyrusCnameHarvesterTest : public testing::Test {
 protected:
  // Drives one response with `aliases` through a harvester watching `host`.
  void Harvest(const std::string& host,
               const std::vector<std::string>& aliases) {
    mojo::Remote<network::mojom::URLLoaderClient> harvester(
        ZephyrusCnameHarvester::Create(downstream_.Bind(), cache_, host));
    auto head = network::mojom::URLResponseHead::New();
    head->dns_aliases = aliases;
    harvester->OnReceiveResponse(std::move(head),
                                 mojo::ScopedDataPipeConsumerHandle(),
                                 std::nullopt);
    harvester.FlushForTesting();
    task_environment_.RunUntilIdle();
  }

  base::test::TaskEnvironment task_environment_;
  scoped_refptr<PrivacyCnameCache> cache_ =
      base::MakeRefCounted<PrivacyCnameCache>();
  RecordingClient downstream_;
};

// The case §9.1 exists for: a first-party-looking host that resolves into
// someone else's registrable domain. Without this the request reads as
// first-party and the page reports itself as clean.
TEST_F(ZephyrusCnameHarvesterTest, LearnsTheCanonicalDomainOfACloakedHost) {
  Harvest("metrics.example.com", {"example.com.ssl.sc.omtrdc.net"});

  const std::optional<std::string> canonical =
      cache_->CanonicalEtld1("metrics.example.com");
  ASSERT_TRUE(canonical.has_value());
  EXPECT_EQ("omtrdc.net", *canonical);
}

// Learning is what makes the second request cheap: §9.1 requires classification
// be one lookup after the first request, and NeedsResolution() is the gate that
// stops a harvester being attached again.
TEST_F(ZephyrusCnameHarvesterTest, AHarvestedHostNeedsNoSecondResolution) {
  EXPECT_TRUE(cache_->NeedsResolution("metrics.example.com"));
  Harvest("metrics.example.com", {"tracker-co.net"});
  EXPECT_FALSE(cache_->NeedsResolution("metrics.example.com"));
}

// A company pointing its own subdomain at its own infrastructure is ordinary
// hosting. Recording a canonical here would invent a third party that is not
// there — the false positive §16 budgets zero of.
TEST_F(ZephyrusCnameHarvesterTest, SameCompanyAliasIsNotRecordedAsCloaking) {
  Harvest("images.example.com", {"cdn.example.com"});

  EXPECT_FALSE(cache_->CanonicalEtld1("images.example.com").has_value());
  // Still remembered as resolved, so it is not re-harvested forever.
  EXPECT_FALSE(cache_->NeedsResolution("images.example.com"));
}

TEST_F(ZephyrusCnameHarvesterTest, NoAliasesRecordsNothing) {
  Harvest("plain.example.com", {});
  EXPECT_FALSE(cache_->CanonicalEtld1("plain.example.com").has_value());
}

// The harvester sits in the response path of every first sighting of a host.
// If it ever failed to forward, pages would hang rather than mis-report, so
// this is checked on the same path as the recording itself.
TEST_F(ZephyrusCnameHarvesterTest, ForwardsTheResponseDownstream) {
  Harvest("metrics.example.com", {"tracker-co.net"});
  EXPECT_EQ(1, downstream_.responses());
}

}  // namespace
}  // namespace zephyrus_adblock
