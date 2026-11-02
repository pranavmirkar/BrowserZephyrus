// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_SHIELD_DNS_H_
#define CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_SHIELD_DNS_H_

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace zephyrus_threat {

// Asks a free filtering DNS resolver (Cloudflare's security resolver, which
// refuses names it has seen used for phishing and malware by answering 0.0.0.0)
// whether a hostname is blocked. Measured on 419 live phishing hosts reported
// in the last three days: it flagged 72%, against 60% for Google's Safe
// Browsing lists as Brave holds them (research/sb_free_signals.py).
//
// What it costs: the HOSTNAME goes to the resolver. So it is off unless the user
// turns it on, it is asked only for hosts that are not on the bundled list of
// popular sites, and a verdict is cached for minutes. No path, no query, no
// cookies, no identifiers are sent.
//
// UI thread only.
class ShieldDnsChecker {
 public:
  enum class Verdict {
    kClean,
    kBlocked,
    // The resolver was unreachable or answered something unexpected. Callers
    // treat this as clean: a missing answer must never block a site.
    kUnknown,
  };
  using Callback = base::OnceCallback<void(Verdict)>;

  explicit ShieldDnsChecker(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~ShieldDnsChecker();
  ShieldDnsChecker(const ShieldDnsChecker&) = delete;
  ShieldDnsChecker& operator=(const ShieldDnsChecker&) = delete;

  // Looks up `host` (answered from the cache when it can be) and runs
  // `callback` once, later, never before this function returns.
  void Check(const std::string& host, Callback callback);

  // Whether `host` is a public name worth asking about: not an address, not a
  // single label, not a local or internal name.
  static bool IsEligibleHost(std::string_view host);

  // The resolver's JSON answer to an A query. Only the explicit 0.0.0.0 answer
  // is a block; "does not exist" is not (the host may simply be mistyped).
  static Verdict ParseAnswer(std::string_view json);

 private:
  struct Pending {
    std::unique_ptr<network::SimpleURLLoader> loader;
    std::vector<Callback> callbacks;
  };
  struct CacheEntry {
    Verdict verdict;
    base::TimeTicks expires;
  };

  void OnResponse(const std::string& host, std::optional<std::string> body);
  void Finish(const std::string& host, Verdict verdict);

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::map<std::string, Pending> pending_;
  std::map<std::string, CacheEntry> cache_;
  // When recent requests were sent, for the per-minute cap.
  std::deque<base::TimeTicks> recent_requests_;
  base::WeakPtrFactory<ShieldDnsChecker> weak_factory_{this};
};

}  // namespace zephyrus_threat

#endif  // CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_SHIELD_DNS_H_
