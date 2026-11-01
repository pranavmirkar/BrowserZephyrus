#include "common.h"
int main(int argc,char**argv){ std::vector<std::string> files(argv+1,argv+argc); auto text=Combined(files); auto c=MakeCorpus(text,20000);
  for(auto&r:c){ const char* t = r.type==(1<<1)?"script": r.type==(1<<2)?"image":"xhr"; printf("%s\t%s\t%s\t%d\n", r.url.c_str(), r.initiator.c_str(), t, (int)r.expect_ad); } }
