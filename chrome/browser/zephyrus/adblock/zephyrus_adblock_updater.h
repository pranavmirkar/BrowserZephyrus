// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_UPDATER_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_UPDATER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"

class GURL;

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace zephyrus_adblock {

// Whether `body` plausibly IS an adblock filter list.
//
// **Why this exists.** These lists are fetched over HTTPS from third parties
// (easylist.to, raw.githubusercontent.com, secure.fanboy.co.nz) and we cannot
// verify a signature: none of those publishers sign their lists, and we cannot
// sign content we do not control. Real signing needs a project-hosted mirror.
//
// Until then, TLS is the only integrity guarantee, and TLS says nothing about
// WHAT the host served — only that the host we asked served it. A hijacked or
// misconfigured origin returning an error page, a login portal, or any other
// large blob previously sailed past the size floor and was adopted verbatim as
// filter rules. This is the check that stops that: content that is not a filter
// list is refused, and the previous good copy is kept.
//
// It cannot detect a hostile list that is genuinely well-formed. Nothing short
// of a signed mirror can.
bool LooksLikeFilterList(std::string_view body);

// Downloads the current upstream filter lists (EasyList, EasyPrivacy, and uBlock
// Origin's filters/quick-fixes — where the fast-moving YouTube scriptlet rules
// live) over HTTPS, concatenates them, and writes a single combined file to
// `output_path`. Runs entirely in the browser process on the UI thread (the
// file write happens on a background thread). Credential-less fetches (no
// cookies / no referrer) to keep updates privacy-clean.
//
// One-shot: create, call Start(), and destroy after the completion callback
// runs (destroying earlier cancels the in-flight downloads).
// Which lists a run fetches. Lists not fetched keep their previous copy.
enum class UpdateKind {
  kFull,        // Every list; the daily refresh.
  kQuickFixes,  // Only uBO's quick-fixes.txt, which expires after 8 hours.
};

class ZephyrusAdblockUpdater {
 public:
  // `success` is true only when a fresh, plausibly-complete combined list was
  // written to the output path.
  using CompletionCallback = base::OnceCallback<void(bool success)>;

  ZephyrusAdblockUpdater(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      base::FilePath output_path,
      UpdateKind kind = UpdateKind::kFull);
  ZephyrusAdblockUpdater(const ZephyrusAdblockUpdater&) = delete;
  ZephyrusAdblockUpdater& operator=(const ZephyrusAdblockUpdater&) = delete;
  ~ZephyrusAdblockUpdater();

  void Start(CompletionCallback on_complete);

 private:
  void Fetch(const GURL& url,
             base::OnceCallback<void(std::optional<std::string>)> on_body);
  void OnListDownloaded(size_t index, std::optional<std::string> body);
  void OnIncludeDownloaded(std::string url, std::optional<std::string> body);
  // bodies_[index] with each `!#include` line replaced by the file it names,
  // or nullopt when one of those files failed to download.
  std::optional<std::string> ExpandIncludes(size_t index) const;
  void OnAllDownloaded();
  void OnFileWritten(bool ok);

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  const base::FilePath output_path_;
  const UpdateKind kind_;
  CompletionCallback on_complete_;

  std::vector<std::unique_ptr<network::SimpleURLLoader>> loaders_;
  std::vector<std::string> bodies_;  // per-list; empty string = failed/skipped
  // Included files by resolved URL; empty = requested but failed.
  std::map<std::string, std::string> includes_;
  size_t include_bytes_ = 0;
  size_t pending_ = 0;

  base::WeakPtrFactory<ZephyrusAdblockUpdater> weak_factory_{this};
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_UPDATER_H_
