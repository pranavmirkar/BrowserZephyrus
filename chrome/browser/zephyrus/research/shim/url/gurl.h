// Minimal GURL: lowercases scheme/host, ensures a path, keeps query/fragment. Enough for the engine's call shapes.
#pragma once
#include <string>
#include <string_view>
class GURL {
 public:
  GURL() = default;
  explicit GURL(std::string_view in) { Parse(in); }
  bool is_valid() const { return valid_; }
  bool has_host() const { return valid_ && !host_.empty(); }
  bool SchemeIsHTTPOrHTTPS() const { return valid_ && (scheme_=="http"||scheme_=="https"); }
  const std::string& spec() const { return spec_; }
  const std::string& host() const { return host_; }
 private:
  void Parse(std::string_view in) {
    size_t p = in.find("://"); if (p==std::string_view::npos) return;
    scheme_.assign(in.substr(0,p)); for(char&c:scheme_) if(c>='A'&&c<='Z') c+=32;
    std::string_view rest = in.substr(p+3);
    size_t e = rest.find_first_of("/?#"); std::string_view auth = e==std::string_view::npos? rest : rest.substr(0,e);
    std::string_view tail = e==std::string_view::npos? std::string_view() : rest.substr(e);
    size_t at = auth.rfind('@'); std::string_view hp = at==std::string_view::npos? auth : auth.substr(at+1);
    size_t colon = hp.find(':'); std::string_view h = colon==std::string_view::npos? hp : hp.substr(0,colon);
    host_.assign(h); for(char&c:host_) if(c>='A'&&c<='Z') c+=32;
    if (host_.empty()) return;
    std::string t(tail); if (t.empty() || t[0]!='/') t = "/" + t;
    spec_ = scheme_ + "://" + std::string(auth.substr(0, at==std::string_view::npos?0:at+1)) + host_ + (colon==std::string_view::npos? "" : std::string(hp.substr(colon))) + t;
    valid_ = true;
  }
  bool valid_ = false; std::string scheme_, host_, spec_;
};
