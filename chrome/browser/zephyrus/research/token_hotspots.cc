#define private public
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#undef private
#include <map>
#include "base/strings/string_util.h"
#include "common.h"
using namespace zephyrus_adblock;
int main(int argc,char**argv){ std::vector<std::string> files(argv+1,argv+argc); std::string text=Combined(files); AdblockFilterEngine e; e.AddRules(text);
  auto corpus=MakeCorpus(text,20000); std::map<std::string,double> load; std::map<std::string,int> hits; size_t n=0;
  for(auto&r:corpus){ if(r.expect_ad) continue; ++n; GURL u(r.url); std::string l=base::ToLowerASCII(u.spec()); size_t s=0; for(size_t i=0;i<=l.size();++i){ if(i<l.size()&&base::IsAsciiAlphaNumeric(l[i])) continue; if(i-s>=3){ std::string t=l.substr(s,i-s); auto it=e.block_buckets_.find(t); if(it!=e.block_buckets_.end()){ load[t]+=it->second.size(); hits[t]++; } } s=i+1; } }
  std::vector<std::pair<double,std::string>> v; for(auto&kv:load) v.push_back({kv.second/n,kv.first}); std::sort(v.rbegin(),v.rend()); double tot=0; for(auto&p:v) tot+=p.first;
  printf("avg rules reached through tokens per clean request: %.0f\n", tot); for(int i=0;i<14;++i) printf("  %-14s bucket=%5zu rules  in %4.1f%% of requests -> %6.1f rules/request\n", v[i].second.c_str(), e.block_buckets_[v[i].second].size(), 100.0*hits[v[i].second]/n, v[i].first);
  // what do those bucketed rules look like?
  for(const char* tk: {"com","static","js","images","cdn","api"}){ auto it=e.block_buckets_.find(tk); if(it==e.block_buckets_.end()) continue; printf("  sample rules under '%s': ",tk); int c=0; for(auto&r:it->second){ if(c++>=4) break; printf("[%s%s%s] ", r.anchor==2?"||":"", r.pattern.c_str(), r.anchor_end?"|":""); } printf("\n"); } }
