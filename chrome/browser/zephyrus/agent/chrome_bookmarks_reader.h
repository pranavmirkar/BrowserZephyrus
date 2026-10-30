// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_CHROME_BOOKMARKS_READER_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_CHROME_BOOKMARKS_READER_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "url/gurl.h"

namespace zephyrus::agent {

// One bookmark or folder read out of Google Chrome's "Bookmarks" file.
struct ImportedBookmark {
  ImportedBookmark();
  ImportedBookmark(const ImportedBookmark&);
  ImportedBookmark& operator=(const ImportedBookmark&);
  ImportedBookmark(ImportedBookmark&&);
  ImportedBookmark& operator=(ImportedBookmark&&);
  ~ImportedBookmark();

  bool is_folder = false;
  std::u16string title;
  GURL url;  // folders: empty
  std::vector<ImportedBookmark> children;
};

// What was read: Chrome's three roots in order (bookmarks bar, other, mobile),
// each a folder, empty ones left out.
struct ChromeBookmarks {
  ChromeBookmarks();
  ChromeBookmarks(ChromeBookmarks&&);
  ChromeBookmarks& operator=(ChromeBookmarks&&);
  ~ChromeBookmarks();

  std::vector<ImportedBookmark> roots;
  size_t urls = 0;     // web addresses kept
  size_t skipped = 0;  // entries left out: not http(s), malformed, past a limit
};

// Chromium's own importer reads Firefox, Edge and a bookmarks.html file, but not
// Chrome, so onboarding's "Bring my bookmarks from Chrome" reads Chrome's JSON
// file itself. The file belongs to another program and could be anything, so
// this is defensive: only http and https addresses are kept (no javascript:
// bookmarklets, no file: or chrome: pages), folders nest at most kMaxDepth deep,
// at most kMaxBookmarks are read, and titles are cut to kMaxTitle characters.
// Returns nullopt for something that is not a Chrome bookmarks file at all.
inline constexpr size_t kMaxChromeBookmarks = 20000;
inline constexpr int kMaxChromeBookmarkDepth = 32;
inline constexpr size_t kMaxChromeBookmarkTitle = 512;
inline constexpr size_t kMaxChromeBookmarksFileBytes = 32 * 1024 * 1024;
std::optional<ChromeBookmarks> ParseChromeBookmarks(std::string_view json);

// Where Chrome keeps the default profile's bookmarks on this computer, or an
// empty path if there is no local app data folder.
base::FilePath ChromeBookmarksFilePath();

// Reads and parses that file. Blocking: call it on a thread that may block.
std::optional<ChromeBookmarks> ReadChromeBookmarks(const base::FilePath& path);

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_CHROME_BOOKMARKS_READER_H_
