// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"

namespace zephyrus_adblock {

namespace {

// What Zephyrus is, in uBO's vocabulary. Everything not listed is false:
// env_firefox, env_mobile, env_safari, cap_html_filtering (a Firefox-only
// stream filter), ext_ubol (uBO Lite), adguard, ext_abp, env_mv3...
bool TokenValue(std::string_view token) {
  static constexpr std::string_view kTrue[] = {
      "env_chromium", "ext_ublock", "cap_user_stylesheet", "true",
  };
  for (std::string_view t : kTrue) {
    if (token == t) {
      return true;
    }
  }
  return false;
}

// Recursive-descent evaluator over `||`, `&&`, `!` and parentheses. Depth is
// capped so a hostile line cannot recurse the parser off the stack; anything
// malformed evaluates false, which drops the block -- the conservative side.
class ConditionParser {
 public:
  explicit ConditionParser(std::string_view text) : text_(text) {}

  bool Parse() {
    bool value = Or(0);
    SkipSpace();
    return ok_ && pos_ == text_.size() && value;
  }

 private:
  static constexpr int kMaxDepth = 32;

  void SkipSpace() {
    while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t')) {
      ++pos_;
    }
  }
  bool Consume(std::string_view op) {
    SkipSpace();
    if (text_.substr(pos_).starts_with(op)) {
      pos_ += op.size();
      return true;
    }
    return false;
  }
  bool Or(int depth) {
    bool value = And(depth);
    while (ok_ && Consume("||")) {
      value = And(depth) || value;
    }
    return value;
  }
  bool And(int depth) {
    bool value = Unary(depth);
    while (ok_ && Consume("&&")) {
      value = Unary(depth) && value;
    }
    return value;
  }
  bool Unary(int depth) {
    if (depth > kMaxDepth) {
      ok_ = false;
      return false;
    }
    if (Consume("!")) {
      return !Unary(depth + 1);
    }
    if (Consume("(")) {
      bool value = Or(depth + 1);
      if (!Consume(")")) {
        ok_ = false;
      }
      return value;
    }
    SkipSpace();
    size_t start = pos_;
    while (pos_ < text_.size() &&
           (base::IsAsciiAlphaNumeric(text_[pos_]) || text_[pos_] == '_')) {
      ++pos_;
    }
    if (start == pos_) {
      ok_ = false;
      return false;
    }
    return TokenValue(text_.substr(start, pos_ - start));
  }

  std::string_view text_;
  size_t pos_ = 0;
  bool ok_ = true;
};

// "a.b.example.co.in" -> "a.b.example": the host minus its public suffix, or
// empty when it has none (an IP, a bare suffix, an unknown TLD).
std::string_view StripRegistry(std::string_view host) {
  const size_t registry =
      net::registry_controlled_domains::GetCanonicalHostRegistryLength(
          host, net::registry_controlled_domains::EXCLUDE_UNKNOWN_REGISTRIES,
          net::registry_controlled_domains::EXCLUDE_PRIVATE_REGISTRIES);
  if (registry == 0 || registry == std::string::npos ||
      registry + 1 >= host.size()) {
    return std::string_view();
  }
  return host.substr(0, host.size() - registry - 1);
}

bool HostMatchesPlainDomain(std::string_view host, std::string_view domain) {
  if (host == domain) {
    return true;
  }
  return host.size() > domain.size() && host.ends_with(domain) &&
         host[host.size() - domain.size() - 1] == '.';
}

}  // namespace

bool EvaluateListCondition(std::string_view expression) {
  return ConditionParser(expression).Parse();
}

std::string PreprocessFilterList(std::string_view text) {
  // Fast path: most lists (EasyList, EasyPrivacy) carry no directives at all.
  if (text.find("!#if") == std::string_view::npos) {
    return std::string(text);
  }
  // One frame per open `!#if`: whether its current branch is live. A line is
  // kept only when every enclosing frame is.
  struct Frame {
    bool parent_live;
    bool condition;
    bool in_else;
  };
  std::vector<Frame> stack;
  bool live = true;
  std::string out;
  out.reserve(text.size());
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    const size_t next = end == std::string_view::npos ? text.size() : end + 1;
    std::string_view line = text.substr(pos, next - pos);
    const std::string_view trimmed =
        base::TrimWhitespaceASCII(line, base::TRIM_ALL);
    pos = next;
    if (trimmed.starts_with("!#if ") || trimmed == "!#if") {
      const bool condition = EvaluateListCondition(trimmed.substr(4));
      stack.push_back({live, condition, false});
      live = live && condition;
      continue;
    }
    if (trimmed == "!#else") {
      if (!stack.empty() && !stack.back().in_else) {
        stack.back().in_else = true;
        live = stack.back().parent_live && !stack.back().condition;
      }
      continue;
    }
    if (trimmed == "!#endif") {
      if (!stack.empty()) {
        live = stack.back().parent_live;
        stack.pop_back();
      }
      continue;
    }
    if (live) {
      out.append(line);
    }
  }
  return out;
}

std::vector<std::string> FindListIncludes(std::string_view text) {
  std::vector<std::string> names;
  for (std::string_view line : base::SplitStringPiece(
           text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (!line.starts_with("!#include ")) {
      continue;
    }
    std::string_view name = base::TrimWhitespaceASCII(
        line.substr(sizeof("!#include ") - 1), base::TRIM_ALL);
    bool plain = !name.empty() && name.size() <= 64 && name.ends_with(".txt") &&
                 name.find("..") == std::string_view::npos;
    for (char c : name) {
      if (!base::IsAsciiAlphaNumeric(c) && c != '-' && c != '_' && c != '.') {
        plain = false;
        break;
      }
    }
    if (plain) {
      names.emplace_back(name);
    }
  }
  return names;
}

std::vector<std::string> DomainLookupKeys(std::string_view host) {
  std::vector<std::string> keys;
  for (size_t pos = 0; pos != std::string_view::npos;) {
    keys.emplace_back(host.substr(pos));
    size_t dot = host.find('.', pos);
    pos = (dot == std::string_view::npos) ? dot : dot + 1;
  }
  const std::string_view base = StripRegistry(host);
  for (size_t pos = 0; !base.empty() && pos != std::string_view::npos;) {
    keys.push_back(std::string(base.substr(pos)) + ".*");
    size_t dot = base.find('.', pos);
    pos = (dot == std::string_view::npos) ? dot : dot + 1;
  }
  return keys;
}

bool HostMatchesFilterDomain(std::string_view host, std::string_view domain) {
  if (domain.size() > 2 && domain.ends_with(".*")) {
    const std::string_view base = StripRegistry(host);
    return !base.empty() &&
           HostMatchesPlainDomain(base, domain.substr(0, domain.size() - 2));
  }
  return HostMatchesPlainDomain(host, domain);
}

namespace {

constexpr std::string_view kFormatKey = "! Zephyrus-Format: ";
constexpr std::string_view kFullUpdateKey = "! Zephyrus-Full-Update: ";

}  // namespace

CombinedListHeader ParseCombinedListHeader(std::string_view head) {
  CombinedListHeader header;
  // The header is the first few lines; nothing further down is trusted as one.
  head = head.substr(0, 1024);
  for (std::string_view line : base::SplitStringPiece(
           head, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (line.starts_with(kFormatKey)) {
      int value = 0;
      if (base::StringToInt(line.substr(kFormatKey.size()), &value)) {
        header.format = value;
      }
    } else if (line.starts_with(kFullUpdateKey)) {
      int64_t value = 0;
      if (base::StringToInt64(line.substr(kFullUpdateKey.size()), &value) &&
          value > 0) {
        header.full_update_seconds = value;
      }
    } else if (line.starts_with("! =====")) {
      break;  // The lists begin; the header is over.
    }
  }
  return header;
}

std::string CombinedListHeaderText(int64_t full_update_seconds) {
  return base::StrCat(
      {"! Zephyrus combined filter lists (auto-updated). Do not edit.\n",
       kFormatKey, base::NumberToString(kCombinedListFormat), "\n",
       kFullUpdateKey, base::NumberToString(full_update_seconds), "\n"});
}

std::string ListSectionMarker(std::string_view url) {
  return base::StrCat({"! ===== ", url, " ====="});
}

bool IsTrustedScriptletSectionMarker(std::string_view line) {
  constexpr std::string_view kOpen = "! ===== ";
  constexpr std::string_view kClose = " =====";
  line = base::TrimWhitespaceASCII(line, base::TRIM_ALL);
  if (!line.starts_with(kOpen) || !line.ends_with(kClose) ||
      line.size() < kOpen.size() + kClose.size()) {
    return false;
  }
  const std::string_view label = line.substr(
      kOpen.size(), line.size() - kOpen.size() - kClose.size());
  // uBO's own repository, exactly: a prefix match on the full path, so a
  // look-alike account or repository ("uBlockOrigin/uAssets-mirror") does not
  // qualify.
  constexpr std::string_view kUbOrigin =
      "https://raw.githubusercontent.com/uBlockOrigin/uAssets/";
  // The bundled snapshot predates per-URL markers and labels its uBO part
  // this way; everything before it in that file is EasyList + EasyPrivacy.
  constexpr std::string_view kSnapshotUbo = "ZEPHYRUS-UBO-APPEND:";
  return label.starts_with(kUbOrigin) || label.starts_with(kSnapshotUbo);
}

std::optional<std::string_view> FindListSection(std::string_view combined,
                                                std::string_view url) {
  const std::string marker = base::StrCat({"\n", ListSectionMarker(url), "\n"});
  const size_t at = combined.find(marker);
  if (at == std::string_view::npos) {
    return std::nullopt;
  }
  const size_t start = at + marker.size();
  size_t end = combined.find("\n! ===== ", start);
  if (end == std::string_view::npos) {
    end = combined.size();
  }
  const std::string_view body = combined.substr(start, end - start);
  if (base::TrimWhitespaceASCII(body, base::TRIM_ALL).empty()) {
    return std::nullopt;
  }
  return body;
}

}  // namespace zephyrus_adblock
