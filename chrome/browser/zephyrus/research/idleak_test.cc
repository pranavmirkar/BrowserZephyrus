#include "common.h"
#include "idleak.h"
#include <openssl/evp.h>
static std::string Hex(const std::vector<uint8_t>& d, bool up=false){ static const char*L="0123456789abcdef",*U="0123456789ABCDEF"; std::string s; for(uint8_t b:d){ s+=(up?U:L)[b>>4]; s+=(up?U:L)[b&15]; } return s; }
static std::string B64(const std::vector<uint8_t>& d, bool url, bool pad){ static const char*T="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string s; size_t i=0; for(;i+2<d.size();i+=3){ uint32_t v=d[i]<<16|d[i+1]<<8|d[i+2]; for(int k=3;k>=0;--k) s+=T[(v>>(6*k))&63]; } if(i<d.size()){ uint32_t v=d[i]<<16|(i+1<d.size()?d[i+1]<<8:0); s+=T[(v>>18)&63]; s+=T[(v>>12)&63]; if(i+1<d.size()) s+=T[(v>>6)&63]; if(pad) s.append(i+1<d.size()?"=":"=="); } if(url) for(char&c:s){ if(c=='+')c='-'; if(c=='/')c='_'; } return s; }
int main(){
  idleak::Scanner sc; sc.AddEmail("Jane.Doe+news@Gmail.com", 7); sc.AddEmail("ravi.kumar@example.in", 9);
  int pass=0, fail=0; auto expect=[&](const char* what, std::string data, int want_ident, const char* want_kind){ int got=-1; std::string kind; sc.Scan(data,[&](idleak::Hit h){ got=h.ident; kind=h.encoding; }); bool ok = want_ident<0? got<0 : (got==want_ident && (!want_kind||kind==want_kind)); (ok?pass:fail)++; printf("  %-4s %-62s %s\n", ok?"ok":"FAIL", what, got<0?"(no hit)":("ident "+std::to_string(got)+" via "+kind).c_str()); };
  std::string g1="janedoe@gmail.com", g2="ravi.kumar@example.in";
  auto s256=idleak::Digest("SHA256",g1), s1=idleak::Digest("SHA1",g1), m5=idleak::Digest("MD5",g1), r256=idleak::Digest("SHA256",g2);
  puts("positive cases (identifier present in some encoding):");
  expect("sha256 hex in query string (UID2 / LiveRamp style)", "https://t.example/px?v=2&em="+Hex(s256)+"&ts=1", 7, "hex");
  expect("sha256 hex UPPERCASE", "https://t.example/px?em="+Hex(s256,true), 7, "hex");
  expect("sha256 base64 (padded)", "https://t.example/c?hem="+B64(s256,false,true), 7, "base64");
  expect("sha256 base64url (no padding)", "https://t.example/c?hem="+B64(s256,true,false), 7, "base64");
  expect("md5 hex", "https://t.example/m?e="+Hex(m5), 7, "hex");
  expect("sha1 hex", "https://t.example/m?e="+Hex(s1), 7, "hex");
  expect("sha256 hex inside a JSON POST body", "{\"events\":[{\"type\":\"page\",\"user\":{\"id_sha256\":\""+Hex(r256)+"\"}}]}", 9, "hex");
  expect("sha256 of Gmail-normalised form (dots/+tag stripped)", "https://t.example/px?em="+Hex(idleak::Digest("SHA256","janedoe@gmail.com")), 7, "hex");
  expect("plain address", "https://t.example/px?email=ravi.kumar@example.in&x=1", 9, "plain");
  expect("percent-encoded address", "https://t.example/px?email=ravi.kumar%40example.in&x=1", 9, "percent-encoded");
  expect("digest glued to a longer token (suffix window)", "https://t.example/px?sid=abc"+Hex(r256)+"&z=1", 9, "hex");
  puts("negative controls:");
  { std::string h=Hex(s256); h[10]=h[10]=='a'?'b':'a'; expect("sha256 with ONE hex digit changed", "https://t.example/px?em="+h, -1, nullptr); }
  expect("random 64-hex (session id)", "https://t.example/px?sid=3f7a9c2e1b4d6580a1f3c5e7092b4d6f8a0c2e4b6d8f1a3c5e7092b4d6f8a0c1", -1, nullptr);
  expect("unrelated address", "https://t.example/px?email=someone.else@example.org", -1, nullptr);
  expect("sha1 hex presented as 'sha256' length? (40 hex, wrong user)", "https://t.example/px?e="+Hex(idleak::Digest("SHA1","nobody@x.com")), -1, nullptr);
  printf("%d passed, %d failed\n", pass, fail);
  // throughput + false positives on random data
  std::mt19937_64 rng(1); std::string big; big.reserve(1<<20); const char* hexd="0123456789abcdef"; while(big.size()<(1<<20)){ int k=rng()%5; if(k==0){ for(int i=0;i<64;++i) big+=hexd[rng()%16]; } else if(k==1){ big+="&utm_source=newsletter&session="; for(int i=0;i<27;++i) big+="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"[rng()%62]; } else big+="/path/to/resource?id="+std::to_string(rng()%1000000)+"&q=hello+world"; }
  size_t hits=0; auto t0=Clock::now(); size_t total=0; for(size_t off=0; off+4096<=big.size(); off+=4096){ sc.Scan(std::string_view(big).substr(off,4096),[&](idleak::Hit){ ++hits; }); total+=4096; } auto t1=Clock::now();
  printf("throughput: %.0f MB/s over %.1f MB of hex-heavy random request-like data (%.2f us per 4 KB request), false positives: %zu\n", total/1e6/(Ms(t0,t1)/1000), total/1e6, Ms(t0,t1)*1000/(total/4096), hits);
  std::string typical="https://www.google-analytics.com/g/collect?v=2&tid=G-ABC123XYZ&gtm=45je5a30v9123456789za200&_p=1698765432101&cid=1234567890.1698765432&ul=en-us&sr=1920x1080&_s=1&dl=https%3A%2F%2Fwww.example.com%2Fproducts%2Fshoes%3Fcolor%3Dred&dt=Red%20Shoes&en=page_view"; auto t2=Clock::now(); for(int i=0;i<200000;++i) sc.Scan(typical,[&](idleak::Hit){ ++hits; }); auto t3=Clock::now(); printf("a typical analytics URL (%zu bytes): %.2f us per scan\n", typical.size(), Ms(t2,t3)*1000/200000);
}
