#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "common.h"
using namespace zephyrus_adblock;
int main(int argc,char**argv){ std::vector<std::string> files(argv+1,argv+argc); auto text=Combined(files); AdblockFilterEngine e; e.AddRules(text);
  auto c=MakeCorpus(text,20000); for(auto&r:c) printf("%d\n", (int)e.ShouldBlock(GURL(r.url),GURL(r.initiator),(ResourceType)r.type)); }
