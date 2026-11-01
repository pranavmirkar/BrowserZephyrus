// Emits the same rule-derived probes ztest uses, plus the shipped engine's decision, for cross-engine comparison.
#define private public
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#undef private
#include "common.h"
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
static const char* TypeName(int t){ switch(t){ case 1<<0:return "other"; case 1<<1:return "script"; case 1<<2:return "image"; case 1<<3:return "stylesheet"; case 1<<4:return "object"; case 1<<5:return "xhr"; case 1<<6:return "subdocument"; case 1<<7:return "font"; case 1<<8:return "media"; case 1<<9:return "ping"; case 1<<10:return "websocket"; case 1<<12:return "document"; default:return nullptr; /* popup etc: no equivalent in the other engine */ } }
int main(int argc,char**argv){ std::vector<std::string> files(argv+1,argv+argc); auto text=Combined(files); AdblockFilterEngine e; e.AddRules(text);
  std::vector<const FilterRule*> all; auto feed=[&](const RuleBuckets& m,const std::vector<FilterRule>& un){ for(auto&kv:m) for(auto&r:kv.second) all.push_back(&r); for(auto&r:un) all.push_back(&r); };
  feed(e.block_buckets_,e.untokenized_block_); feed(e.exception_buckets_,e.untokenized_exception_); feed(e.important_buckets_,e.untokenized_important_); feed(e.document_buckets_,e.untokenized_document_);
  std::mt19937 rng(99); int types[]={1<<1,1<<2,1<<5,1<<6,1<<0};
  auto emit=[&](const std::string&u,const std::string&i,int ty){ if(!TypeName(ty)) return; printf("%s\t%s\t%s\t%d\n",u.c_str(),i.c_str(),TypeName(ty),(int)e.ShouldBlock(GURL(u),GURL(i),(ResourceType)ty)); };
  for(auto* rp: all){ const FilterRule& r=*rp; std::string u=Synth(r); int ty= r.type_mask? (r.type_mask & -(int)r.type_mask) : types[rng()%5]; emit(u,Ini(r,u,0),ty); emit(u,Ini(r,u,3),ty); } }
