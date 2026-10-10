// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/chrome_bookmarks_reader.h"

#include <string>

#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

// The shape of Chrome's real file, trimmed.
constexpr char kSample[] = R"json({
  "checksum": "x",
  "roots": {
    "bookmark_bar": {"type": "folder", "name": "Bookmarks bar", "children": [
      {"type": "url", "name": "Wikipedia", "url": "https://en.wikipedia.org/"},
      {"type": "folder", "name": "Work", "children": [
        {"type": "url", "name": "Mail", "url": "https://mail.example.com/"},
        {"type": "url", "name": "Tool", "url": "javascript:alert(1)"}
      ]}
    ]},
    "other": {"type": "folder", "name": "Other bookmarks", "children": [
      {"type": "url", "name": "Local", "url": "file:///C:/secret.txt"},
      {"type": "url", "name": "News", "url": "http://news.example.com/"}
    ]},
    "synced": {"type": "folder", "name": "Mobile bookmarks", "children": []}
  },
  "version": 1
})json";

TEST(ChromeBookmarksTest, ReadsTheTreeAndKeepsOnlyWebAddresses) {
  std::optional<ChromeBookmarks> read = ParseChromeBookmarks(kSample);
  ASSERT_TRUE(read);
  // The empty mobile folder is left out.
  ASSERT_EQ(read->roots.size(), 2u);
  EXPECT_EQ(read->roots[0].title, u"Bookmarks bar");
  ASSERT_EQ(read->roots[0].children.size(), 2u);
  EXPECT_EQ(read->roots[0].children[0].url.spec(), "https://en.wikipedia.org/");
  const ImportedBookmark& work = read->roots[0].children[1];
  EXPECT_TRUE(work.is_folder);
  // The javascript: bookmarklet is not brought over.
  ASSERT_EQ(work.children.size(), 1u);
  EXPECT_EQ(work.children[0].title, u"Mail");
  // Nor is a file: address.
  ASSERT_EQ(read->roots[1].children.size(), 1u);
  EXPECT_EQ(read->roots[1].children[0].title, u"News");
  EXPECT_EQ(read->urls, 3u);
  EXPECT_EQ(read->skipped, 2u);
}

TEST(ChromeBookmarksTest, SomethingElseIsNotABookmarksFile) {
  EXPECT_FALSE(ParseChromeBookmarks(""));
  EXPECT_FALSE(ParseChromeBookmarks("[1,2,3]"));
  EXPECT_FALSE(ParseChromeBookmarks(R"({"version": 1})"));
  EXPECT_FALSE(ParseChromeBookmarks("{not json"));
}

TEST(ChromeBookmarksTest, DeepNestingStopsAtTheLimit) {
  // A folder inside a folder, far deeper than anyone makes by hand.
  std::string json = R"({"roots": {"bookmark_bar": )";
  for (int i = 0; i < 60; ++i) {
    json += R"({"type": "folder", "name": "f", "children": [)";
  }
  json += R"({"type": "url", "name": "deep", "url": "https://deep.example/"})";
  for (int i = 0; i < 60; ++i) {
    json += "]}";
  }
  json += "}}";
  std::optional<ChromeBookmarks> read = ParseChromeBookmarks(json);
  ASSERT_TRUE(read);
  EXPECT_EQ(read->urls, 0u);
  EXPECT_GE(read->skipped, 1u);
}

TEST(ChromeBookmarksTest, LongTitlesAreCut) {
  const std::string name(5000, 'a');
  const std::string json = R"({"roots": {"bookmark_bar": {"type": "folder",)"
                           R"( "name": "b", "children": [{"type": "url",)"
                           R"( "name": ")" + name +
                           R"(", "url": "https://a.example/"}]}}})";
  std::optional<ChromeBookmarks> read = ParseChromeBookmarks(json);
  ASSERT_TRUE(read);
  ASSERT_EQ(read->roots.size(), 1u);
  EXPECT_EQ(read->roots[0].children[0].title.size(), kMaxChromeBookmarkTitle);
}

}  // namespace
}  // namespace zephyrus::agent
