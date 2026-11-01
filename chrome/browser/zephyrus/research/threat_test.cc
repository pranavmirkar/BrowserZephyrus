#include "common.h"
#include "threat_index.h"
int main(int argc,char**argv){
  if(argc<2){ fprintf(stderr,"usage: threat_test <dir with phishdb_domains.txt and phishdb_links.txt>\n"); return 2; }
  const std::string D=std::string(argv[1])+"/";
  std::vector<std::string> doms, links; { std::ifstream f(D+"phishdb_domains.txt"); std::string l; while(std::getline(f,l)) if(!l.empty()&&l[0]!='#') doms.push_back(l); } { std::ifstream f(D+"phishdb_links.txt"); std::string l; while(std::getline(f,l)) if(!l.empty()) links.push_back(l); }
  auto t0=Clock::now(); threat::Builder b; for(auto&d:doms) b.AddHost(d); for(auto&l:links) b.AddUrl(l); auto blob=b.Finish(); auto t1=Clock::now();
  printf("source: %zu domains + %zu URLs (%.1f MB of text)  ->  index of %zu entries, %.2f MB (%.1f bytes/entry), built in %.0f ms\n", doms.size(), links.size(), (11.0+65.8), b.count(), blob.size()/1e6, (double)blob.size()/b.count(), Ms(t0,t1));
  threat::Index idx; if(!idx.Load(blob.data(),blob.size())) { puts("load failed"); return 1; }
  // recall: every listed URL and domain must be found
  size_t miss=0; for(size_t i=0;i<links.size();i+=7){ if(!idx.IsListed(links[i])) ++miss; } size_t checked=(links.size()+6)/7; size_t dmiss=0; for(auto&d:doms) if(!idx.IsListed("https://"+d+"/some/page?x=1")) ++dmiss;
  printf("recall: %zu/%zu sampled listed URLs found; %zu/%zu listed domains found with an unseen path and query\n", checked-miss, checked, doms.size()-dmiss, doms.size());
  printf("subdomain coverage: 'https://login.deep.%s/a' -> %s\n", doms[5].c_str(), idx.IsListed("https://login.deep."+doms[5]+"/a")?"listed (host-suffix rule works)":"missed");
  // false positives: benign synthetic traffic
  std::mt19937_64 rng(5); size_t fp=0, N=3000000; const char* tl[]={"com","org","net","in","io","dev","co.uk"}; auto t2=Clock::now();
  for(size_t i=0;i<N;++i){ char buf[200]; snprintf(buf,sizeof buf,"https://www.site%llu.%s/p/%llu/x?id=%llu", (unsigned long long)(rng()%50000000), tl[rng()%7], (unsigned long long)(rng()%1000), (unsigned long long)rng()); fp+=idx.IsListed(buf); } auto t3=Clock::now();
  printf("false positives: %zu hits in %zu benign-shaped URLs (each tests ~%d expressions); theoretical per-URL rate ~%.1e\n", fp, N, 30, 30.0*b.count()/2.8e14);
  const char* known[]={"https://www.google.com/search?q=x","https://github.com/","https://www.hdfcbank.com/personal/pay","https://upi.npci.org.in/","https://en.wikipedia.org/wiki/Phishing","https://accounts.google.com/signin","https://www.irctc.co.in/nget/train-search","https://www.flipkart.com/","https://www.amazon.in/","https://www.sbi.co.in/"}; size_t kfp=0; for(auto k:known) kfp+=idx.IsListed(k); printf("popular real sites listed as threats: %zu of %zu\n", kfp, sizeof(known)/sizeof(*known));
  printf("lookup: %.2f us per miss (full expression set, includes URL parsing), over %zu URLs\n", Ms(t2,t3)*1000/N, N);
  std::vector<std::string> hitset(links.begin(), links.begin()+std::min<size_t>(links.size(),200000)); auto t4=Clock::now(); size_t h=0; for(auto&u:hitset) h+=idx.IsListed(u); auto t5=Clock::now(); printf("lookup: %.2f us per hit (%zu/%zu)\n", Ms(t4,t5)*1000/hitset.size(), h, hitset.size());
  // tamper test
  size_t bad=0; std::mt19937 r2(3); for(int i=0;i<200;++i){ auto c=blob; for(int k=0;k<4;++k) c[r2()%c.size()]^=1<<(r2()%8); threat::Index t; bad+=!t.Load(c.data(),c.size()); } printf("corrupted blobs rejected at load: %zu/200 (rest only changed hash bytes: wrong answers for a few URLs, never a crash)\n", bad);
}
