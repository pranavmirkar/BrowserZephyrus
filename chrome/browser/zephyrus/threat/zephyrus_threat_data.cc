// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/threat/zephyrus_threat_data.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/zephyrus/buildflags/dev_switches.h"

namespace zephyrus_threat {

namespace {

// A list for the whole web is a few megabytes; a file past this is not one of
// ours and is not read.
constexpr int64_t kMaxBlobBytes = 64 * 1024 * 1024;

std::optional<std::vector<uint8_t>> ReadBlob(const base::FilePath& path) {
  if (path.empty()) {
    return std::nullopt;
  }
  const std::optional<int64_t> size = base::GetFileSize(path);
  if (!size || *size <= 0 || *size > kMaxBlobBytes) {
    return std::nullopt;
  }
  return base::ReadFileToBytes(path);
}

// The first of `candidates` that is a valid index goes into `index`.
void LoadFirstValid(ZephyrusThreatIndex& index,
                    const std::vector<base::FilePath>& candidates,
                    const char* what) {
  for (const base::FilePath& path : candidates) {
    std::optional<std::vector<uint8_t>> blob = ReadBlob(path);
    if (!blob) {
      continue;
    }
    if (index.Load(std::move(*blob))) {
      VLOG(1) << "Zephyrus " << what << ": " << index.size() << " entries from "
              << path;
      return;
    }
    LOG(WARNING) << "Zephyrus " << what << ": " << path << " is not valid; ignored";
  }
}

std::unique_ptr<ZephyrusThreatData> LoadOnBlockingThread(
    base::FilePath assets_dir,
    base::FilePath data_dir) {
  auto data = std::make_unique<ZephyrusThreatData>();
  std::vector<base::FilePath> threats = {data_dir.AppendASCII(kThreatsFileName),
                                         assets_dir.AppendASCII(kThreatsFileName)};
  std::vector<base::FilePath> popular = {data_dir.AppendASCII(kPopularFileName),
                                         assets_dir.AppendASCII(kPopularFileName)};
  if (zephyrus::DevSwitchesEnabled()) {
    // For tests and demos: a blob chosen by the environment wins.
    std::unique_ptr<base::Environment> env = base::Environment::Create();
    if (const std::optional<std::string> path = env->GetVar("ZEPHYRUS_THREAT_BLOB")) {
      threats.insert(threats.begin(), base::FilePath::FromUTF8Unsafe(*path));
    }
    if (const std::optional<std::string> path = env->GetVar("ZEPHYRUS_POPULAR_BLOB")) {
      popular.insert(popular.begin(), base::FilePath::FromUTF8Unsafe(*path));
    }
  }
  LoadFirstValid(data->threats, threats, "threat list");
  LoadFirstValid(data->popular, popular, "popular-sites list");
  return data;
}

}  // namespace

void LoadThreatData(
    const base::FilePath& assets_dir,
    const base::FilePath& data_dir,
    base::OnceCallback<void(std::unique_ptr<ZephyrusThreatData>)> callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&LoadOnBlockingThread, assets_dir, data_dir),
      std::move(callback));
}

}  // namespace zephyrus_threat
