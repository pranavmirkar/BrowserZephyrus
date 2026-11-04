// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_DATABASE_MANAGER_H_
#define CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_DATABASE_MANAGER_H_

#include <memory>
#include <set>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/zephyrus/threat/zephyrus_shield_dns.h"
#include "chrome/browser/zephyrus/threat/zephyrus_threat_data.h"
#include "components/safe_browsing/core/browser/db/sb_local_database_manager.h"

namespace zephyrus_threat {

// Chromium's Safe Browsing database manager, plus two checks of ours that need
// no Google account or API key (this build has none, so stock Safe Browsing
// finds nothing):
//
//  1. A local index of known-bad hosts and addresses built from open feeds.
//     Instant and sends nothing anywhere.
//  2. Shield DNS, only when the user turns it on: a free filtering resolver is
//     asked about the host names of sites that are not popular.
//
// A hit is reported through the stock path as a phishing result, so Chromium's
// own warning page, "proceed anyway" and per-site bypass all work unchanged.
// When neither check finds anything the request goes to the stock manager, so a
// build that later gains Google's lists uses both.
class ZephyrusDatabaseManager : public safe_browsing::SBLocalDatabaseManager {
 public:
  static scoped_refptr<ZephyrusDatabaseManager> Create(
      const base::FilePath& base_path,
      scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
      scoped_refptr<base::SequencedTaskRunner> io_task_runner,
      safe_browsing::ExtendedReportingLevelCallback
          extended_reporting_level_callback,
      base::FilePath assets_dir,
      base::FilePath data_dir,
      base::RepeatingCallback<bool()> shield_dns_enabled);

  // SafeBrowsingDatabaseManager:
  bool CheckBrowseUrl(const GURL& url,
                      const safe_browsing::SBThreatTypeSet& threat_types,
                      Client* client,
                      safe_browsing::CheckBrowseUrlType check_type) override;
  void CancelCheck(Client* client) override;
  void StartOnUIThread(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      const safe_browsing::SBProtocolConfig& config) override;
  void StopOnUIThread(bool shutdown) override;

 protected:
  ZephyrusDatabaseManager(
      const base::FilePath& base_path,
      safe_browsing::ExtendedReportingLevelCallback
          extended_reporting_level_callback,
      scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
      scoped_refptr<base::SequencedTaskRunner> io_task_runner,
      base::FilePath assets_dir,
      base::FilePath data_dir,
      base::RepeatingCallback<bool()> shield_dns_enabled);
  ~ZephyrusDatabaseManager() override;

 private:
  void OnDataLoaded(std::unique_ptr<ZephyrusThreatData> data);
  // Reports `type` for `url` to `client` from a posted task (a client is never
  // called before CheckBrowseUrl returns), unless the check was cancelled.
  void ReportLater(Client* client,
                   const GURL& url,
                   safe_browsing::SBThreatType type);
  void Deliver(uintptr_t client_address,
               const GURL& url,
               safe_browsing::SBThreatType type);
  void OnShieldVerdict(uintptr_t client_address,
                       GURL url,
                       safe_browsing::SBThreatTypeSet threat_types,
                       safe_browsing::CheckBrowseUrlType check_type,
                       safe_browsing::SBThreatType threat_if_blocked,
                       ShieldDnsChecker::Verdict verdict);
  bool ShouldAskShield(const GURL& url) const;

  base::FilePath assets_dir_;
  base::FilePath data_dir_;
  base::RepeatingCallback<bool()> shield_dns_enabled_;
  std::unique_ptr<ZephyrusThreatData> data_;
  std::unique_ptr<ShieldDnsChecker> shield_;
  // Clients whose check has not reached the stock manager yet; CancelCheck()
  // must forget them here, because the stock manager has never heard of them.
  std::set<Client*> ours_pending_;
  base::WeakPtrFactory<ZephyrusDatabaseManager> weak_factory_{this};
};

}  // namespace zephyrus_threat

#endif  // CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_DATABASE_MANAGER_H_
