// Stand-ins for the base:: string helpers the ad-block engine uses. Behaviour matches Chromium's for these call shapes.
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
namespace base {
enum TrimPositions { TRIM_NONE = 0, TRIM_LEADING = 1, TRIM_TRAILING = 2, TRIM_ALL = 3 };
enum WhitespaceHandling { KEEP_WHITESPACE, TRIM_WHITESPACE };
enum SplitResult { SPLIT_WANT_ALL, SPLIT_WANT_NONEMPTY };
inline bool IsAsciiAlphaNumeric(char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'); }
inline bool IsAsciiAlpha(char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
inline bool IsAsciiDigit(char c){ return c>='0'&&c<='9'; }
inline bool IsAsciiWhitespace(char c){ return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }
inline std::string ToLowerASCII(std::string_view s){ std::string r(s); for(char&c:r) if(c>='A'&&c<='Z') c+=32; return r; }
inline bool StartsWith(std::string_view s, std::string_view p){ return s.size()>=p.size() && s.compare(0,p.size(),p)==0; }
inline bool EndsWith(std::string_view s, std::string_view p){ return s.size()>=p.size() && s.compare(s.size()-p.size(),p.size(),p)==0; }
inline std::string_view TrimWhitespaceASCII(std::string_view s, TrimPositions pos){
  size_t b=0,e=s.size();
  if(pos&TRIM_LEADING) while(b<e&&IsAsciiWhitespace(s[b])) ++b;
  if(pos&TRIM_TRAILING) while(e>b&&IsAsciiWhitespace(s[e-1])) --e;
  return s.substr(b,e-b);
}
inline std::vector<std::string_view> SplitStringPiece(std::string_view in, std::string_view seps, WhitespaceHandling ws, SplitResult res){
  std::vector<std::string_view> out; size_t start=0;
  auto emit=[&](size_t b,size_t e){ std::string_view p=in.substr(b,e-b); if(ws==TRIM_WHITESPACE) p=TrimWhitespaceASCII(p,TRIM_ALL); if(res==SPLIT_WANT_ALL||!p.empty()) out.push_back(p); };
  for(size_t i=0;i<=in.size();++i){ if(i==in.size()|| seps.find(in[i])!=std::string_view::npos){ emit(start,i); start=i+1; } }
  return out;
}
inline std::string JoinString(const std::vector<std::string_view>& parts, std::string_view sep){ std::string r; for(size_t i=0;i<parts.size();++i){ if(i) r+=sep; r+=parts[i]; } return r; }
inline std::string JoinString(const std::vector<std::string>& parts, std::string_view sep){ std::string r; for(size_t i=0;i<parts.size();++i){ if(i) r+=sep; r+=parts[i]; } return r; }
inline std::string StrCat(std::initializer_list<std::string_view> parts){ std::string r; for(auto p:parts) r+=p; return r; }
inline std::string NumberToString(long long v){ return std::to_string(v); }
inline bool StringToInt(std::string_view s, int* out){ long long v=0; auto r=std::from_chars(s.data(),s.data()+s.size(),v); if(r.ec!=std::errc()||r.ptr!=s.data()+s.size()) return false; *out=(int)v; return true; }
inline bool StringToInt64(std::string_view s, int64_t* out){ auto r=std::from_chars(s.data(),s.data()+s.size(),*out); return r.ec==std::errc()&&r.ptr==s.data()+s.size(); }
}
