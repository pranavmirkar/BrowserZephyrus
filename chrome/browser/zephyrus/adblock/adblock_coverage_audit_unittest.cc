// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

// The coverage audit: which lines of the lists the browser runs does NOTHING
// take? A rule naming something we do not implement is not an error anywhere;
// it just does nothing, and the user finds out when a site breaks or detects
// the blocker (MEASURED: YouTube, 2026-11-12, 21 of 79 rules). This feeds every
// line, on its own, to every engine the service builds and records which ones
// took it, using the real parsers rather than a copy of their rules.
//
//   ZEPHYRUS_FULL_LIST=<downloaded filters.txt> ZEPHYRUS_AUDIT_OUT=<out.tsv>
//   zephyrus_adblock_engine_unittests --gtest_filter=AdblockCoverageAudit.*
//
// Output: one row per non-comment line: the engines that took it (or "-"),
// whether it sat in a section that may use trusted scriptlets, and the line.
// research/coverage_report.py turns it into the report.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_cosmetic_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_csp_filter.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_param_filter.h"
#include "chrome/browser/zephyrus/adblock/adblock_scriptlet_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_urlskip_filter.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_adblock {
namespace {

TEST(AdblockCoverageAudit, EveryLineOfTheFullList) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  const std::optional<std::string> list_path = env->GetVar("ZEPHYRUS_FULL_LIST");
  const std::optional<std::string> out_path = env->GetVar("ZEPHYRUS_AUDIT_OUT");
  if (!list_path || !out_path) {
    GTEST_SKIP() << "set ZEPHYRUS_FULL_LIST and ZEPHYRUS_AUDIT_OUT";
  }
  std::string raw;
  ASSERT_TRUE(base::ReadFileToString(base::FilePath::FromUTF8Unsafe(*list_path),
                                     &raw));
  const std::string list = PreprocessFilterList(raw);

  // One instance of each, reused: AddRules returns how many rules THIS call
  // added, which is all the audit needs.
  AdblockFilterEngine network;
  AdblockCosmeticEngine cosmetic;
  AdblockScriptletEngine scriptlet;
  AdblockParamFilter params;
  AdblockUrlSkipFilter urlskip;
  AdblockCspFilter csp;

  std::string out;
  out.reserve(list.size() + list.size() / 4);
  size_t lines = 0, taken = 0;
  // The section header the scriptlet engine judges trust by. Fed again with
  // each line, so a line is judged as it is in the full list.
  std::string section = "! ===== (before any section) =====";
  bool trusted = false;
  for (std::string_view line : base::SplitStringPiece(
           list, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (IsTrustedScriptletSectionMarker(line) || line.starts_with("! =====")) {
      section = std::string(line);
      trusted = IsTrustedScriptletSectionMarker(line);
      continue;
    }
    if (line.starts_with("!") || line.starts_with("[")) {
      continue;
    }
    ++lines;
    std::vector<std::string_view> who;
    const size_t regex_before = network.regex_rules().size();
    if (network.AddRules(line) > 0 ||
        network.regex_rules().size() > regex_before) {
      who.push_back("network");
    }
    if (cosmetic.AddRules(line) > 0) {
      who.push_back("cosmetic");
    }
    const std::string with_section = section + "\n" + std::string(line);
    if (scriptlet.AddRules(with_section, /*trust_unsectioned=*/false) > 0) {
      who.push_back("scriptlet");
    }
    if (params.AddRules(line) > 0) {
      who.push_back("param");
    }
    if (urlskip.AddRules(with_section, /*trust_unsectioned=*/false) > 0) {
      who.push_back("urlskip");
    }
    if (csp.AddRules(line) > 0) {
      who.push_back("csp");
    }
    if (!who.empty()) {
      ++taken;
    }
    out += who.empty() ? std::string("-") : base::JoinString(who, ",");
    out += trusted ? "\ttrusted\t" : "\t-\t";
    out += line;
    out += "\n";
  }
  ASSERT_TRUE(base::WriteFile(base::FilePath::FromUTF8Unsafe(*out_path), out));
  LOG(INFO) << "audit: " << lines << " rule lines, " << taken << " taken, "
            << (lines - taken) << " taken by nothing";
  EXPECT_GT(taken, lines / 2) << "the audit itself is broken";
}

}  // namespace
}  // namespace zephyrus_adblock
