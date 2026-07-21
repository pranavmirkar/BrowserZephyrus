// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_UPDATER_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_UPDATER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace zephyrus_adblock {

// Downloads the current upstream filter lists (EasyList, EasyPrivacy, and uBlock
// Origin's filters/quick-fixes — where the fast-moving YouTube scriptlet rules
// live) over HTTPS, concatenates them, and writes a single combined file to
// `output_path`. Runs entirely in the browser process on the UI thread (the
// file write happens on a background thread). Credential-less fetches (no
// cookies / no referrer) to keep updates privacy-clean.
//
// One-shot: create, call Start(), and destroy after the completion callback
// runs (destroying earlier cancels the in-flight downloads).
class ZephyrusAdblockUpdater {
 public:
  // `success` is true only when a fresh, plausibly-complete combined list was
  // written to the output path.
  using CompletionCallback = base::OnceCallback<void(bool success)>;

  ZephyrusAdblockUpdater(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      base::FilePath output_path);
  ZephyrusAdblockUpdater(const ZephyrusAdblockUpdater&) = delete;
  ZephyrusAdblockUpdater& operator=(const ZephyrusAdblockUpdater&) = delete;
  ~ZephyrusAdblockUpdater();

  void Start(CompletionCallback on_complete);

 private:
  void OnListDownloaded(size_t index, std::optional<std::string> body);
  void OnAllDownloaded();
  void OnFileWritten(bool ok);

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  const base::FilePath output_path_;
  CompletionCallback on_complete_;

  std::vector<std::unique_ptr<network::SimpleURLLoader>> loaders_;
  std::vector<std::string> bodies_;  // per-list; empty string = failed/skipped
  size_t pending_ = 0;

  base::WeakPtrFactory<ZephyrusAdblockUpdater> weak_factory_{this};
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_UPDATER_H_
