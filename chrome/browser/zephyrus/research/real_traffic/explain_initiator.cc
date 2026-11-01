// For requests whose verdict changes when the tab, not the issuing frame, is the initiator: which block rule decides it?
//   explain_initiator real.tsv decisions_frame.txt decisions_top.txt <list files...>
// It calls the shipped engine's own per-rule matcher one rule at a time, so the answer is the engine's, not a re-implementation.
#define private public
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#undef private
#include "base/strings/string_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"
#include "common.h"
#include <map>
using namespace zephyrus_adblock;

static int TypeBit(const std::string& t) {
  if (t == "script") return 1 << 1;      if (t == "image") return 1 << 2;      if (t == "stylesheet") return 1 << 3;
  if (t == "object") return 1 << 4;      if (t == "xhr") return 1 << 5;        if (t == "subdocument") return 1 << 6;
  if (t == "font") return 1 << 7;        if (t == "media") return 1 << 8;      if (t == "ping") return 1 << 9;
  if (t == "websocket") return 1 << 10;  if (t == "document") return 1 << 12;  return 1 << 0;
}

static std::string Describe(const FilterRule& r) {
  std::string s = r.is_exception ? "@@" : "";
  s += r.anchor == FilterRule::kAnchorHostname ? "||" : r.anchor == FilterRule::kAnchorStart ? "|" : "";
  s += r.pattern;
  if (r.anchor_end) s += "|";
  s += r.party == 2 ? "  $third-party" : r.party == 1 ? "  $first-party" : "";
  for (auto& d : r.domains_included) s += "  domain=" + d;
  for (auto& d : r.domains_excluded) s += "  domain=~" + d;
  return s;
}

int main(int argc, char** argv) {
  if (argc < 5) { fprintf(stderr, "usage: explain_initiator real.tsv decisions_frame.txt decisions_top.txt lists...\n"); return 2; }
  std::vector<std::string> files(argv + 4, argv + argc);
  auto engine = std::make_unique<AdblockFilterEngine>();
  engine->AddRules(Combined(files));

  std::vector<std::vector<std::string>> rows;
  { std::ifstream f(argv[1]); std::string l;
    while (std::getline(f, l)) { std::vector<std::string> c; size_t s = 0;
      for (size_t k = 0; k <= l.size(); ++k) if (k == l.size() || l[k] == '\t') { c.push_back(l.substr(s, k - s)); s = k + 1; }
      rows.push_back(c); } }
  std::vector<int> frame, top;
  { std::ifstream f(argv[2]); int a, b; while (f >> a >> b) frame.push_back(a); }
  { std::ifstream f(argv[3]); int a, b; while (f >> a >> b) top.push_back(a); }

  std::vector<const FilterRule*> block;
  for (auto& kv : engine->block_buckets_) for (auto& r : kv.second) block.push_back(&r);
  for (auto& r : engine->untokenized_block_) block.push_back(&r);

  std::map<std::string, int> by_rule;
  int moved = 0;
  for (size_t i = 0; i < rows.size(); ++i) {
    if (frame[i] || !top[i]) continue;
    ++moved;
    GURL url(rows[i][1]), tab(rows[i][11]);
    const std::string lower = base::ToLowerASCII(url.spec());
    const bool third_party = !net::registry_controlled_domains::SameDomainOrHost(
        url, tab, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
    for (const FilterRule* r : block) {
      RuleBuckets none; std::vector<FilterRule> one{*r};
      if (engine->AnyRuleMatches(none, one, lower, tab.host(), third_party, (ResourceType)TypeBit(rows[i][3])))
        ++by_rule[Describe(*r)];
    }
  }
  printf("%d requests are blocked only when the tab is the initiator. Block rules that fire for them:\n", moved);
  std::vector<std::pair<int, std::string>> sorted;
  for (auto& kv : by_rule) sorted.push_back({kv.second, kv.first});
  std::sort(sorted.rbegin(), sorted.rend());
  for (size_t i = 0; i < sorted.size() && i < 12; ++i) printf("  %3d  %s\n", sorted[i].first, sorted[i].second.c_str());
  return 0;
}
