// Tiny public-suffix approximation (shim). Real Chromium uses a gperf perfect hash over the full PSL.
#pragma once
#include <string>
#include <string_view>
#include <unordered_set>
#include "url/gurl.h"
namespace net::registry_controlled_domains {
enum PrivateRegistryFilter { EXCLUDE_PRIVATE_REGISTRIES, INCLUDE_PRIVATE_REGISTRIES };
enum UnknownRegistryFilter { EXCLUDE_UNKNOWN_REGISTRIES, INCLUDE_UNKNOWN_REGISTRIES };
inline const std::unordered_set<std::string_view>& TwoLevel() {
  static const std::unordered_set<std::string_view> s = {"co.uk","org.uk","ac.uk","gov.uk","com.au","net.au","org.au","co.in","net.in","org.in","gov.in","ac.in","co.jp","ne.jp","or.jp","com.br","com.cn","com.mx","co.nz","co.za","com.sg","com.tr","com.tw","com.hk","co.kr","co.id","com.ar","com.ua","com.pl","github.io","blogspot.com","herokuapp.com","vercel.app","web.app","pages.dev","netlify.app"};
  return s; }
inline size_t GetCanonicalHostRegistryLength(std::string_view host, UnknownRegistryFilter, PrivateRegistryFilter) {
  size_t last = host.rfind('.'); if (last==std::string_view::npos) return 0;
  size_t prev = host.rfind('.', last-1);
  if (prev!=std::string_view::npos && TwoLevel().count(host.substr(prev+1))) return host.size()-prev-1;
  return host.size()-last-1; }
inline std::string_view DomainAndRegistry(std::string_view host) {
  size_t reg = GetCanonicalHostRegistryLength(host, EXCLUDE_UNKNOWN_REGISTRIES, EXCLUDE_PRIVATE_REGISTRIES);
  if (reg==0 || reg+1>=host.size()) return host;
  size_t dot = host.rfind('.', host.size()-reg-2);
  return dot==std::string_view::npos? host : host.substr(dot+1); }
inline bool SameDomainOrHost(const GURL& a, const GURL& b, PrivateRegistryFilter) { return DomainAndRegistry(a.host())==DomainAndRegistry(b.host()); }
}
