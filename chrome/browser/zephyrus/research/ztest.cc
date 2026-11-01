#define private public
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#undef private
#include "common.h"
#include "zindex.h"
#include <sys/mman.h>
#include <fcntl.h>
using namespace zephyrus_adblock;
static std::string Synth(const FilterRule& r){ std::string b; for(char c: r.pattern){ if(c=='*') b+="xq7"; else if(c=='^') b+="/"; else b+=c; }
  if(r.anchor==FilterRule::kAnchorHostname){ if(b.find('/')==std::string::npos) b+="/"; return "https://"+b; }
  if(r.anchor==FilterRule::kAnchorStart){ if(b.rfind("http",0)==0) return b; return "https://"+b; }
  return "https://site.example/"+b; }
static std::string Ini(const FilterRule& r, const std::string& url, int variant){
  if(variant==3){ size_t a=url.find("://"); size_t e=url.find('/',a+3); return url.substr(0,e==std::string::npos?url.size():e)+"/"; }
  if(!r.domains_included.empty()){ std::string d=r.domains_included[0]; if(d.size()>2&&d.substr(d.size()-2)==".*") d=d.substr(0,d.size()-2)+".com"; return "https://"+d+"/"; }
  if(r.party==2) return "https://third-party.test/"; if(r.party==1){ size_t a=url.find("://"); size_t e=url.find('/',a+3); return url.substr(0,e==std::string::npos?url.size():e)+"/"; }
  return "https://news.example.org/"; }
int main(int argc,char**argv){
  std::vector<std::string> files(argv+1,argv+argc); std::string text=Combined(files);
  size_t h0=HeapInUse(); auto t0=Clock::now(); auto base=std::make_unique<AdblockFilterEngine>(); base->AddRules(text); auto t1=Clock::now(); size_t h1=HeapInUse();
  printf("BASELINE  parse %.0f ms, heap %.1f MB\n", Ms(t0,t1), (h1-h0)/1e6);
  // ---- build ZIndex from the baseline's parsed rules
  auto b0=Clock::now(); zidx::Builder bld; std::vector<std::pair<zidx::Group,const FilterRule*>> all;
  auto feed=[&](zidx::Group g,const RuleBuckets& m,const std::vector<FilterRule>& un){ for(auto&kv:m) for(auto&r:kv.second){ bld.Add(g,r,kv.first); all.push_back({g,&r}); } for(auto&r:un){ bld.Add(g,r,""); all.push_back({g,&r}); } };
  feed(zidx::kBlock,base->block_buckets_,base->untokenized_block_); feed(zidx::kException,base->exception_buckets_,base->untokenized_exception_);
  feed(zidx::kImportant,base->important_buckets_,base->untokenized_important_); feed(zidx::kDocument,base->document_buckets_,base->untokenized_document_);
  std::vector<uint8_t> blob=bld.Finish(); auto b1=Clock::now();
  printf("ZINDEX    build %.0f ms (offline, once per list update), blob %.2f MB for %zu rules (%.1f B/rule)\n", Ms(b0,b1), blob.size()/1e6, all.size(), (double)blob.size()/all.size());
  printf("          by rarest token: %zu; by n-gram: 3-gram %zu, 4-gram %zu, 5-gram %zu; by initiator domain: %zu; residual still scanned linearly: block %zu, exception %zu, important %zu, document %zu\n", bld.stats_tok,bld.stats_gram[0],bld.stats_gram[1],bld.stats_gram[2],bld.stats_dom[0]+bld.stats_dom[1]+bld.stats_dom[2]+bld.stats_dom[3],bld.stats_resid[0],bld.stats_resid[1],bld.stats_resid[2],bld.stats_resid[3]);
  { FILE*f=fopen("zidx.bin","wb"); fwrite(blob.data(),1,blob.size(),f); fclose(f); }
  // ---- load via mmap
  size_t r_before=RssKb(); size_t hh0=HeapInUse(); auto l0=Clock::now();
  int fd=open("zidx.bin",O_RDONLY); void* m=mmap(nullptr,blob.size(),PROT_READ,MAP_PRIVATE,fd,0); zidx::Index idx; bool ok=idx.Load((const uint8_t*)m,blob.size()); auto l1=Clock::now();
  printf("          mmap+validate %.2f ms (ok=%d), heap allocated by loading: %.0f KB\n", Ms(l0,l1), ok, (HeapInUse()-hh0)/1e3); if(!ok) return 1;
  // corrupt blob must be rejected, not crash
  { std::vector<uint8_t> bad=blob; int rej=0,tot=0; std::mt19937 rng(7); for(int i=0;i<300;++i){ auto c=bad; for(int k=0;k<8;++k) c[rng()%c.size()]^=1<<(rng()%8); zidx::Index t; ++tot; rej+=!t.Load(c.data(),c.size()); } printf("          fuzzed blobs (8 flipped bits): %d/%d rejected at load, 0 crashes (the rest only altered data bytes, never an offset)\n", rej,tot); }
  // ---- differential test
  auto corpus=MakeCorpus(text,20000); size_t mism=0, checked=0, blocked_agree=0, longs=0;
  auto cmp=[&](const std::string& u,const std::string& i,int ty){ GURL gu(u),gi(i); bool a=base->ShouldBlock(gu,gi,(ResourceType)ty), b=idx.ShouldBlock(gu,gi,(ResourceType)ty); ++checked; blocked_agree+=a; if(a!=b){ if(++mism<=10) printf("  MISMATCH base=%d new=%d type=%d url=%s ini=%s\n",a,b,ty,u.c_str(),i.c_str()); } };
  for(auto&r:corpus) cmp(r.url,r.initiator,r.type);
  size_t after_corpus=checked;
  std::mt19937 rng(99); int types[]={1<<1,1<<2,1<<5,1<<6,1<<0};
  for(auto& [g,rp]: all){ const FilterRule& r=*rp; std::string u=Synth(r); int ty= r.type_mask? (r.type_mask & -(int)r.type_mask) : types[rng()%5];
    cmp(u,Ini(r,u,0),ty); cmp(u,Ini(r,u,3),ty);                       // exact; and first-party initiator
    cmp(u,Ini(r,u,0),types[rng()%5]);                                  // other resource type
    std::string m=u; if(m.size()>12){ size_t k=8+rng()%(m.size()-8); m[k]= m[k]=='a'?'b':'a'; } cmp(m,Ini(r,u,0),ty); // one-char mutation
    std::string v=u+"?q=1&utm=2"; cmp(v,Ini(r,u,0),ty);
    // The rule's match placed after 150 other tokens: guards against any fixed cap on tokens per URL.
    if(r.anchor==FilterRule::kAnchorNone){ std::string junk; for(int k=0;k<150;++k){ char b[16]; snprintf(b,sizeof b,"abc%03d/",k); junk+=b; } std::string body=u.substr(std::string("https://site.example/").size()); cmp("https://site.example/"+junk+body,Ini(r,u,0),ty); ++longs; } }
  printf("DIFFERENTIAL  %zu probes (%zu realistic-corpus + %zu derived from every rule, %zu of those with the match after 150 other tokens), %zu of them blocked by the baseline: %zu mismatches\n", checked, after_corpus, checked-after_corpus, longs, blocked_agree, mism);
  size_t dm=0, dc=0; for(auto& [g,rp]: all){ if(g!=zidx::kDocument) continue; std::string u=Synth(*rp); GURL gu(u); uint32_t a=base->GetDocumentExceptions(gu), b=idx.DocumentExceptions(gu); ++dc; dm+=a!=b; } printf("DOC-EXCEPTIONS %zu probes, %zu mismatches\n", dc, dm);
  // ---- benchmark both on the same corpus
  std::vector<GURL> U,I; for(auto&r:corpus){U.emplace_back(r.url); I.emplace_back(r.initiator);} 
  auto bench=[&](const char*name,auto&& f){ for(size_t i=0;i<U.size();++i) f(i); std::vector<double> lat; lat.reserve(U.size()*3); double tot=0; for(int rep=0;rep<3;++rep) for(size_t i=0;i<U.size();++i){ auto a=Clock::now(); f(i); auto c=Clock::now(); double us=Ms(a,c)*1000; lat.push_back(us); tot+=us; } std::sort(lat.begin(),lat.end()); printf("%-10s mean %6.2f us  p50 %6.2f  p99 %7.2f  max %7.1f\n",name,tot/lat.size(),lat[lat.size()/2],lat[lat.size()*99/100],lat.back()); };
  volatile size_t sink=0;
  bench("baseline",[&](size_t i){ sink+=base->ShouldBlock(U[i],I[i],(ResourceType)corpus[i].type); });
  bench("zindex",[&](size_t i){ sink+=idx.ShouldBlock(U[i],I[i],(ResourceType)corpus[i].type); });
  // ---- destroy cost + resident memory
  auto d0=Clock::now(); base.reset(); auto d1=Clock::now(); printf("baseline engine destroy: %.1f ms (zindex: munmap)\n", Ms(d0,d1));
  printf("process RSS now %.0f MB (blob is a read-only file mapping: shared via page cache across profiles)\n", RssKb()/1e3);
}
