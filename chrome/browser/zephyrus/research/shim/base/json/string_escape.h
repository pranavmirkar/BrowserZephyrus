#pragma once
#include <string>
#include <string_view>
namespace base {
inline bool EscapeJSONString(std::string_view in, bool quotes, std::string* out){
  if(quotes) out->push_back('"');
  for(unsigned char c: in){
    switch(c){ case '"': *out+="\\\""; break; case '\\': *out+="\\\\"; break; case '\n': *out+="\\n"; break; case '\r': *out+="\\r"; break; case '\t': *out+="\\t"; break;
      default: if(c<0x20){ char b[8]; snprintf(b,sizeof b,"\\u%04X",c); *out+=b; } else out->push_back((char)c); }
  }
  if(quotes) out->push_back('"'); return true; }
}
