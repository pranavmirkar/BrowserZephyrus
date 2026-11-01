// Real traffic through the shipped engine and ZIndex: decisions and per-request cost.
#define private public
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#undef private
#include "common.h"
#include "zindex.h"
#include <map>
using namespace zephyrus_adblock;
static int TypeBit(const std::string& t){ if(t=="script")return 1<<1; if(t=="image")return 1<<2; if(t=="stylesheet")return 1<<3; if(t=="object")return 1<<4; if(t=="xhr")return 1<<5; if(t=="subdocument")return 1<<6; if(t=="font")return 1<<7; if(t=="media")return 1<<8; if(t=="ping")return 1<<9; if(t=="websocket")return 1<<10; if(t=="document")return 1<<12; return 1<<0; }
int main(int argc,char**argv){ std::string tsv=argv[1]; std::vector<std::string> files(argv+2,argv+argc); std::string text=Combined(files);
  auto base=std::make_unique<AdblockFilterEngine>(); base->AddRules(text);
  zidx::Builder bld; auto feed=[&](zidx::Group g,const RuleBuckets& m,const std::vector<FilterRule>& un){ for(auto&kv:m) for(auto&r:kv.second) bld.Add(g,r,kv.first); for(auto&r:un) bld.Add(g,r,""); };
  feed(zidx::kBlock,base->block_buckets_,base->untokenized_block_); feed(zidx::kException,base->exception_buckets_,base->untokenized_exception_); feed(zidx::kImportant,base->important_buckets_,base->untokenized_important_); feed(zidx::kDocument,base->document_buckets_,base->untokenized_document_);
  auto blob=bld.Finish(); zidx::Index idx; if(!idx.Load(blob.data(),blob.size())) return 1;
  struct R{ GURL u,i; int t; int page; }; std::vector<R> reqs; std::ifstream f(tsv); std::string l;
  while(std::getline(f,l)){ std::vector<std::string> c; size_t s=0; for(size_t k=0;k<=l.size();++k) if(k==l.size()||l[k]=='\t'){ c.push_back(l.substr(s,k-s)); s=k+1; } reqs.push_back({GURL(c[1]),GURL(c[2]),TypeBit(c[3]),atoi(c[0].c_str())}); }
  for(int w=0;w<2;++w) for(auto&r:reqs){ base->ShouldBlock(r.u,r.i,(ResourceType)r.t); idx.ShouldBlock(r.u,r.i,(ResourceType)r.t); }
  std::vector<double> tb,tz; std::vector<int> db,dz; size_t mism=0; std::map<int,double> page_b,page_z;
  for(auto&r:reqs){ auto a=Clock::now(); bool x=base->ShouldBlock(r.u,r.i,(ResourceType)r.t); auto b=Clock::now(); bool y=idx.ShouldBlock(r.u,r.i,(ResourceType)r.t); auto c=Clock::now(); double ub=Ms(a,b)*1000, uz=Ms(b,c)*1000; tb.push_back(ub); tz.push_back(uz); db.push_back(x); dz.push_back(y); mism+=x!=y; page_b[r.page]+=ub; page_z[r.page]+=uz; }
  auto mean=[](std::vector<double>&v){ double s=0; for(double x:v) s+=x; return s/v.size(); }; auto pct=[](std::vector<double> v,double p){ std::sort(v.begin(),v.end()); return v[(size_t)(v.size()*p)]; };
  printf("REAL TRAFFIC: %zu requests on %zu pages\n", reqs.size(), page_b.size());
  printf("shipped engine : mean %.2f us  p50 %.2f  p99 %.2f  max %.0f | blocked %d (%.1f%%)\n", mean(tb), pct(tb,.5), pct(tb,.99), *std::max_element(tb.begin(),tb.end()), std::accumulate(db.begin(),db.end(),0), 100.0*std::accumulate(db.begin(),db.end(),0)/db.size());
  printf("ZIndex         : mean %.2f us  p50 %.2f  p99 %.2f  max %.0f | blocked %d | decisions differing from shipped engine: %zu\n", mean(tz), pct(tz,.5), pct(tz,.99), *std::max_element(tz.begin(),tz.end()), std::accumulate(dz.begin(),dz.end(),0), mism);
  double pb=0,pz=0; for(auto&kv:page_b) pb+=kv.second; for(auto&kv:page_z) pz+=kv.second; printf("per page (avg %zu requests): matching costs %.2f ms shipped vs %.2f ms ZIndex\n", reqs.size()/page_b.size(), pb/page_b.size()/1000, pz/page_z.size()/1000);
  std::ofstream o("real_zeph_decisions.txt"); for(size_t i=0;i<db.size();++i) o<<db[i]<<"\t"<<dz[i]<<"\n"; }
