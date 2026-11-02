// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/fuzz/adblock_fuzz_targets.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_cosmetic_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_param_filter.h"
#include "chrome/browser/zephyrus/adblock/adblock_regex_matcher.h"
#include "chrome/browser/zephyrus/adblock/adblock_scriptlet_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_urlskip_filter.h"
#include "chrome/browser/zephyrus/adblock/adblock_zindex.h"
#include "url/gurl.h"

namespace zephyrus_adblock::fuzz {

namespace {

// Hostile input can be large; a rule list the size of the real ones is a
// different job (and is tested elsewhere).
constexpr size_t kMaxInput = 128 * 1024;

struct Probe {
  GURL url;
  GURL initiator;
  ResourceType type;
};

// Requests worth asking about, made of the words in `text`: a rule that names a
// host is only exercised by a request to it.
std::vector<Probe> ProbesFor(std::string_view text) {
  std::vector<std::string> words;
  size_t i = 0;
  while (i < text.size() && words.size() < 8) {
    while (i < text.size() && !base::IsAsciiAlphaNumeric(text[i])) {
      ++i;
    }
    const size_t start = i;
    while (i < text.size() &&
           (base::IsAsciiAlphaNumeric(text[i]) || text[i] == '.' ||
            text[i] == '-' || text[i] == '_')) {
      ++i;
    }
    if (i - start >= 2 && i - start <= 40) {
      words.emplace_back(text.substr(start, i - start));
    }
  }
  std::vector<std::string> urls = {
      "https://ads.example.com/ad.js", "https://www.google-analytics.com/a.js",
      "https://news.example/article/index.html?utm_source=x&id=7&url=https%3A%2F%2Fexample.org%2F",
      "https://cdn.example/lib/v1/app.min.js"};
  for (const std::string& w : words) {
    urls.push_back("https://" + w + "/");
    urls.push_back("https://cdn.example/" + w + ".js?x=" + w);
    urls.push_back("https://" + w + ".example/p/a.gif?utm_source=1&url=https%3A%2F%2F" +
                   w + ".example%2F");
  }
  std::vector<Probe> probes;
  const GURL news("https://news.example/");
  for (const std::string& u : urls) {
    const GURL url(u);
    if (!url.is_valid()) {
      continue;
    }
    for (ResourceType type : {kTypeScript, kTypeImage, kTypeXhr,
                              kTypeSubdocument, kTypeDocument}) {
      probes.push_back({url, news, type});
    }
    probes.push_back({url, GURL("https://" + std::string(url.host()) + "/"),
                      kTypeScript});
  }
  return probes;
}

std::string Describe(const Probe& p) {
  return base::StrCat({p.url.spec(), " from ", p.initiator.spec(), " type ",
                       std::to_string(static_cast<int>(p.type))});
}

std::string_view Cap(std::string_view input) {
  return input.substr(0, kMaxInput);
}

// --- ListParser --------------------------------------------------------------

std::vector<uint8_t> BuildIndexOf(const AdblockFilterEngine& engine,
                                  bool* too_long) {
  AdblockZIndexBuilder builder;
  engine.ForEachRule([&](AdblockFilterEngine::RuleGroup group,
                         const FilterRule& rule, std::string_view) {
    builder.Add(group, rule);
  });
  std::optional<std::vector<uint8_t>> blob = builder.Finish();
  *too_long = !blob.has_value();
  return blob ? std::move(*blob) : std::vector<uint8_t>();
}

}  // namespace

std::string ListParser(std::string_view input) {
  input = Cap(input);
  AdblockFilterEngine engine;
  engine.AddRules(input);

  bool too_long = false;
  const std::vector<uint8_t> blob = BuildIndexOf(engine, &too_long);
  if (too_long) {
    return std::string();  // a pattern over 64 KB: documented, the engine keeps it
  }
  AdblockZIndex index;
  if (!index.Load(blob)) {
    return "an index built from the engine's own rules does not load";
  }
  for (const Probe& p : ProbesFor(input)) {
    const bool by_engine = engine.ShouldBlock(p.url, p.initiator, p.type);
    const bool by_index = index.ShouldBlock(p.url, p.initiator, p.type);
    if (by_engine != by_index) {
      return "engine " + std::to_string(by_engine) + " but index " +
             std::to_string(by_index) + " for " + Describe(p);
    }
    if (engine.ResolveRedirect(p.url, p.initiator, p.type) !=
        index.ResolveRedirect(p.url, p.initiator, p.type)) {
      return "stand-in differs between engine and index for " + Describe(p);
    }
  }
  const GURL page("https://news.example/");
  if (engine.GetDocumentExceptions(page) != index.GetDocumentExceptions(page)) {
    return "document exceptions differ between engine and index";
  }
  return std::string();
}

std::vector<uint8_t> SeedIndexBlob() {
  AdblockFilterEngine engine;
  engine.AddRules(
      "||ads.example^$script,third-party\n"
      "@@||ads.example/ok.js\n"
      "||track.example/p$image,domain=news.example|~sub.news.example\n"
      "/ad[0-9]+\\.js/$script\n"
      "||cdn.example/lib.js$script,redirect=noopjs:5\n"
      "@@||cdn.example/lib.js$redirect-rule=noopjs\n"
      "||x.example^$to=y.example|~z.y.example,important\n"
      "@@||news.example^$generichide\n");
  bool too_long = false;
  return BuildIndexOf(engine, &too_long);
}

// --- IndexBlob ---------------------------------------------------------------

std::string IndexBlob(std::string_view input) {
  input = input.substr(0, 512 * 1024);
  // The index reads its tables in place and needs 4-byte alignment, which a
  // mapped file and a vector both give. A fuzzer's buffer is not promised it.
  std::vector<uint32_t> storage((input.size() + 3) / 4);
  base::as_writable_byte_span(storage)
      .first(input.size())
      .copy_from(base::as_byte_span(input));
  const base::span<const uint8_t> bytes =
      base::as_byte_span(storage).first(input.size());
  AdblockZIndex index;
  if (!index.Load(bytes)) {
    return std::string();
  }
  // Load() accepted it: every lookup must now be safe, whatever it answers.
  for (const Probe& p : ProbesFor("ads.example track.example news.example")) {
    index.ShouldBlock(p.url, p.initiator, p.type);
    index.ResolveRedirect(p.url, p.initiator, p.type);
    index.GetDocumentExceptions(p.url);
  }
  return std::string();
}

// --- RegexMatcher --------------------------------------------------------------

std::string RegexMatcher(std::string_view input) {
  input = Cap(input);
  const scoped_refptr<const AdblockRegexMatcher> matcher =
      AdblockRegexMatcher::BuildFromList(input);
  for (const Probe& p : ProbesFor(input)) {
    matcher->Match(p.url, p.initiator, p.type);
  }
  return std::string();
}

// --- ParamFilter -----------------------------------------------------------------

std::string ParamFilter(std::string_view input) {
  input = Cap(input);
  AdblockParamFilter filter;
  filter.AddBuiltInRules();
  filter.AddRules(input);
  for (const Probe& p : ProbesFor(input)) {
    const std::optional<GURL> out = filter.Strip(p.url, p.initiator, p.type);
    if (!out) {
      continue;
    }
    // Everything but the removed parameters is left exactly as it was.
    if (!out->is_valid() || out->scheme() != p.url.scheme() ||
        out->host() != p.url.host() || out->path() != p.url.path() ||
        out->ref() != p.url.ref()) {
      return "a stripped URL changed more than its query: " + Describe(p);
    }
    const std::vector<std::string_view> before = base::SplitStringPiece(
        p.url.query(), "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    size_t at = 0;
    for (const std::string_view piece : base::SplitStringPiece(
             out->query(), "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      while (at < before.size() && before[at] != piece) {
        ++at;
      }
      if (at == before.size()) {
        return "a stripped URL has a query piece the original did not: " +
               Describe(p);
      }
      ++at;
    }
  }
  return std::string();
}

// --- UrlSkip ----------------------------------------------------------------------

namespace checks {

std::string SkipTarget(const GURL& from, const GURL& to) {
  if (!to.is_valid() || !to.SchemeIsHTTPOrHTTPS() || to.host().empty() ||
      to.has_username() || to.has_password() ||
      to.GetWithoutRef() == from.GetWithoutRef()) {
    return "a skip target that is not a plain, different web address: " +
           from.spec() + " -> " + to.possibly_invalid_spec();
  }
  return std::string();
}

}  // namespace checks

std::string UrlSkip(std::string_view input) {
  input = Cap(input);
  // The steps alone, on a URL carried in the same input.
  const size_t newline = input.find('\n');
  const std::string_view steps = input.substr(0, newline);
  const std::string_view rest =
      newline == std::string_view::npos ? std::string_view() : input.substr(newline + 1);
  const GURL given(rest.substr(0, rest.find('\n')));
  for (const GURL& url :
       {given, GURL("https://t.example/c?url=https%3A%2F%2Fnews.example%2Fa&u=aHR0cHM6Ly94LmV4YW1wbGUv#https://frag.example/")}) {
    if (const std::optional<GURL> out =
            AdblockUrlSkipFilter::ApplySteps(steps, url)) {
      if (std::string bad = checks::SkipTarget(url, *out); !bad.empty()) {
        return bad;
      }
    }
  }
  AdblockUrlSkipFilter filter;
  filter.AddRules(input, /*trust_unsectioned=*/true);
  for (const Probe& p : ProbesFor(input)) {
    for (const bool blocked : {false, true}) {
      if (const std::optional<GURL> out =
              filter.Skip(p.url, p.initiator, p.type, blocked)) {
        if (std::string bad = checks::SkipTarget(p.url, *out); !bad.empty()) {
          return bad;
        }
      }
    }
  }
  return std::string();
}

// --- CosmeticEngine ----------------------------------------------------------------

namespace checks {

// What a hiding selector may never do, derived from the contract stated in
// adblock_cosmetic_engine.cc and written again here so that the engine's own
// check is not what is being trusted: it is injected at user origin, which the
// page cannot override, so it must not close its rule, open a comment or leave
// a quote, bracket or parenthesis open to swallow the rules after it.
bool SelectorIsInert(std::string_view css) {
  if (css.find_first_of("{}<") != std::string_view::npos ||
      css.find("/*") != std::string_view::npos) {
    return false;
  }
  int parens = 0;
  int brackets = 0;
  char quote = 0;
  for (const char c : css) {
    if (quote) {
      quote = c == quote ? 0 : quote;
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '(') {
      ++parens;
    } else if (c == ')') {
      if (--parens < 0) {
        return false;
      }
    } else if (c == '[') {
      ++brackets;
    } else if (c == ']') {
      if (--brackets < 0) {
        return false;
      }
    }
  }
  return !quote && !parens && !brackets;
}

bool DeclarationsAreInert(std::string_view decls) {
  if (!SelectorIsInert(decls) || decls.find('\\') != std::string_view::npos ||
      decls.find('@') != std::string_view::npos) {
    return false;
  }
  const std::string lowered = base::ToLowerASCII(decls);
  for (const std::string_view banned :
       {"url(", "image-set(", "image(", "expression(", "src("}) {
    if (lowered.find(banned) != std::string::npos) {
      return false;
    }
  }
  return true;
}

}  // namespace checks

std::string CosmeticEngine(std::string_view input) {
  using checks::DeclarationsAreInert;
  using checks::SelectorIsInert;
  input = Cap(input);
  for (const bool generic : {false, true}) {
    AdblockCosmeticEngine engine;
    engine.AddRules(input, generic);
    for (const Probe& p : ProbesFor(input)) {
      if (p.type != kTypeScript) {
        continue;
      }
      for (const std::string& selector : engine.GetSelectorsForUrl(p.url)) {
        if (!SelectorIsInert(selector)) {
          return "a hiding selector that is not inert: " + selector;
        }
      }
      for (const std::string& selector : engine.GetGenericSelectorsForTokens(
               p.url, {"#ad", ".banner", "#" + std::string(p.url.host())})) {
        if (!SelectorIsInert(selector)) {
          return "a generic selector that is not inert: " + selector;
        }
      }
      for (const std::string& rule : engine.GetStyleRulesForUrl(p.url)) {
        const size_t open = rule.find('{');
        if (open == std::string::npos || rule.back() != '}' ||
            rule.find('{', open + 1) != std::string::npos ||
            rule.find('}') != rule.size() - 1 ||
            !SelectorIsInert(rule.substr(0, open)) ||
            !DeclarationsAreInert(rule.substr(open + 1, rule.size() - open - 2))) {
          return "a style rule that is not inert: " + rule;
        }
      }
    }
  }
  return std::string();
}

// --- ScriptletEngine -------------------------------------------------------------------

namespace checks {

// Reads a JSON string literal at `s[at]` (which must be a quote); the index just
// past its closing quote, or npos. A raw newline cannot be inside one.
size_t SkipStringLiteral(std::string_view s, size_t at) {
  if (at >= s.size() || s[at] != '"') {
    return std::string_view::npos;
  }
  for (size_t i = at + 1; i < s.size(); ++i) {
    if (s[i] == '\\') {
      ++i;
    } else if (s[i] == '"') {
      return i + 1;
    } else if (static_cast<unsigned char>(s[i]) < 0x20) {
      return std::string_view::npos;
    }
  }
  return std::string_view::npos;
}

// A call line is exactly `try{var f=zephyrusScriptlets["name"];if(f)f("a","b");}
// catch(e){}`: a name and arguments that are string literals and nothing else.
// If a list's text could end one literal and start code, it would not parse like
// this.
bool CallLineIsOnlyLiterals(std::string_view line) {
  constexpr std::string_view kStart = "try{var f=zephyrusScriptlets[";
  constexpr std::string_view kEnd = ");}catch(e){}";
  if (!line.starts_with(kStart) || !line.ends_with(kEnd)) {
    return false;
  }
  size_t at = SkipStringLiteral(line, kStart.size());
  if (at == std::string_view::npos || !line.substr(at).starts_with("];if(f)f(")) {
    return false;
  }
  at += std::string_view("];if(f)f(").size();
  const size_t end = line.size() - kEnd.size();
  if (at == end) {
    return true;  // no arguments
  }
  while (true) {
    at = SkipStringLiteral(line, at);
    if (at == std::string_view::npos || at > end) {
      return false;
    }
    if (at == end) {
      return true;
    }
    if (line[at] != ',') {
      return false;
    }
    ++at;
  }
}

}  // namespace checks

std::string ScriptletEngine(std::string_view input) {
  using checks::CallLineIsOnlyLiterals;
  input = Cap(input);
  for (const bool trusted : {false, true}) {
    AdblockScriptletEngine engine;
    engine.AddRules(input, trusted);
    for (const Probe& p : ProbesFor(input)) {
      if (p.type != kTypeScript) {
        continue;
      }
      const std::string script = engine.BuildInjectionScriptForUrl(p.url);
      for (const std::string_view line : base::SplitStringPiece(
               script, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
        if (line.starts_with("try{var f=zephyrusScriptlets[") &&
            !CallLineIsOnlyLiterals(line)) {
          return "an injected scriptlet call that is more than string literals: " +
                 std::string(line.substr(0, 200));
        }
      }
    }
  }
  return std::string();
}

// --- ListUtil --------------------------------------------------------------------------

std::string ListUtil(std::string_view input) {
  input = Cap(input);
  const std::string preprocessed = PreprocessFilterList(input);
  FindListIncludes(input);
  const size_t newline = input.find('\n');
  const std::string_view first = input.substr(0, newline);
  EvaluateListCondition(first);
  IsTrustedScriptletSectionMarker(first);
  ParseCombinedListHeader(input.substr(0, 4096));
  FindListSection(input, "https://x.example/list.txt");
  FindListSection(input, first);
  const std::string_view second =
      newline == std::string_view::npos ? std::string_view() : input.substr(newline + 1);
  DomainLookupKeys(first.substr(0, 253));
  HostMatchesFilterDomain(first.substr(0, 253), second.substr(0, second.find('\n')));
  // The combined list the blocker builds is read back by the same parsers.
  PreprocessFilterList(preprocessed);
  return std::string();
}

base::span<const Target> Targets() {
  static constexpr Target kTargets[] = {
      {"list_parser", &ListParser},     {"index_blob", &IndexBlob},
      {"regex_matcher", &RegexMatcher}, {"param_filter", &ParamFilter},
      {"urlskip", &UrlSkip},            {"cosmetic", &CosmeticEngine},
      {"scriptlet", &ScriptletEngine},  {"list_util", &ListUtil},
  };
  return kTargets;
}

}  // namespace zephyrus_adblock::fuzz
