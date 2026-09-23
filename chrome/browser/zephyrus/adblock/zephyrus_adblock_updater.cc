// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_updater.h"

#include <algorithm>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"
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
    // IndianList: the regional list for Indian sites (Hindi and English news,
    // cricket and film portals) and the ad networks they run, which the
    // global lists cover thinly.
    "https://easylist-downloads.adblockplus.org/indianlist.txt",
};

// How many `!#include`s one list may pull in. uBO's filters.txt has ten
// today; the cap only exists so a hostile list cannot fan the updater out.
constexpr size_t kMaxIncludesPerList = 24;
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

// uBO's quick-fixes.txt: where the fast-moving fixes (YouTube's among them)
// land. Its own header says "Expires: 8 hours", so it is refreshed on that
// cadence by itself; see UpdateKind.
constexpr size_t kQuickFixesIndex = 3;

// A combined file is never written without these: EasyList, EasyPrivacy and
// uBO's main list are the blocker. Anything else missing is tolerated.
constexpr size_t kCoreLists[] = {0, 1, 2};

// The indices above are positions in kListUrls; reordering that table must
// not silently point them at different lists.
static_assert(std::string_view(kListUrls[kQuickFixesIndex])
                  .ends_with("/quick-fixes.txt"));
static_assert(std::string_view(kListUrls[0]).ends_with("/easylist.txt"));
static_assert(std::string_view(kListUrls[1]).ends_with("/easyprivacy.txt"));
static_assert(std::string_view(kListUrls[2]).ends_with("/filters.txt"));

// Builds the new combined file from the lists just downloaded and writes it.
// Runs on a blocking sequence, because it reads the file it replaces.
//
// A list with no fresh copy -- its download failed, one of its `!#include`s
// failed, or this was a quick-fixes-only run -- is CARRIED FORWARD from the
// previous file rather than dropped. Dropping it was the old behaviour, and it
// meant one host having a bad hour left every user without that list's rules
// until the next day's update.
bool AssembleAndWrite(const base::FilePath& path,
                      std::vector<std::optional<std::string>> fresh,
                      bool full_refresh,
                      int64_t now_seconds) {
  std::string existing;
  if (!base::ReadFileToStringWithMaxSize(path, &existing,
                                         kMaxCombinedSize + 1024 * 1024)) {
    existing.clear();  // Missing, unreadable or implausibly large: start over.
  }
  const CombinedListHeader old_header = ParseCombinedListHeader(existing);

  std::string lists;
  lists.reserve(kMaxCombinedSize / 2);
  size_t fresh_count = 0;
  for (size_t i = 0; i < kNumLists; ++i) {
    std::string_view section;
    if (fresh[i]) {
      section = *fresh[i];
      ++fresh_count;
    } else if (std::optional<std::string_view> old =
                   FindListSection(existing, UrlAt(i))) {
      section = *old;
      if (full_refresh) {
        LOG(WARNING) << "[Zephyrus] keeping the previous copy of " << UrlAt(i);
      }
    } else {
      if (std::ranges::contains(kCoreLists, i)) {
        LOG(ERROR) << "[Zephyrus] no copy of core filter list " << UrlAt(i)
                   << "; keeping the previous combined file";
        return false;
      }
      continue;
    }
    lists += '\n';
    lists += ListSectionMarker(UrlAt(i));
    lists += '\n';
    lists += section;
    if (!section.ends_with('\n')) {
      lists += '\n';
    }
  }
  if (fresh_count == 0) {
    return false;  // Nothing new; leave the file (and its age) alone.
  }

  // The full-refresh stamp moves only when most lists really were refreshed;
  // otherwise the next check retries the full download instead of treating a
  // mostly carried-forward file as current for another day.
  const bool refreshed = full_refresh && fresh_count * 2 >= kNumLists;
  const int64_t stamp =
      refreshed ? now_seconds : old_header.full_update_seconds;
  std::string contents = CombinedListHeaderText(stamp) + lists;

  if (contents.size() < kMinCombinedSize || contents.size() > kMaxCombinedSize) {
    LOG(ERROR) << "[Zephyrus] combined filter list size implausible ("
               << contents.size() << " bytes); keeping the previous copy";
    return false;
  }
  // Never replace a good list with a much smaller one: a partial outage
  // upstream, or a host serving a stub, should leave the user on yesterday's
  // working rules rather than a gutted file.
  if (!existing.empty()) {
    const double ratio = static_cast<double>(contents.size()) /
                         static_cast<double>(existing.size());
    if (ratio < kMinShrinkRatio) {
      LOG(ERROR) << "[Zephyrus] refusing filter list update: new size "
                 << contents.size() << " is only " << (ratio * 100)
                 << "% of the existing " << existing.size()
                 << " bytes; keeping the previous copy";
      return false;
    }
  }
  if (!base::CreateDirectory(path.DirName())) {
    return false;
  }
  return base::ImportantFileWriter::WriteFileAtomically(path, contents,
                                                        "ZephyrusAdBlock");
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
    base::FilePath output_path,
    UpdateKind kind)
    : url_loader_factory_(std::move(url_loader_factory)),
      output_path_(std::move(output_path)),
      kind_(kind) {}

ZephyrusAdblockUpdater::~ZephyrusAdblockUpdater() = default;

namespace {

net::NetworkTrafficAnnotationTag ListTrafficAnnotation() {
  return
      net::DefineNetworkTrafficAnnotation("zephyrus_adblock_list_update", R"(
        semantics {
          sender: "Zephyrus Ad Blocker"
          description:
            "Periodically downloads public ad/tracker filter lists (EasyList, "
            "EasyPrivacy, and uBlock Origin's filter lists) so the built-in ad "
            "and tracker blocker stays current, including fast-moving rules "
            "such as YouTube ad blocking."
          trigger:
            "Shortly after startup and hourly while the browser runs: every "
            "list is refreshed once a day, and uBlock Origin's quick-fixes "
            "list (the one its publisher marks as expiring after 8 hours) "
            "every 8 hours. Also fetches the files those lists include from "
            "the same directory, and IndianList from adblockplus.org."
          data: "None. Only a plain HTTPS GET for the public list files."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "Turning the ad blocker off stops these downloads."
          policy_exception_justification: "Not yet implemented."
        })");
}

}  // namespace

void ZephyrusAdblockUpdater::Fetch(
    const GURL& url,
    base::OnceCallback<void(std::optional<std::string>)> on_body) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DO_NOT_SAVE_COOKIES;

  auto loader = network::SimpleURLLoader::Create(std::move(request),
                                                 ListTrafficAnnotation());
  loader->SetTimeoutDuration(base::Seconds(90));
  // Bounded download (5 MB cap); every individual upstream list is well under
  // that (EasyList, the largest, is a few MB).
  loader->DownloadToString(
      url_loader_factory_.get(), std::move(on_body),
      network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
  loaders_.push_back(std::move(loader));
}

void ZephyrusAdblockUpdater::Start(CompletionCallback on_complete) {
  on_complete_ = std::move(on_complete);
  bodies_.resize(kNumLists);
  std::vector<size_t> wanted;
  if (kind_ == UpdateKind::kQuickFixes) {
    wanted.push_back(kQuickFixesIndex);
  } else {
    for (size_t i = 0; i < kNumLists; ++i) {
      wanted.push_back(i);
    }
  }
  pending_ = wanted.size();
  for (size_t i : wanted) {
    Fetch(GURL(UrlAt(i)),
          base::BindOnce(&ZephyrusAdblockUpdater::OnListDownloaded,
                         weak_factory_.GetWeakPtr(), i));
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
    // uBO's filters.txt is mostly a table of contents: its rules live in
    // filters-general.txt, filters-2020..2026.txt and the rest, pulled in with
    // `!#include`. Read as comments, those lines dropped most of uBO's rules
    // -- and nearly all its anti-adblock fixes. Includes resolve against the
    // list's own URL, and FindListIncludes admits only bare same-directory
    // names, so they can only reach the directory the list came from.
    const GURL base_url(UrlAt(index));
    std::vector<std::string> names = FindListIncludes(bodies_[index]);
    if (names.size() > kMaxIncludesPerList) {
      names.resize(kMaxIncludesPerList);
    }
    for (const std::string& name : names) {
      const GURL url = base_url.Resolve(name);
      if (!url.is_valid() || !url.SchemeIs(url::kHttpsScheme) ||
          url.host() != base_url.host() || includes_.contains(url.spec())) {
        continue;
      }
      includes_[url.spec()];  // Reserve: a second mention is not refetched.
      ++pending_;
      Fetch(url, base::BindOnce(&ZephyrusAdblockUpdater::OnIncludeDownloaded,
                                weak_factory_.GetWeakPtr(), url.spec()));
    }
  }
  if (--pending_ == 0) {
    OnAllDownloaded();
  }
}

void ZephyrusAdblockUpdater::OnIncludeDownloaded(
    std::string url,
    std::optional<std::string> body) {
  // Included files are fragments: no header of their own, so only the markup
  // half of LooksLikeFilterList applies. A missing include leaves the parent
  // list's other rules in place.
  if (body && !body->empty() &&
      include_bytes_ + body->size() <= kMaxCombinedSize &&
      LooksLikeFilterList(base::StrCat({"!\n", *body}))) {
    // Bounded in total, not just per file: everything held here stays in
    // memory until the combined list is written.
    include_bytes_ += body->size();
    includes_[url] = std::move(*body);
  } else {
    LOG(WARNING) << "[Zephyrus] filter list include failed: " << url;
  }
  if (--pending_ == 0) {
    OnAllDownloaded();
  }
}

std::optional<std::string> ZephyrusAdblockUpdater::ExpandIncludes(
    size_t index) const {
  const std::string& body = bodies_[index];
  if (includes_.empty() || body.find("!#include ") == std::string::npos) {
    return body;
  }
  const GURL base_url(UrlAt(index));
  std::string out;
  out.reserve(body.size());
  size_t pos = 0;
  while (pos < body.size()) {
    size_t end = body.find('\n', pos);
    const size_t next = end == std::string::npos ? body.size() : end + 1;
    const std::string_view line = std::string_view(body).substr(pos, next - pos);
    pos = next;
    // Inlined in place, not appended: the include often sits inside an
    // `!#if` block (filters-mobile.txt) that must still govern it.
    std::vector<std::string> names = FindListIncludes(line);
    if (names.size() == 1) {
      auto it = includes_.find(base_url.Resolve(names[0]).spec());
      if (it != includes_.end()) {
        if (it->second.empty()) {
          // Requested but failed: this copy of the list is missing part of
          // its rules. Returning nothing makes the caller keep the previous,
          // complete copy instead of adopting a hollow one.
          return std::nullopt;
        }
        out.append(it->second);
        out.push_back('\n');
        continue;
      }
    }
    out.append(line);
  }
  return out;
}

void ZephyrusAdblockUpdater::OnAllDownloaded() {
  std::vector<std::optional<std::string>> fresh(kNumLists);
  for (size_t i = 0; i < kNumLists; ++i) {
    if (!bodies_[i].empty()) {
      fresh[i] = ExpandIncludes(i);
    }
  }
  bodies_.clear();
  includes_.clear();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&AssembleAndWrite, output_path_, std::move(fresh),
                     kind_ == UpdateKind::kFull,
                     base::Time::Now().ToTimeT()),
      base::BindOnce(&ZephyrusAdblockUpdater::OnFileWritten,
                     weak_factory_.GetWeakPtr()));
}

void ZephyrusAdblockUpdater::OnFileWritten(bool ok) {
  if (on_complete_) {
    std::move(on_complete_).Run(ok);
  }
}

}  // namespace zephyrus_adblock
