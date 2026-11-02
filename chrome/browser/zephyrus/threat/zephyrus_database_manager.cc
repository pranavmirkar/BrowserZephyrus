// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/threat/zephyrus_database_manager.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "components/safe_browsing/core/browser/db/util.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"

namespace zephyrus_threat {

namespace {

using safe_browsing::SBThreatType;
using safe_browsing::SBThreatTypeSet;

// What a hit is reported as. The feeds and the filtering resolver say only
// "bad", so phishing's warning ("deceptive site") is the honest general one;
// malware is used only when that is all the caller asked about.
std::optional<SBThreatType> ThreatToReport(const SBThreatTypeSet& types) {
  if (types.contains(SBThreatType::SB_THREAT_TYPE_URL_PHISHING)) {
    return SBThreatType::SB_THREAT_TYPE_URL_PHISHING;
  }
  if (types.contains(SBThreatType::SB_THREAT_TYPE_URL_MALWARE)) {
    return SBThreatType::SB_THREAT_TYPE_URL_MALWARE;
  }
  return std::nullopt;
}

}  // namespace

// static
scoped_refptr<ZephyrusDatabaseManager> ZephyrusDatabaseManager::Create(
    const base::FilePath& base_path,
    scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
    scoped_refptr<base::SequencedTaskRunner> io_task_runner,
    safe_browsing::ExtendedReportingLevelCallback
        extended_reporting_level_callback,
    base::FilePath assets_dir,
    base::FilePath data_dir,
    base::RepeatingCallback<bool()> shield_dns_enabled) {
  return base::WrapRefCounted(new ZephyrusDatabaseManager(
      base_path, std::move(extended_reporting_level_callback),
      std::move(ui_task_runner), std::move(io_task_runner),
      std::move(assets_dir), std::move(data_dir),
      std::move(shield_dns_enabled)));
}

ZephyrusDatabaseManager::ZephyrusDatabaseManager(
    const base::FilePath& base_path,
    safe_browsing::ExtendedReportingLevelCallback
        extended_reporting_level_callback,
    scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
    scoped_refptr<base::SequencedTaskRunner> io_task_runner,
    base::FilePath assets_dir,
    base::FilePath data_dir,
    base::RepeatingCallback<bool()> shield_dns_enabled)
    : V4LocalDatabaseManager(base_path,
                             std::move(extended_reporting_level_callback),
                             std::move(ui_task_runner),
                             std::move(io_task_runner),
                             /*task_runner_for_tests=*/nullptr),
      assets_dir_(std::move(assets_dir)),
      data_dir_(std::move(data_dir)),
      shield_dns_enabled_(std::move(shield_dns_enabled)) {}

ZephyrusDatabaseManager::~ZephyrusDatabaseManager() = default;

void ZephyrusDatabaseManager::StartOnUIThread(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const safe_browsing::V4ProtocolConfig& config) {
  shield_ = std::make_unique<ShieldDnsChecker>(url_loader_factory);
  if (!data_) {
    LoadThreatData(assets_dir_, data_dir_,
                   base::BindOnce(&ZephyrusDatabaseManager::OnDataLoaded,
                                  weak_factory_.GetWeakPtr()));
  }
  V4LocalDatabaseManager::StartOnUIThread(std::move(url_loader_factory), config);
}

void ZephyrusDatabaseManager::StopOnUIThread(bool shutdown) {
  shield_.reset();
  ours_pending_.clear();
  weak_factory_.InvalidateWeakPtrs();
  V4LocalDatabaseManager::StopOnUIThread(shutdown);
}

void ZephyrusDatabaseManager::OnDataLoaded(
    std::unique_ptr<ZephyrusThreatData> data) {
  data_ = std::move(data);
}

bool ZephyrusDatabaseManager::ShouldAskShield(const GURL& url) const {
  // Until the popular-sites list has loaded nothing is asked: "unknown" must
  // never mean "ask about everything".
  return shield_ && data_ && data_->popular.size() > 0 &&
         shield_dns_enabled_ && shield_dns_enabled_.Run() &&
         ShieldDnsChecker::IsEligibleHost(url.host()) &&
         !data_->popular.IsListed(url);
}

bool ZephyrusDatabaseManager::CheckBrowseUrl(
    const GURL& url,
    const SBThreatTypeSet& threat_types,
    Client* client,
    safe_browsing::CheckBrowseUrlType check_type) {
  const std::optional<SBThreatType> report = ThreatToReport(threat_types);
  if (!report || !CanCheckUrl(url) ||
      check_type != safe_browsing::CheckBrowseUrlType::kHashDatabase) {
    return V4LocalDatabaseManager::CheckBrowseUrl(url, threat_types, client,
                                                  check_type);
  }

  // 1. The local index: instant, nothing leaves the machine.
  if (data_ && data_->threats.IsListed(url)) {
    VLOG(1) << "Zephyrus threat list: " << url.host();
    ReportLater(client, url, *report);
    return false;
  }

  // 2. Shield DNS, if the user turned it on and the site is not a popular one.
  if (ShouldAskShield(url)) {
    ours_pending_.insert(client);
    shield_->Check(
        std::string(url.host()),
        base::BindOnce(&ZephyrusDatabaseManager::OnShieldVerdict,
                       weak_factory_.GetWeakPtr(), client, url, threat_types,
                       check_type, *report));
    return false;
  }

  return V4LocalDatabaseManager::CheckBrowseUrl(url, threat_types, client,
                                                check_type);
}

void ZephyrusDatabaseManager::OnShieldVerdict(
    Client* client,
    GURL url,
    SBThreatTypeSet threat_types,
    safe_browsing::CheckBrowseUrlType check_type,
    SBThreatType threat_if_blocked,
    ShieldDnsChecker::Verdict verdict) {
  if (ours_pending_.erase(client) == 0) {
    return;  // cancelled while the answer was on its way
  }
  if (verdict == ShieldDnsChecker::Verdict::kBlocked) {
    VLOG(1) << "Zephyrus Shield DNS blocked: " << url.host();
    client->OnCheckBrowseUrlResult(url, threat_if_blocked,
                                   safe_browsing::ThreatMetadata());
    return;
  }
  // Not blocked (or no answer): the stock manager gets its turn, so a build with
  // Google's lists loses nothing by also having ours.
  if (V4LocalDatabaseManager::CheckBrowseUrl(url, threat_types, client,
                                             check_type)) {
    client->OnCheckBrowseUrlResult(url, SBThreatType::SB_THREAT_TYPE_SAFE,
                                   safe_browsing::ThreatMetadata());
  }
}

void ZephyrusDatabaseManager::ReportLater(Client* client,
                                          const GURL& url,
                                          SBThreatType type) {
  ours_pending_.insert(client);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ZephyrusDatabaseManager::Deliver,
                                weak_factory_.GetWeakPtr(), client, url, type));
}

void ZephyrusDatabaseManager::Deliver(Client* client,
                                      const GURL& url,
                                      SBThreatType type) {
  if (ours_pending_.erase(client) == 0) {
    return;
  }
  client->OnCheckBrowseUrlResult(url, type, safe_browsing::ThreatMetadata());
}

void ZephyrusDatabaseManager::CancelCheck(Client* client) {
  if (ours_pending_.erase(client) > 0) {
    return;  // never reached the stock manager
  }
  V4LocalDatabaseManager::CancelCheck(client);
}

}  // namespace zephyrus_threat
