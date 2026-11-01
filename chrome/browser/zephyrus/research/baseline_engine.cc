#define private public
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_cosmetic_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_scriptlet_engine.h"
#undef private
#include "common.h"
using namespace zephyrus_adblock;
int main(int argc, char** argv){
  std::vector<std::string> files(argv+1, argv+argc);
  std::string text = Combined(files);
  printf("input: %zu files, %.1f MB, %zu lines\n", files.size(), text.size()/1e6, (size_t)std::count(text.begin(),text.end(),'\n'));
  size_t h0=HeapInUse(), r0=RssKb();
  auto t0=Clock::now();
  auto net = std::make_unique<AdblockFilterEngine>(); size_t parsed = net->AddRules(text);
  auto t1=Clock::now(); size_t h1=HeapInUse();
  auto cos = std::make_unique<AdblockCosmeticEngine>(); cos->AddRules(text, true);
  auto t2=Clock::now(); size_t h2=HeapInUse();
  auto scr = std::make_unique<AdblockScriptletEngine>(); scr->AddRules(text, false);
  auto t3=Clock::now(); size_t h3=HeapInUse();
  printf("PARSE   network %.0f ms (%zu rules)   cosmetic %.0f ms   scriptlet %.0f ms   total %.0f ms\n", Ms(t0,t1), parsed, Ms(t1,t2), Ms(t2,t3), Ms(t0,t3));
  printf("HEAP    network %.1f MB   cosmetic %.1f MB   scriptlet %.1f MB   total %.1f MB   (RSS +%.0f MB, text itself %.1f MB)\n", (h1-h0)/1e6,(h2-h1)/1e6,(h3-h2)/1e6,(h3-h0)/1e6,(RssKb()-r0)/1e3, text.size()/1e6);
  size_t tok=0; for(auto&kv: net->block_buckets_) tok+=kv.second.size();
  printf("SHAPE   block buckets %zu holding %zu rules | untokenized: block %zu, exception %zu, important %zu, document %zu | exception buckets %zu\n", net->block_buckets_.size(), tok, net->untokenized_block_.size(), net->untokenized_exception_.size(), net->untokenized_important_.size(), net->untokenized_document_.size(), net->exception_buckets_.size());
  size_t big=0, bigk=0; std::string bigname; for(auto&kv: net->block_buckets_){ if(kv.second.size()>big){big=kv.second.size(); bigname=kv.first;} } 
  std::vector<size_t> sizes; for(auto&kv: net->block_buckets_) sizes.push_back(kv.second.size()); std::sort(sizes.begin(),sizes.end());
  printf("        largest bucket '%s' = %zu rules; p99 bucket = %zu; sizeof(FilterRule)=%zu bytes\n", bigname.c_str(), big, sizes[sizes.size()*99/100], sizeof(FilterRule));
  auto corpus = MakeCorpus(text, 20000);
  // warm
  for(size_t i=0;i<2000;++i){ net->ShouldBlock(GURL(corpus[i].url), GURL(corpus[i].initiator), (ResourceType)corpus[i].type); }
  std::vector<double> lat_clean, lat_ad; size_t blocked=0, ads=0, ad_blocked=0, clean_blocked=0;
  for(auto& r: corpus){ GURL u(r.url), ini(r.initiator); auto a=Clock::now(); bool b=net->ShouldBlock(u, ini, (ResourceType)r.type); auto c=Clock::now(); double us=Ms(a,c)*1000; (r.expect_ad?lat_ad:lat_clean).push_back(us); blocked+=b; if(r.expect_ad){++ads; ad_blocked+=b;} else clean_blocked+=b; }
  auto pct=[](std::vector<double> v,double p){ std::sort(v.begin(),v.end()); return v[(size_t)(v.size()*p)]; };
  auto mean=[](const std::vector<double>&v){ return std::accumulate(v.begin(),v.end(),0.0)/v.size(); };
  printf("MATCH   clean requests (n=%zu): mean %.1f us  p50 %.1f  p99 %.1f  max %.0f | ad-shaped (n=%zu): mean %.1f us  p99 %.1f | blocked %zu (ads blocked %zu/%zu, clean blocked %zu)\n", lat_clean.size(), mean(lat_clean), pct(lat_clean,.5), pct(lat_clean,.99), *std::max_element(lat_clean.begin(),lat_clean.end()), lat_ad.size(), mean(lat_ad), pct(lat_ad,.99), blocked, ad_blocked, ads, clean_blocked);
  double per_page_clean = mean(lat_clean)*150/1000.0;
  printf("        => a page making 150 subresource requests spends ~%.1f ms of UI-thread time on matching alone\n", per_page_clean);
  auto d0=Clock::now(); net.reset(); auto d1=Clock::now(); cos.reset(); auto d2=Clock::now(); scr.reset(); auto d3=Clock::now();
  printf("DESTROY network %.1f ms   cosmetic %.1f ms   scriptlet %.1f ms   (on the UI thread when a list hot-swaps)\n", Ms(d0,d1),Ms(d1,d2),Ms(d2,d3));
}
