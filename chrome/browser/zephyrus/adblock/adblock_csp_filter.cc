// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_csp_filter.h"

#include <algorithm>
#include <utility>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace zephyrus_adblock {

BASE_FEATURE(kZephyrusAdblockCsp,
             "ZephyrusAdblockCsp",
             base::FEATURE_ENABLED_BY_DEFAULT);

namespace {

constexpr size_t kMaxPolicyLength = 4096;

// Options are comma separated; a CSP source list never contains a comma.
std::vector<std::string> SplitOptions(std::string_view options) {
  std::vector<std::string> out;
  for (std::string_view piece : base::SplitStringPiece(
           options, ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    out.emplace_back(piece);
  }
  return out;
}

bool IsFrameType(ResourceType type) {
  return type == kTypeDocument || type == kTypeSubdocument;
}

}  // namespace

AdblockCspFilter::AdblockCspFilter() = default;
AdblockCspFilter::~AdblockCspFilter() = default;

// static
bool AdblockCspFilter::IsAcceptablePolicy(std::string_view policy) {
  if (policy.empty() || policy.size() > kMaxPolicyLength) {
    return false;
  }
  for (const char c : policy) {
    // Control characters would split the header; a comma would turn it into two
    // policies the list author did not write.
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f || c == ',') {
      return false;
    }
  }
  const std::string lower = base::ToLowerASCII(policy);
  // A violation report goes to whatever address the policy names: a list must
  // not be able to make a page tell a third party where you are.
  return lower.find("report-uri") == std::string::npos &&
         lower.find("report-to") == std::string::npos;
}

size_t AdblockCspFilter::AddRules(std::string_view list_text) {
  size_t added = 0;
  size_t pos = 0;
  // About a hundred lines mention `csp` in a 9 MB list: jump from one
  // occurrence to the next.
  while ((pos = list_text.find("csp", pos)) != std::string_view::npos) {
    const size_t line_start = list_text.rfind('\n', pos);
    const size_t begin =
        line_start == std::string_view::npos ? 0 : line_start + 1;
    size_t end = list_text.find('\n', pos);
    if (end == std::string_view::npos) {
      end = list_text.size();
    }
    std::string_view line = base::TrimWhitespaceASCII(
        list_text.substr(begin, end - begin), base::TRIM_ALL);
    pos = end;
    // A comment, or a cosmetic rule that happens to say "csp".
    if (line.empty() || line[0] == '!' ||
        line.find("##") != std::string_view::npos ||
        line.find("#@#") != std::string_view::npos ||
        line.find("#?#") != std::string_view::npos ||
        line.find("$$") != std::string_view::npos) {
      continue;
    }
    if (AddOne(line)) {
      ++added;
    }
  }
  return added;
}

bool AdblockCspFilter::AddOne(std::string_view line) {
  // The options start at the last `$` that is followed by an option list
  // containing `csp`. A policy never contains a `$`, so rfind is exact.
  const size_t dollar = line.rfind('$');
  if (dollar == std::string_view::npos) {
    return false;
  }
  Spec spec;
  bool has_csp = false;
  std::string rest;
  for (const std::string& opt : SplitOptions(line.substr(dollar + 1))) {
    if (opt == "csp" || opt.starts_with("csp=")) {
      if (has_csp) {
        return false;
      }
      has_csp = true;
      spec.policy = opt == "csp" ? std::string() : opt.substr(4);
      continue;
    }
    if (!rest.empty()) {
      rest += ',';
    }
    rest += opt;
  }
  if (!has_csp) {
    return false;
  }
  std::string pattern(line.substr(0, dollar));
  spec.exception = pattern.starts_with("@@");
  if (!spec.exception && spec.policy.empty()) {
    ++skipped_count_;  // a bare `$csp` block says nothing to add
    return false;
  }
  if (!spec.policy.empty() && !IsAcceptablePolicy(spec.policy)) {
    ++skipped_count_;
    return false;
  }
  // A rule for every page on the web is almost certainly a mistake in a list,
  // and a policy on every page is not something to take on trust.
  if (pattern.empty()) {
    pattern = "*";
  } else if (pattern == "@@") {
    pattern = "@@*";
  }
  if (!spec.exception && pattern == "*" && rest.empty()) {
    ++skipped_count_;
    return false;
  }
  std::string engine_line = pattern;
  if (!rest.empty()) {
    engine_line += '$' + rest;
  }
  specs_.push_back(std::move(spec));
  if (!engine_.AddTaggedRule(engine_line,
                             static_cast<uint32_t>(specs_.size()))) {
    specs_.pop_back();  // an option the engine does not understand
    ++skipped_count_;
    return false;
  }
  ++rule_count_;
  return true;
}

std::vector<std::string> AdblockCspFilter::PoliciesFor(
    const GURL& url,
    const GURL& initiator,
    ResourceType type) const {
  std::vector<std::string> policies;
  if (specs_.empty() || !IsFrameType(type) || !url.SchemeIsHTTPOrHTTPS()) {
    return policies;
  }
  std::vector<const Spec*> exceptions;
  std::vector<const Spec*> applying;
  // A page load has no initiator worth the name: `$domain=` means the site
  // being opened, as it does for every rule that names a page.
  engine_.ForEachMatchingRule(
      url, type == kTypeDocument ? url : initiator, type,
      [&](const FilterRule& rule) {
        if (rule.tag == 0 || rule.tag > specs_.size()) {
          return;
        }
        const Spec& spec = specs_[rule.tag - 1];
        (spec.exception ? exceptions : applying).push_back(&spec);
      });
  for (const Spec* exception : exceptions) {
    if (exception->policy.empty()) {
      return policies;  // the page is exempt from every policy
    }
  }
  for (const Spec* spec : applying) {
    const bool excepted =
        std::ranges::any_of(exceptions, [&](const Spec* exception) {
          return exception->policy == spec->policy;
        });
    if (!excepted && !std::ranges::contains(policies, spec->policy)) {
      policies.push_back(spec->policy);
    }
  }
  return policies;
}

}  // namespace zephyrus_adblock
