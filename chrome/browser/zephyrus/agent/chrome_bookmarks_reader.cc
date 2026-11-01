// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/chrome_bookmarks_reader.h"

#include <utility>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"

namespace zephyrus::agent {

ImportedBookmark::ImportedBookmark() = default;
ImportedBookmark::ImportedBookmark(const ImportedBookmark&) = default;
ImportedBookmark& ImportedBookmark::operator=(const ImportedBookmark&) = default;
ImportedBookmark::ImportedBookmark(ImportedBookmark&&) = default;
ImportedBookmark& ImportedBookmark::operator=(ImportedBookmark&&) = default;
ImportedBookmark::~ImportedBookmark() = default;

ChromeBookmarks::ChromeBookmarks() = default;
ChromeBookmarks::ChromeBookmarks(ChromeBookmarks&&) = default;
ChromeBookmarks& ChromeBookmarks::operator=(ChromeBookmarks&&) = default;
ChromeBookmarks::~ChromeBookmarks() = default;

namespace {

std::u16string Title(const base::DictValue& node) {
  const std::string* name = node.FindString("name");
  std::u16string title = name ? base::UTF8ToUTF16(*name) : std::u16string();
  if (title.size() > kMaxChromeBookmarkTitle) {
    title.resize(kMaxChromeBookmarkTitle);
  }
  return title;
}

// Reads one node of Chrome's tree into `out`. Returns false when the node is
// left out (and counts why in `result`).
bool ReadNode(const base::DictValue& node,
              int depth,
              ChromeBookmarks& result,
              ImportedBookmark& out) {
  if (result.urls >= kMaxChromeBookmarks) {
    ++result.skipped;
    return false;
  }
  const std::string* type = node.FindString("type");
  if (!type) {
    ++result.skipped;
    return false;
  }
  if (*type == "url") {
    const std::string* address = node.FindString("url");
    GURL url = address ? GURL(*address) : GURL();
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
      ++result.skipped;
      return false;
    }
    out.is_folder = false;
    out.title = Title(node);
    out.url = std::move(url);
    ++result.urls;
    return true;
  }
  if (*type == "folder") {
    if (depth >= kMaxChromeBookmarkDepth) {
      ++result.skipped;
      return false;
    }
    out.is_folder = true;
    out.title = Title(node);
    if (const base::ListValue* children = node.FindList("children")) {
      for (const base::Value& child : *children) {
        if (!child.is_dict()) {
          ++result.skipped;
          continue;
        }
        ImportedBookmark read;
        if (ReadNode(child.GetDict(), depth + 1, result, read)) {
          out.children.push_back(std::move(read));
        }
      }
    }
    return true;
  }
  ++result.skipped;
  return false;
}

}  // namespace

std::optional<ChromeBookmarks> ParseChromeBookmarks(std::string_view json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const base::DictValue* roots = parsed->GetDict().FindDict("roots");
  if (!roots) {
    return std::nullopt;
  }
  ChromeBookmarks result;
  for (const char* key : {"bookmark_bar", "other", "synced"}) {
    const base::DictValue* root = roots->FindDict(key);
    if (!root) {
      continue;
    }
    ImportedBookmark folder;
    if (ReadNode(*root, 0, result, folder) && folder.is_folder &&
        !folder.children.empty()) {
      result.roots.push_back(std::move(folder));
    }
  }
  return result;
}

base::FilePath ChromeBookmarksFilePath() {
  base::FilePath local;
  if (!base::PathService::Get(base::DIR_LOCAL_APP_DATA, &local)) {
    return base::FilePath();
  }
  return local.Append(FILE_PATH_LITERAL("Google"))
      .Append(FILE_PATH_LITERAL("Chrome"))
      .Append(FILE_PATH_LITERAL("User Data"))
      .Append(FILE_PATH_LITERAL("Default"))
      .Append(FILE_PATH_LITERAL("Bookmarks"));
}

std::optional<ChromeBookmarks> ReadChromeBookmarks(const base::FilePath& path) {
  if (path.empty()) {
    return std::nullopt;
  }
  std::string json;
  if (!base::ReadFileToStringWithMaxSize(path, &json,
                                         kMaxChromeBookmarksFileBytes)) {
    return std::nullopt;
  }
  return ParseChromeBookmarks(json);
}

}  // namespace zephyrus::agent
