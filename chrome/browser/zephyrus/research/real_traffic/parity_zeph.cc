// For every rule line, would the shipped Zephyrus parser keep it as a NETWORK rule? (one engine per line: exact, order-free)
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "common.h"
using namespace zephyrus_adblock;
int main(int argc,char**argv){ std::ifstream f(argv[1]); std::string l; while(std::getline(f,l)){ size_t t=l.find('\t'); std::string rule=l.substr(t+1); AdblockFilterEngine e; size_t n=e.AddRules(rule); printf("%d\n",(int)(n>0)); } }
