// ThreatIndex prototype: an offline, per-URL-leak-free list of known-bad hosts and URLs.
//   * entries are 48-bit keyed hashes (SipHash-2-4 with a random per-build key) stored sorted, with a 64K-bucket directory
//   * the key is chosen when the blob is built, so nobody can pre-compute a URL that collides with a legitimate site
//   * lookups generate the Safe Browsing style expression set (host suffixes x path prefixes) and test each
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
namespace threat {
inline uint64_t Rotl(uint64_t x,int b){ return (x<<b)|(x>>(64-b)); }
inline uint64_t SipHash24(const uint8_t k[16], const uint8_t* in, size_t n){
  uint64_t k0,k1; memcpy(&k0,k,8); memcpy(&k1,k+8,8);
  uint64_t v0=0x736f6d6570736575ULL^k0,v1=0x646f72616e646f6dULL^k1,v2=0x6c7967656e657261ULL^k0,v3=0x7465646279746573ULL^k1;
  auto round=[&]{ v0+=v1; v1=Rotl(v1,13); v1^=v0; v0=Rotl(v0,32); v2+=v3; v3=Rotl(v3,16); v3^=v2; v0+=v3; v3=Rotl(v3,21); v3^=v0; v2+=v1; v1=Rotl(v1,17); v1^=v2; v2=Rotl(v2,32); };
  const uint8_t* end=in+(n&~7ULL); for(;in!=end;in+=8){ uint64_t m; memcpy(&m,in,8); v3^=m; round(); round(); v0^=m; }
  uint64_t b=(uint64_t)n<<56; switch(n&7){ case 7:b|=(uint64_t)in[6]<<48; [[fallthrough]]; case 6:b|=(uint64_t)in[5]<<40; [[fallthrough]]; case 5:b|=(uint64_t)in[4]<<32; [[fallthrough]]; case 4:b|=(uint64_t)in[3]<<24; [[fallthrough]]; case 3:b|=(uint64_t)in[2]<<16; [[fallthrough]]; case 2:b|=(uint64_t)in[1]<<8; [[fallthrough]]; case 1:b|=in[0]; }
  v3^=b; round(); round(); v0^=b; v2^=0xff; round(); round(); round(); round(); return v0^v1^v2^v3; }

// "http://User@Host.com:80/a/b?x=1#frag" -> host "host.com", path "/a/b", query "x=1"
struct Url { std::string host, path, query; bool ok=false; };
inline Url Parse(std::string_view u){
  Url r; size_t p=u.find("://"); if(p==std::string_view::npos) return r; u.remove_prefix(p+3);
  size_t h=u.find('#'); if(h!=std::string_view::npos) u=u.substr(0,h);
  size_t e=u.find_first_of("/?"); std::string_view auth=u.substr(0,e), rest=e==std::string_view::npos? std::string_view():u.substr(e);
  size_t at=auth.rfind('@'); if(at!=std::string_view::npos) auth.remove_prefix(at+1);
  size_t c=auth.rfind(':'); if(c!=std::string_view::npos && auth.find(']')==std::string_view::npos) auth=auth.substr(0,c);
  r.host.assign(auth); for(char&ch:r.host) if(ch>='A'&&ch<='Z') ch+=32; while(!r.host.empty()&&r.host.back()=='.') r.host.pop_back();
  if(r.host.empty()) return r;
  size_t q=rest.find('?'); r.path.assign(q==std::string_view::npos? rest : rest.substr(0,q)); if(r.path.empty()) r.path="/"; if(q!=std::string_view::npos) r.query.assign(rest.substr(q+1));
  r.ok=true; return r; }
// Safe Browsing style expressions: up to 5 host forms x up to 6 path forms
template<class F> inline void Expressions(const Url& u, F&& f){
  std::vector<std::string> hosts{u.host}; { std::string_view h=u.host; int n=0; while(n<4){ size_t d=h.find('.'); if(d==std::string_view::npos) break; h.remove_prefix(d+1); if(h.find('.')==std::string_view::npos) break; hosts.emplace_back(h); ++n; } }
  std::vector<std::string> paths; if(!u.query.empty()) paths.push_back(u.path+"?"+u.query); paths.push_back(u.path);
  { size_t pos=0; int n=0; while(n<4){ size_t s=u.path.find('/',pos); if(s==std::string::npos) break; std::string pre=u.path.substr(0,s+1); if(pre!=u.path) paths.push_back(pre); pos=s+1; ++n; } }
  if(paths.empty()||paths.back()!="/") paths.push_back("/");
  std::string e; for(auto&h:hosts) for(auto&p:paths){ e=h; e+=p; if(f(e)) return; } }

class Builder { public:
  Builder(){ FILE*f=fopen("/dev/urandom","rb"); (void)!fread(key_,1,16,f); fclose(f); }
  void AddHost(const std::string& h){ std::string e=h+"/"; keys_.push_back(Key(e)); }
  void AddUrl(const std::string& url){ Url u=Parse(url); if(!u.ok) return; std::string e=u.host+u.path+(u.query.empty()?"":"?"+u.query); keys_.push_back(Key(e)); }
  std::vector<uint8_t> Finish(){ std::sort(keys_.begin(),keys_.end()); keys_.erase(std::unique(keys_.begin(),keys_.end()),keys_.end());
    std::vector<uint8_t> out(16+4+65537*4+keys_.size()*4); memcpy(out.data(),key_,16); uint32_t n=(uint32_t)keys_.size(); memcpy(out.data()+16,&n,4);
    uint32_t* dir=(uint32_t*)(out.data()+20); size_t idx=0; for(uint32_t b=0;b<=65536;++b){ while(idx<keys_.size() && (keys_[idx]>>32)<b) ++idx; dir[b]=(uint32_t)idx; }
    uint8_t* tail=out.data()+20+65537*4; for(size_t i=0;i<keys_.size();++i){ uint32_t lo=(uint32_t)(keys_[i]&0xffffffffu); memcpy(tail+i*4,&lo,4); }   // top 16 bits live in the directory, low 32 here
    return out; }   // 48-bit key = 16 directory bits + 32 stored bits => 4 bytes/entry + 256 KB directory
  size_t count() const { return keys_.size(); }
 private: uint64_t Key(const std::string& e) const { return SipHash24(key_,(const uint8_t*)e.data(),e.size()) & 0xffffffffffffULL; } uint8_t key_[16]; std::vector<uint64_t> keys_; };

class Index { public:
  bool Load(const uint8_t* d,size_t n){ if(n<20+65537*4) return false; memcpy(key_,d,16); memcpy(&n_,d+16,4); dir_=(const uint32_t*)(d+20); lo_=(const uint32_t*)(d+20+65537*4);
    if(n<20+65537*4+(size_t)n_*4 || dir_[65536]!=n_) return false; for(uint32_t b=0;b<65536;++b) if(dir_[b]>dir_[b+1]) return false; return true; }
  bool Contains(std::string_view expr) const { uint64_t k=SipHash24(key_,(const uint8_t*)expr.data(),expr.size())&0xffffffffffffULL; uint32_t b=(uint32_t)(k>>32), lo=(uint32_t)k;
    const uint32_t* s=lo_+dir_[b]; const uint32_t* e=lo_+dir_[b+1]; return std::binary_search(s,e,lo); }
  // true if the URL, or any host-suffix / path-prefix form of it, is listed
  bool IsListed(std::string_view url) const { Url u=Parse(url); if(!u.ok) return false; bool hit=false; Expressions(u,[&](const std::string& e){ hit=Contains(e); return hit; }); return hit; }
  uint32_t size() const { return n_; }
 private: uint8_t key_[16]; uint32_t n_=0; const uint32_t* dir_=nullptr; const uint32_t* lo_=nullptr; };
}
