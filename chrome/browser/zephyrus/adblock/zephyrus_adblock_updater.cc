// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_updater.h"

#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace zephyrus_adblock {

namespace {

// Upstream lists refreshed on each update. EasyList + EasyPrivacy are the base
// network/tracker coverage; the uBlock Origin lists add uBO's own rules and —
// crucially — the frequently-updated YouTube scriptlet (##+js) rules.
constexpr const char* kListUrls[] = {
    "https://easylist.to/easylist/easylist.txt",
    "https://easylist.to/easylist/easyprivacy.txt",
    "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/"
    "filters.txt",
    "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/"
    "quick-fixes.txt",
    "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/"
    "privacy.txt",
    "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/"
    "badware.txt",
    "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/"
    "unbreak.txt",
    // Cookie-banner / consent-popup blocking (EasyList Cookie List, a.k.a.
    // Fanboy's CookieMonster) — the single biggest "calm web" win: consent
    // walls never render, instead of being clicked away on every site.
    "https://secure.fanboy.co.nz/fanboy-cookiemonster.txt",
    // uBO annoyances-cookies: keeps hard cases (dynamic CMPs) covered.
    "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/"
    "annoyances-cookies.txt",
};
constexpr size_t kNumLists = std::size(kListUrls);

// Safe-buffer accessor for the URL table (avoids raw C-array indexing).
const char* UrlAt(size_t i) {
  return base::span<const char* const>(kListUrls)[i];
}

// A single upstream list smaller than this is treated as a failed download
// (truncation / error page) and dropped.
constexpr size_t kMinListSize = 1024;
// If the combined result is smaller than this the update is rejected and the
// previous file is kept (guards against writing a gutted list).
constexpr size_t kMinCombinedSize = 512 * 1024;

// The largest a legitimate combined list has any business being. The
// per-request cap is already 5 MB; this bounds the concatenation.
constexpr size_t kMaxCombinedSize = 32u * 1024 * 1024;

// An update that collapses the list to a fraction of what we already have is
// treated as an outage, not an update: a truncated or stubbed response is far
// more likely than every publisher legitimately shrinking at once.
constexpr double kMinShrinkRatio = 0.6;

// Writes `contents` to `path` atomically (temp file + rename). Runs on a
// background thread. Returns true on success.
bool WriteCombinedList(const base::FilePath& path,
                       std::unique_ptr<std::string> contents) {
  if (!base::CreateDirectory(path.DirName())) {
    return false;
  }
  return base::ImportantFileWriter::WriteFileAtomically(path, *contents,
                                                        "ZephyrusAdBlock");
}

// Refuses an update that would shrink the stored list dramatically, then writes
// atomically. A partial outage upstream, or a host serving a stub, should leave
// the user on yesterday's working rules rather than a gutted file.
bool WriteCombinedListIfNotAShrink(const base::FilePath& path,
                                   std::unique_ptr<std::string> contents) {
  std::optional<int64_t> existing = base::GetFileSize(path);
  if (existing.has_value() && *existing > 0) {
    const double ratio =
        static_cast<double>(contents->size()) / static_cast<double>(*existing);
    if (ratio < kMinShrinkRatio) {
      LOG(ERROR) << "[Zephyrus] refusing filter list update: new size "
                 << contents->size() << " is only " << (ratio * 100)
                 << "% of the existing " << *existing
                 << " bytes; keeping the previous copy";
      return false;
    }
  }
  return WriteCombinedList(path, std::move(contents));
}

}  // namespace

bool LooksLikeFilterList(std::string_view body) {
  if (body.empty()) {
    return false;
  }

  static constexpr std::string_view kSpace = " \t\r\n";

  // Every list we fetch opens with either an Adblock Plus header line
  // ("[Adblock Plus 2.0]") or a comment ("! Title: ..."). Nothing that is not
  // a filter list does, and an HTML error page certainly does not.
  size_t first = body.find_first_not_of(kSpace);
  if (first == std::string_view::npos) {
    return false;
  }
  // Skip a UTF-8 BOM if the server sent one.
  static constexpr std::string_view kBom = "\xEF\xBB\xBF";
  if (body.substr(first).starts_with(kBom)) {
    first = body.find_first_not_of(kSpace, first + kBom.size());
    if (first == std::string_view::npos) {
      return false;
    }
  }
  const std::string_view start = body.substr(first);
  if (!start.starts_with("[Adblock") && !start.starts_with("!")) {
    return false;
  }

  // Belt and braces for the case that actually happens in the wild: a captive
  // portal or error page whose first bytes were coaxed into looking like a
  // comment. Real filter lists contain essentially no markup up front, so the
  // scan is limited to the head -- element-hiding rules further down
  // legitimately contain angle brackets.
  const std::string_view head = body.substr(0, 4096);
  for (std::string_view marker :
       {"<!doctype", "<!DOCTYPE", "<html", "<HTML", "<head", "<body"}) {
    if (head.find(marker) != std::string_view::npos) {
      return false;
    }
  }
  return true;
}

ZephyrusAdblockUpdater::ZephyrusAdblockUpdater(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    base::FilePath output_path)
    : url_loader_factory_(std::move(url_loader_factory)),
      output_path_(std::move(output_path)) {}

ZephyrusAdblockUpdater::~ZephyrusAdblockUpdater() = default;

void ZephyrusAdblockUpdater::Start(CompletionCallback on_complete) {
  on_complete_ = std::move(on_complete);
  bodies_.resize(kNumLists);
  pending_ = kNumLists;

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("zephyrus_adblock_list_update", R"(
        semantics {
          sender: "Zephyrus Ad Blocker"
          description:
            "Periodically downloads public ad/tracker filter lists (EasyList, "
            "EasyPrivacy, and uBlock Origin's filter lists) so the built-in ad "
            "and tracker blocker stays current, including fast-moving rules "
            "such as YouTube ad blocking."
          trigger:
            "On startup when the local copy of the lists is older than a day, "
            "and roughly once a day while the browser is running."
          data: "None. Only a plain HTTPS GET for the public list files."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "This can be turned off via the ad blocker settings."
          policy_exception_justification: "Not yet implemented."
        })");

  for (size_t i = 0; i < kNumLists; ++i) {
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = GURL(UrlAt(i));
    request->method = "GET";
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    request->load_flags = net::LOAD_DO_NOT_SAVE_COOKIES;

    auto loader = network::SimpleURLLoader::Create(std::move(request),
                                                   traffic_annotation);
    loader->SetTimeoutDuration(base::Seconds(90));
    // Bounded download (5 MB cap); every individual upstream list is well under
    // that (EasyList, the largest, is a few MB).
    loader->DownloadToString(
        url_loader_factory_.get(),
        base::BindOnce(&ZephyrusAdblockUpdater::OnListDownloaded,
                       weak_factory_.GetWeakPtr(), i),
        network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
    loaders_.push_back(std::move(loader));
  }
}

void ZephyrusAdblockUpdater::OnListDownloaded(
    size_t index,
    std::optional<std::string> body) {
  if (!body || body->size() < kMinListSize) {
    LOG(WARNING) << "[Zephyrus] filter list download failed or too small: "
                 << UrlAt(index);
  } else if (!LooksLikeFilterList(*body)) {
    // Served something, but not a filter list. Dropping this one list is the
    // right degradation: the others still update, and the blocker keeps the
    // rules it already had for this one.
    LOG(ERROR) << "[Zephyrus] filter list did not look like a filter list, "
                  "refusing to adopt it: "
               << UrlAt(index);
  } else {
    bodies_[index] = std::move(*body);
  }
  if (--pending_ == 0) {
    OnAllDownloaded();
  }
}

void ZephyrusAdblockUpdater::OnAllDownloaded() {
  auto combined = std::make_unique<std::string>();
  combined->reserve(8 * 1024 * 1024);
  combined->append(
      "! Zephyrus combined filter lists (auto-updated). Do not edit.\n");
  for (size_t i = 0; i < bodies_.size(); ++i) {
    if (bodies_[i].empty()) {
      continue;
    }
    combined->append("\n! ===== ");
    combined->append(UrlAt(i));
    combined->append(" =====\n");
    combined->append(bodies_[i]);
    combined->append("\n");
  }

  if (combined->size() < kMinCombinedSize ||
      combined->size() > kMaxCombinedSize) {
    // Too little (or absurdly much) came back to trust; keep the existing file.
    LOG(ERROR) << "[Zephyrus] combined filter list size implausible ("
               << combined->size() << " bytes); keeping the previous copy";
    if (on_complete_) {
      std::move(on_complete_).Run(false);
    }
    return;
  }

  // Never replace a good list with a much smaller one. Checked on a blocking
  // sequence together with the write, because it has to stat the existing file.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&WriteCombinedListIfNotAShrink, output_path_,
                     std::move(combined)),
      base::BindOnce(&ZephyrusAdblockUpdater::OnFileWritten,
                     weak_factory_.GetWeakPtr()));
}

void ZephyrusAdblockUpdater::OnFileWritten(bool ok) {
  if (on_complete_) {
    std::move(on_complete_).Run(ok);
  }
}

}  // namespace zephyrus_adblock
