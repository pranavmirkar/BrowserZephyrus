// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_THREAT_DATA_H_
#define CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_THREAT_DATA_H_

#include <memory>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "chrome/browser/zephyrus/threat/zephyrus_threat_index.h"

namespace zephyrus_threat {

// The two lists the threat check reads. Immutable once loaded.
struct ZephyrusThreatData {
  // Known-bad hosts and addresses (phishing and malware feeds).
  ZephyrusThreatIndex threats;
  // Popular sites, which Shield DNS never asks about: a host on this list is
  // not looked up anywhere.
  ZephyrusThreatIndex popular;
};

inline constexpr char kThreatsFileName[] = "zephyrus_threats.bin";
inline constexpr char kPopularFileName[] = "zephyrus_popular.bin";

// Reads the two blobs on a thread pool task that may block and runs `callback`
// with the result on the calling sequence. A blob in `data_dir` (the profile's
// updated copy) wins over the one shipped in `assets_dir`. A missing or
// invalid blob leaves its list empty, which means "nothing is known", never
// "everything is listed".
void LoadThreatData(
    const base::FilePath& assets_dir,
    const base::FilePath& data_dir,
    base::OnceCallback<void(std::unique_ptr<ZephyrusThreatData>)> callback);

}  // namespace zephyrus_threat

#endif  // CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_THREAT_DATA_H_
