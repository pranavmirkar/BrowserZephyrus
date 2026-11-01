#define private public
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#undef private
#include "common.h"
#include "base/strings/string_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
using namespace zephyrus_adblock;
static std::vector<std::string_view> Tokens(std::string_view u){ std::vector<std::string_view> t; size_t s=0; for(size_t i=0;i<=u.size();++i){ if(i<u.size()&&base::IsAsciiAlphaNumeric(u[i])) continue; if(i-s>=3) t.push_back(u.substr(s,i-s)); s=i+1;} return t; }
int main(int argc,char**argv){
  std::vector<std::string> files(argv+1,argv+argc); std::string text=Combined(files);
  auto eng=std::make_unique<AdblockFilterEngine>(); eng->AddRules(text);
  auto corpus=MakeCorpus(text,20000);
  std::vector<GURL> urls, inis; for(auto&r:corpus){ urls.emplace_back(r.url); inis.emplace_back(r.initiator); }
  auto run=[&](const char* name, auto fn){ for(int w=0;w<2;++w){ for(size_t i=0;i<urls.size();++i) fn(i);} auto a=Clock::now(); for(int rep=0;rep<5;++rep) for(size_t i=0;i<urls.size();++i) fn(i); auto b=Clock::now(); printf("%-52s %6.2f us/request\n", name, Ms(a,b)*1000/(5.0*urls.size())); };
  volatile size_t sink=0;
  run("full ShouldBlock", [&](size_t i){ sink+=eng->ShouldBlock(urls[i],inis[i],(ResourceType)corpus[i].type); });
  run("lowercase(spec) alloc only", [&](size_t i){ std::string l=base::ToLowerASCII(urls[i].spec()); sink+=l.size(); });
  run("lowercase + tokenise", [&](size_t i){ std::string l=base::ToLowerASCII(urls[i].spec()); sink+=Tokens(l).size(); });
  run("lowercase + tokenise + bucket lookups (block only)", [&](size_t i){ std::string l=base::ToLowerASCII(urls[i].spec()); for(auto t:Tokens(l)){ auto it=eng->block_buckets_.find(t); if(it!=eng->block_buckets_.end()) sink+=it->second.size(); } });
  // untokenized cost: clear them and re-time full
  auto saved_b=eng->untokenized_block_, saved_e=eng->untokenized_exception_, saved_i=eng->untokenized_important_;
  eng->untokenized_block_.clear(); eng->untokenized_exception_.clear(); eng->untokenized_important_.clear();
  run("full ShouldBlock WITHOUT untokenized rules", [&](size_t i){ sink+=eng->ShouldBlock(urls[i],inis[i],(ResourceType)corpus[i].type); });
  eng->untokenized_block_=saved_b; eng->untokenized_exception_=saved_e; eng->untokenized_important_=saved_i;
  // how many candidate rules does a typical clean request evaluate?
  size_t cand=0, reqs=0; for(size_t i=0;i<urls.size();++i){ if(corpus[i].expect_ad) continue; std::string l=base::ToLowerASCII(urls[i].spec()); for(auto t:Tokens(l)){ auto it=eng->block_buckets_.find(t); if(it!=eng->block_buckets_.end()) cand+=it->second.size(); } cand+=eng->untokenized_block_.size(); ++reqs; }
  printf("avg candidate block rules examined per clean request: %.0f (of which untokenized %zu)\n", (double)cand/reqs, eng->untokenized_block_.size());
  // third-party classification cost
  run("SameDomainOrHost only (shim PSL, not Chromium's)", [&](size_t i){ sink+=net::registry_controlled_domains::SameDomainOrHost(urls[i],inis[i],net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES); });
}
