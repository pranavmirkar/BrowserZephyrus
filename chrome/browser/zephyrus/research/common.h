#pragma once
#include <malloc.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>
#include <unistd.h>
using Clock = std::chrono::steady_clock;
inline double Ms(Clock::time_point a, Clock::time_point b){ return std::chrono::duration<double, std::milli>(b-a).count(); }
inline size_t HeapInUse(){ struct mallinfo2 m = mallinfo2(); return m.uordblks + m.hblkhd; }
inline size_t RssKb(){ std::ifstream f("/proc/self/statm"); size_t a,b; f>>a>>b; return b*4; }
inline std::string ReadFile(const std::string& p){ std::ifstream f(p, std::ios::binary); std::stringstream ss; ss<<f.rdbuf(); return ss.str(); }
inline std::string Combined(const std::vector<std::string>& files){ std::string all; for(auto&f:files){ all += "\n! ===== " + f + " =====\n"; all += ReadFile(f); all += "\n"; } return all; }
struct Req { std::string url, initiator; int type; bool expect_ad; };
// Deterministic corpus: ~90% ordinary subresources, ~10% shaped like things the lists block.
inline std::vector<Req> MakeCorpus(const std::string& all_lists_text, size_t n, unsigned seed=42){
  std::mt19937 rng(seed);
  const char* sites[] = {"www.youtube.com","www.amazon.in","www.flipkart.com","timesofindia.indiatimes.com","www.hotstar.com","en.wikipedia.org","www.reddit.com","github.com","www.nytimes.com","www.bbc.com","www.ndtv.com","www.zomato.com","www.swiggy.com","www.irctc.co.in","web.whatsapp.com","www.linkedin.com","www.cricbuzz.com","stackoverflow.com","www.hindustantimes.com","www.moneycontrol.com","medium.com","news.ycombinator.com","www.twitch.tv","www.spotify.com","docs.google.com","mail.google.com","www.paytm.com","www.myntra.com","www.makemytrip.com","www.espncricinfo.com"};
  const char* thirds[] = {"cdnjs.cloudflare.com","fonts.gstatic.com","fonts.googleapis.com","cdn.jsdelivr.net","unpkg.com","i.ytimg.com","static.cloudflareinsights.com","challenges.cloudflare.com","js.stripe.com","maps.googleapis.com"};
  const char* kinds[] = {"/static/js/main.%08x.chunk.js","/static/css/app.%08x.css","/images/hero_%d.jpg","/api/v2/items/%d/related?limit=20&cursor=%08x","/assets/fonts/inter-%d.woff2","/_next/static/chunks/%08x.js","/graphql","/api/search?q=phone&page=%d","/media/thumb/%d_320x180.webp","/v1/session/refresh"};
  std::vector<std::string> blocked_hosts;
  { std::istringstream in(all_lists_text); std::string l; while(std::getline(in,l)){ if(l.rfind("||",0)==0 && l.find('$')==std::string::npos){ size_t c=l.find('^'); if(c!=std::string::npos && l.find('/')==std::string::npos && l.find('*')==std::string::npos) blocked_hosts.push_back(l.substr(2,c-2)); } } }
  std::vector<Req> out; out.reserve(n);
  for(size_t i=0;i<n;++i){
    char buf[256]; Req r; int roll=rng()%100; std::string site = sites[rng()%30];
    r.initiator = "https://"+site+"/"; r.expect_ad=false; r.type=1<<(rng()%3==0?5:(rng()%2?1:2));
    if(roll<10 && !blocked_hosts.empty()){ r.url="https://"+blocked_hosts[rng()%blocked_hosts.size()]+"/ads/tag.js?id="+std::to_string(rng()%100000); r.expect_ad=true; r.type=1<<1; }
    else if(roll<18){ r.url=std::string("https://")+thirds[rng()%10]+"/lib/"+std::to_string(rng()%999)+"/bundle.min.js"; }
    else { snprintf(buf,sizeof buf,kinds[rng()%10],(int)(rng()%100000),(unsigned)rng()); std::string h = (rng()%2? "cdn." : "") + site.substr(site.find('.')+ (site.rfind('.')==site.find('.')?0:1)); r.url="https://"+ (rng()%3? site : h) + buf; }
    out.push_back(r);
  }
  return out;
}
