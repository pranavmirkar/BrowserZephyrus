// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

// The fuzz targets (fuzz/adblock_fuzz_targets.h), driven without libFuzzer: every
// seed, then a deterministic stream of mutations of the seeds, through each
// parser. It runs with the ordinary unit tests, so a parser that crashes, hangs
// or breaks an invariant on hostile text is found in a normal test run, and in a
// sanitizer build of this binary it finds memory errors too. libFuzzer
// (fuzz/*_fuzzer.cc) does the same coverage-guided and for far longer.
//
// ZEPHYRUS_FUZZ_ITERS       mutations per target (default 1500)
// ZEPHYRUS_FUZZ_SEED        a different deterministic stream
// ZEPHYRUS_FUZZ_REAL_LIST   a filter list: windows of it join the seeds (real
//                           lists hold syntax the hand-written seeds do not)
// ZEPHYRUS_FUZZ_OUT         a directory: every failing or slow input is saved
// ZEPHYRUS_FUZZ_DUMP_SEEDS  a directory: writes the binary seeds for libFuzzer

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/environment.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/cstring_view.h"
#include "base/time/time.h"
#include "chrome/browser/zephyrus/adblock/fuzz/adblock_fuzz_targets.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_adblock::fuzz {
namespace {

// A deterministic generator, so a failure can be reproduced from its seed.
class Rng {
 public:
  explicit Rng(uint64_t seed) : state_(seed * 2685821657736338717ull + 1) {}
  uint64_t Next() {
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    return state_ * 2685821657736338717ull;
  }
  size_t Below(size_t n) { return n ? static_cast<size_t>(Next() % n) : 0; }

 private:
  uint64_t state_;
};

const std::vector<std::string>& Tokens() {
  static const base::NoDestructor<std::vector<std::string>> kTokens({
      "||", "@@", "^", "*", "$", ",", "~", "/", "\\", "##", "#@#", "#?#", "#$#",
      "#%#", "+js(", ":style(", ":has-text(", "(?=", "(?!", "(?<=", "\\1",
      "{400,1000}", "domain=", "from=", "to=", "denyallow=", "third-party",
      "script", "image", "xhr", "doc", "popup", "important", "badfilter",
      "match-case", "redirect=", "redirect-rule=", "redirect-rule", "noopjs:10",
      "google-ima.js", "removeparam=", "urlskip=", "?url", "&1", " -base64",
      " -uricomponent", " -blocked", " +https", "generichide", "!#if ",
      "!#else", "!#endif", "! ===== ",
      "https://raw.githubusercontent.com/uBlockOrigin/uAssets/", "trusted-",
      "url(", "expression(", "/*", "}", "{", "https://", "%3A%2F%2F", "%00",
      std::string(1, '\0'), "\n", "\r\n", "\xff\xfe", "\xc0\xaf", "\xed\xa0\x80"});
  return *kTokens;
}

std::string Mutate(std::string s, const std::vector<std::string>& pool,
                   Rng& rng) {
  const size_t steps = 1 + rng.Below(4);
  for (size_t k = 0; k < steps; ++k) {
    switch (rng.Below(10)) {
      case 0:  // flip a bit
        if (!s.empty()) {
          s[rng.Below(s.size())] ^= static_cast<char>(1u << rng.Below(8));
        }
        break;
      case 1:  // an interesting byte
        if (!s.empty()) {
          static constexpr std::array<char, 26> kBytes = {
              0,   '\n', '\r', '"',  '\'',   '\\',   '(', ')', '[',
              ']', '{',  '}',  '$',  '/',    '*',    '^', '|', '@',
              '#', ',',  '~',  '\x7f', '\xff', '\x80', ' ', '\t'};
          s[rng.Below(s.size())] = kBytes[rng.Below(kBytes.size())];
        }
        break;
      case 2: {  // insert random bytes
        std::string junk;
        for (size_t n = 1 + rng.Below(8); n; --n) {
          junk += static_cast<char>(rng.Next());
        }
        s.insert(rng.Below(s.size() + 1), junk);
        break;
      }
      case 3:  // delete a range
        if (!s.empty()) {
          const size_t at = rng.Below(s.size());
          s.erase(at, 1 + rng.Below(16));
        }
        break;
      case 4:  // duplicate a range
        if (!s.empty()) {
          const size_t at = rng.Below(s.size());
          const std::string piece = s.substr(at, 1 + rng.Below(48));
          s.insert(rng.Below(s.size() + 1), piece);
        }
        break;
      case 5:  // a dictionary token
        s.insert(rng.Below(s.size() + 1), Tokens()[rng.Below(Tokens().size())]);
        break;
      case 6:  // a piece of another seed
        if (!pool.empty()) {
          const std::string& other = pool[rng.Below(pool.size())];
          if (!other.empty()) {
            const size_t at = rng.Below(other.size());
            s.insert(rng.Below(s.size() + 1), other.substr(at, 1 + rng.Below(160)));
          }
        }
        break;
      case 7: {  // swap two lines
        std::vector<std::string> lines =
            base::SplitString(s, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
        if (lines.size() > 1) {
          std::swap(lines[rng.Below(lines.size())], lines[rng.Below(lines.size())]);
          s = base::JoinString(lines, "\n");
        }
        break;
      }
      case 8:  // truncate
        s.resize(rng.Below(s.size() + 1));
        break;
      default:  // repeat a character many times (long runs find quadratic code)
        if (!s.empty()) {
          s.insert(rng.Below(s.size() + 1),
                   std::string(1 + rng.Below(600), s[rng.Below(s.size())]));
        }
        break;
    }
    if (s.size() > 200 * 1024) {
      s.resize(200 * 1024);
    }
  }
  return s;
}

std::optional<base::FilePath> CorpusDir(std::string_view name) {
  base::FilePath root;
  if (!base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root)) {
    return std::nullopt;
  }
  return root.AppendASCII("chrome/browser/zephyrus/adblock/fuzz/corpus")
      .AppendASCII(name);
}

std::vector<std::string> ReadCorpus(std::string_view name) {
  std::vector<std::string> out;
  if (const std::optional<base::FilePath> dir = CorpusDir(name)) {
    base::FileEnumerator files(*dir, false, base::FileEnumerator::FILES);
    for (base::FilePath f = files.Next(); !f.empty(); f = files.Next()) {
      std::string text;
      if (base::ReadFileToString(f, &text)) {
        out.push_back(std::move(text));
      }
    }
  }
  return out;
}

// Which seed directories feed which target.
std::vector<std::string> SeedsFor(std::string_view target,
                                  const std::vector<std::string>& real) {
  std::vector<std::string> seeds;
  const auto add = [&](std::string_view dir) {
    for (std::string& s : ReadCorpus(dir)) {
      seeds.push_back(std::move(s));
    }
  };
  if (target == "index_blob") {
    const std::vector<uint8_t> blob = SeedIndexBlob();
    seeds.emplace_back(blob.begin(), blob.end());
    seeds.emplace_back();
    seeds.emplace_back(std::string(64, '\0'));
    return seeds;
  }
  static const std::pair<std::string_view, std::string_view> kDirs[] = {
      {"list_parser", "list"},  {"regex_matcher", "regex"},
      {"param_filter", "param"}, {"urlskip", "urlskip"},
      {"cosmetic", "cosmetic"},  {"scriptlet", "scriptlet"},
      {"list_util", "list_util"}};
  for (const auto& [name, dir] : kDirs) {
    if (name == target) {
      add(dir);
    }
  }
  add("list");
  seeds.insert(seeds.end(), real.begin(), real.end());
  return seeds;
}

std::vector<std::string> RealSeeds(Rng& rng) {
  std::vector<std::string> out;
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  const std::optional<std::string> path = env->GetVar("ZEPHYRUS_FUZZ_REAL_LIST");
  std::string list;
  if (!path || !base::ReadFileToString(base::FilePath::FromUTF8Unsafe(*path), &list)) {
    return out;
  }
  const std::vector<std::string_view> lines = base::SplitStringPiece(
      list, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (int i = 0; i < 80 && lines.size() > 40; ++i) {
    const size_t start = rng.Below(lines.size() - 30);
    std::string window;
    for (size_t k = 0; k < 4 + rng.Below(26); ++k) {
      window.append(lines[start + k]);
      window.push_back('\n');
    }
    out.push_back(std::move(window));
  }
  return out;
}

int EnvInt(base::cstring_view name, int fallback) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  int value = fallback;
  if (const std::optional<std::string> text = env->GetVar(name)) {
    base::StringToInt(*text, &value);
  }
  return value;
}

void Save(const std::string& input, std::string_view target, std::string_view why) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  const std::optional<std::string> dir = env->GetVar("ZEPHYRUS_FUZZ_OUT");
  if (!dir) {
    return;
  }
  static int counter = 0;
  base::WriteFile(
      base::FilePath::FromUTF8Unsafe(*dir).AppendASCII(
          std::string(target) + "_" + std::string(why) + "_" +
          std::to_string(++counter) + ".bin"),
      input);
}

// A hang is any single input taking this long.
constexpr base::TimeDelta kSlow = base::Seconds(3);

TEST(AdblockFuzzRegression, EveryTargetSurvivesItsSeedsAndTheirMutations) {
  const int iterations = EnvInt("ZEPHYRUS_FUZZ_ITERS", 1500);
  Rng rng(static_cast<uint64_t>(EnvInt("ZEPHYRUS_FUZZ_SEED", 1)));
  const std::vector<std::string> real = RealSeeds(rng);
  std::cout << "\n=== fuzz regression: " << iterations
            << " mutations per target, " << real.size()
            << " real-list seeds ===\n";
  for (const Target& target : Targets()) {
    const std::vector<std::string> seeds = SeedsFor(target.name, real);
    ASSERT_FALSE(seeds.empty()) << target.name;
    size_t runs = 0;
    size_t failures = 0;
    base::TimeDelta slowest;
    const base::TimeTicks started = base::TimeTicks::Now();
    const auto run = [&](const std::string& input) {
      const base::TimeTicks t0 = base::TimeTicks::Now();
      const std::string error = target.run(input);
      const base::TimeDelta took = base::TimeTicks::Now() - t0;
      ++runs;
      slowest = std::max(slowest, took);
      if (took > kSlow) {
        ADD_FAILURE() << target.name << " took " << took.InMilliseconds()
                      << " ms on a " << input.size() << " byte input";
        Save(input, target.name, "slow");
      }
      if (!error.empty() && ++failures <= 3) {
        ADD_FAILURE() << target.name << ": " << error;
        Save(input, target.name, "invariant");
      }
    };
    for (const std::string& seed : seeds) {
      run(seed);
    }
    for (int i = 0; i < iterations && failures == 0; ++i) {
      run(Mutate(seeds[rng.Below(seeds.size())], seeds, rng));
    }
    std::cout << "  " << target.name << ": " << runs << " inputs, slowest "
              << slowest.InMilliseconds() << " ms, "
              << (base::TimeTicks::Now() - started).InSecondsF() << " s, "
              << failures << " failures\n";
  }
}

TEST(AdblockFuzzRegression, TheSeedsAreFoundAndThinkingAboutThemIsNotPointless) {
  // A corpus directory that moved would leave every target fuzzing nothing but
  // its built-in strings.
  for (const char* dir : {"list", "regex", "param", "urlskip", "cosmetic",
                          "scriptlet", "list_util"}) {
    EXPECT_FALSE(ReadCorpus(dir).empty()) << dir;
  }
  EXPECT_GT(SeedIndexBlob().size(), 1000u);
}

TEST(AdblockFuzzRegression, ASeedIndexIsLoadableAndTheTargetsHoldOnAValidOne) {
  const std::vector<uint8_t> blob = SeedIndexBlob();
  EXPECT_EQ(IndexBlob(std::string(blob.begin(), blob.end())), "");
  EXPECT_EQ(ListParser(
                "||ads.example^$script\n@@||ads.example/ok.js\n"
                "||cdn.example/lib.js$script,redirect=noopjs\n"),
            "");
}

// A check that cannot fail proves nothing: each invariant must refuse a value
// that breaks it, and accept one that does not.
TEST(AdblockFuzzChecks, SelectorIsInert) {
  for (const char* ok : {"div.ad", "a[href^=\"https://x.example/\"]",
                         ".a:not(.b, .c)", "#ad > .x ~ y", "a[title='x y']"}) {
    EXPECT_TRUE(checks::SelectorIsInert(ok)) << ok;
  }
  // Deliberately strict: a brace is refused even inside a quoted string.
  for (const char* bad :
       {"a}", "a{b:c}", "a[title='}']", "a/*", "a[href", "a)", "\"x", "a'", "a<b", "a]",
       "a:not(b", "x}\n*{display:none"}) {
    EXPECT_FALSE(checks::SelectorIsInert(bad)) << bad;
  }
}

TEST(AdblockFuzzChecks, DeclarationsAreInert) {
  for (const char* ok : {"display:none!important", "color:red;margin:0",
                         "content:\"x\""}) {
    EXPECT_TRUE(checks::DeclarationsAreInert(ok)) << ok;
  }
  for (const char* bad :
       {"background:url(https://t.example/p.gif)", "background:URL(x)",
        "b:image-set(x 1x)", "x:expression(alert(1))", "a:b\\c", "@import x",
        "a:b}", "a:{", "c:src(x)", "d:\"unclosed"}) {
    EXPECT_FALSE(checks::DeclarationsAreInert(bad)) << bad;
  }
}

TEST(AdblockFuzzChecks, CallLineIsOnlyLiterals) {
  const auto line = [](std::string_view args) {
    return "try{var f=zephyrusScriptlets[\"set-constant\"];if(f)f(" +
           std::string(args) + ");}catch(e){}";
  };
  for (const char* ok : {"", "\"a\"", "\"a\",\"b\"", "\"q\\\"uote\",\"\\\\\"",
                         "\"1\",\"\",\"x,y\""}) {
    EXPECT_TRUE(checks::CallLineIsOnlyLiterals(line(ok))) << ok;
  }
  for (const char* bad :
       {"\"a\"+alert(1)+\"b\"", "\"a\",alert(1)", "alert(1)", "\"a\" \"b\"",
        "\"a\",", ",\"a\"", "\"unterminated", "\"a\\\"", "\"line\nbreak\"",
        "\"a\");alert(1);//", "1"}) {
    EXPECT_FALSE(checks::CallLineIsOnlyLiterals(line(bad))) << bad;
  }
  EXPECT_FALSE(checks::CallLineIsOnlyLiterals("alert(1)"));
  EXPECT_FALSE(checks::CallLineIsOnlyLiterals(
      "try{var f=zephyrusScriptlets[x];if(f)f();}catch(e){}"));
}

TEST(AdblockFuzzChecks, SkipTarget) {
  const GURL from("https://t.example/c?url=x");
  EXPECT_EQ(checks::SkipTarget(from, GURL("https://news.example/a#frag")), "");
  EXPECT_EQ(checks::SkipTarget(from, GURL("http://news.example/")), "");
  for (const char* bad :
       {"javascript:alert(1)", "data:text/html,x", "file:///c:/x",
        "https://u:p@news.example/", "https://u@news.example/", "https://",
        "not a url", "https://t.example/c?url=x#other", "ftp://news.example/"}) {
    EXPECT_NE(checks::SkipTarget(from, GURL(bad)), "") << bad;
  }
}

TEST(AdblockFuzzRegression, DumpSeedsForLibFuzzer) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  const std::optional<std::string> dir = env->GetVar("ZEPHYRUS_FUZZ_DUMP_SEEDS");
  if (!dir) {
    GTEST_SKIP() << "set ZEPHYRUS_FUZZ_DUMP_SEEDS to write the binary seeds";
  }
  const base::FilePath out = base::FilePath::FromUTF8Unsafe(*dir);
  ASSERT_TRUE(base::CreateDirectory(out));
  const std::vector<uint8_t> blob = SeedIndexBlob();
  ASSERT_TRUE(base::WriteFile(out.AppendASCII("index_blob_seed.bin"), blob));
}

}  // namespace
}  // namespace zephyrus_adblock::fuzz
