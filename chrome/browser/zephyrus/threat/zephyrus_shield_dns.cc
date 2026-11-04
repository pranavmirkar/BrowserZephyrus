// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/threat/zephyrus_shield_dns.h"

#include <algorithm>
#include <utility>

#include "base/base64url.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/values.h"
#include "chrome/browser/zephyrus/threat/zephyrus_dns_wire.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/dns/public/dns_protocol.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "url/gurl.h"

namespace zephyrus_threat {

namespace {

constexpr char kCloudflareUrl[] = "https://security.cloudflare-dns.com/dns-query";
constexpr char kQuad9Url[] = "https://dns.quad9.net/dns-query";
// The same service by address, for the second attempt. A different origin, so it
// opens a new connection instead of reusing the one that may have gone quiet.
// Quad9's certificate names this address.
constexpr char kQuad9HedgeUrl[] = "https://9.9.9.9/dns-query";
// Chromium reaches Quad9 over HTTP/3, and a session left idle for ~15 s is
// closed by the server without the client noticing: the next request then
// stalls until its own timeout (measured: 2 of 5 runs of the browser test, with
// Quad9 itself answering in 70 ms from a fresh connection). So when Quad9 has not
// answered after this long, the question is asked again on a new connection and
// the first answer wins.
constexpr base::TimeDelta kQuad9HedgeDelay = base::Milliseconds(700);
constexpr size_t kMaxAnswerBytes = 8 * 1024;
constexpr size_t kMaxCachedHosts = 4096;
// All attempts share a deadline, so a delayed retry cannot extend the
// navigation wait when the resolvers are unreachable.
constexpr base::TimeDelta kLookupBudget = base::Milliseconds(1500);
// After this many failed requests in a row a resolver is left alone for a while.
constexpr int kFailuresBeforeMute = 3;
constexpr base::TimeDelta kInitialMute = base::Seconds(10);
constexpr base::TimeDelta kMaxMute = base::Minutes(1);
constexpr base::TimeDelta kCleanTtl = base::Minutes(10);
constexpr base::TimeDelta kBlockedTtl = base::Minutes(30);
constexpr base::TimeDelta kUnknownTtl = base::Minutes(1);
// A page can navigate frames to endless made-up subdomains. These bound what that
// can cost: at most this many hosts in flight, and this many per minute; the
// rest are answered "unknown" (treated as clean) at once, and nothing is sent.
constexpr size_t kMaxInFlight = 8;
constexpr size_t kMaxPerMinute = 120;

// A SimpleURLLoader must not be destroyed from inside its own completion
// callback (it still has work to do after the callback returns, and 156 treats
// that as a fatal dangling pointer). So loaders are always retired later.
void RetireLoader(std::unique_ptr<network::SimpleURLLoader> loader) {
  if (loader) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(loader));
  }
}

bool IsAsciiDigitsAndDots(std::string_view host) {
  return !host.empty() &&
         std::ranges::all_of(host, [](char c) {
           return (c >= '0' && c <= '9') || c == '.';
         });
}

}  // namespace

ShieldDnsChecker::ShieldDnsChecker(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : url_loader_factory_(std::move(url_loader_factory)) {}

ShieldDnsChecker::~ShieldDnsChecker() = default;

// static
bool ShieldDnsChecker::IsEligibleHost(std::string_view host) {
  // The optional DNS root label does not change which host this names.
  if (host.ends_with('.')) {
    host.remove_suffix(1);
  }
  if (host.empty() || host.size() > 253 || host.find('.') == std::string_view::npos ||
      host.front() == '.' || host.back() == '.' ||
      host.find(':') != std::string_view::npos ||  // IPv6 literal
      IsAsciiDigitsAndDots(host)) {                 // IPv4 literal
    return false;
  }
  // RFC 8375 reserves this name for local residential networks.
  if (host == "home.arpa" || host.ends_with(".home.arpa")) {
    return false;
  }
  // Names that only mean something on this machine or this network.
  for (const char* suffix : {".local", ".localhost", ".internal", ".lan", ".home",
                             ".corp", ".intranet", ".test", ".invalid",
                             ".example", ".onion"}) {
    if (host.ends_with(suffix)) {
      return false;
    }
  }
  return true;
}

// static
ShieldDnsChecker::Verdict ShieldDnsChecker::ParseAnswer(std::string_view json) {
  std::optional<base::DictValue> dict = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC, 8);
  if (!dict) {
    return Verdict::kUnknown;
  }
  const std::optional<int> status = dict->FindInt("Status");
  if (!status || *status != 0) {
    return Verdict::kUnknown;
  }
  const base::ListValue* answers = dict->FindList("Answer");
  if (answers) {
    for (const base::Value& answer : *answers) {
      const base::DictValue* record = answer.GetIfDict();
      if (!record || record->FindInt("type") != net::dns_protocol::kTypeA) {
        continue;
      }
      const std::string* data = record->FindString("data");
      if (data && *data == "0.0.0.0") {
        return Verdict::kBlocked;
      }
    }
  }
  return Verdict::kClean;
}

bool ShieldDnsChecker::IsMuted(Source source) const {
  return base::TimeTicks::Now() < health_[source].muted_until;
}

bool ShieldDnsChecker::CanStartLookup(Source source) const {
  if (IsMuted(source)) {
    return false;
  }
  if (health_[source].cooldown.is_zero()) {
    return true;
  }
  // Allow one recovery attempt at a time after the cooldown expires.
  return std::ranges::none_of(pending_, [source](const auto& entry) {
    const Pending& pending = entry.second;
    return pending.loaders[source] ||
           (source == kQuad9 && pending.loaders[kQuad9Hedge]);
  });
}

void ShieldDnsChecker::Check(std::string host, Callback callback) {
  if (host.ends_with('.')) {
    host.pop_back();
  }
  const auto cached = cache_.find(host);
  if (cached != cache_.end()) {
    const CacheEntry& entry = cached->second;
    if (base::TimeTicks::Now() < entry.expires &&
        (entry.verdict != Verdict::kUnknown ||
         entry.recovery_generation == recovery_generation_)) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), cached->second.verdict));
      return;
    }
    cache_.erase(cached);
  }
  const auto in_flight = pending_.find(host);
  if (in_flight != pending_.end()) {
    in_flight->second.callbacks.push_back(std::move(callback));
    return;
  }

  VLOG(1) << "Zephyrus Shield DNS asks about " << host;
  const base::TimeTicks now = base::TimeTicks::Now();
  while (!recent_requests_.empty() &&
         now - recent_requests_.front() > base::Minutes(1)) {
    recent_requests_.pop_front();
  }
  const bool nobody_to_ask =
      !CanStartLookup(kCloudflare) && !CanStartLookup(kQuad9);
  if (nobody_to_ask || pending_.size() >= kMaxInFlight ||
      recent_requests_.size() >= kMaxPerMinute) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), Verdict::kUnknown));
    return;
  }
  recent_requests_.push_back(now);

  Pending pending;
  pending.deadline = now + kLookupBudget;
  pending.recovery_generation = recovery_generation_;
  pending.callbacks.push_back(std::move(callback));
  Pending& entry = pending_.emplace(host, std::move(pending)).first->second;
  for (const Source source : {kCloudflare, kQuad9}) {
    if (!CanStartLookup(source)) {
      entry.any_unknown = true;
    } else {
      StartRequest(host, source, entry);
    }
  }
  if (entry.loaders[kQuad9]) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ShieldDnsChecker::StartHedge,
                       weak_factory_.GetWeakPtr(), host),
        kQuad9HedgeDelay);
  }
  if (entry.outstanding == 0) {
    // No resolver could be asked about this name (a muted one, or a name it cannot
    // carry). Nothing will call back, so settle it here, still never synchronously.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&ShieldDnsChecker::Finish,
                                  weak_factory_.GetWeakPtr(), host, Verdict::kUnknown));
  }
}

void ShieldDnsChecker::StartRequest(const std::string& host,
                                    Source source,
                                    Pending& pending) {
  GURL url;
  std::string accept;
  if (source == kCloudflare) {
    url = net::AppendQueryParameter(GURL(kCloudflareUrl), "name", host);
    url = net::AppendQueryParameter(url, "type", "A");
    accept = "application/dns-json";
  } else {  // kQuad9 and kQuad9Hedge
    const std::vector<uint8_t> query = BuildDnsQuery(host);
    if (query.empty()) {
      // Not a name a resolver can be asked about: this source has no opinion.
      pending.any_unknown = true;
      return;
    }
    std::string encoded;
    base::Base64UrlEncode(query, base::Base64UrlEncodePolicy::OMIT_PADDING, &encoded);
    url = net::AppendQueryParameter(
        GURL(source == kQuad9Hedge ? kQuad9HedgeUrl : kQuad9Url), "dns", encoded);
    accept = "application/dns-message";
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->redirect_mode = network::mojom::RedirectMode::kError;
  request->load_flags = net::LOAD_DISABLE_CACHE | net::LOAD_BYPASS_CACHE;
  request->headers.SetHeader("Accept", accept);

  constexpr net::NetworkTrafficAnnotationTag kAnnotation =
      net::DefineNetworkTrafficAnnotation("zephyrus_shield_dns", R"(
        semantics {
          sender: "Zephyrus Shield DNS"
          description:
            "Asks two free filtering DNS services (Cloudflare's security "
            "resolver and Quad9) whether a website's host name is known to host "
            "phishing or malware, so the browser can warn before the page loads."
          trigger:
            "Only when the user has turned Shield DNS on, and only the first "
            "time (per ten minutes) a navigation goes to a host name that is "
            "not on the built-in list of popular sites."
          data:
            "The host name of the page being opened, nothing else: no path, "
            "query, cookies or identifiers."
          destination: OTHER
        }
        policy {
          cookies_allowed: NO
          setting: "Settings: Shield DNS (off by default)."
          policy_exception_justification: "Not yet implemented."
        })");
  const base::TimeDelta remaining = pending.deadline - base::TimeTicks::Now();
  if (!remaining.is_positive()) {
    pending.any_unknown = true;
    return;
  }
  std::unique_ptr<network::SimpleURLLoader>& loader = pending.loaders[source];
  loader = network::SimpleURLLoader::Create(std::move(request), kAnnotation);
  loader->SetTimeoutDuration(remaining);
  if (source != kQuad9Hedge) {
    ++pending.outstanding;
  }
  loader->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&ShieldDnsChecker::OnResponse, weak_factory_.GetWeakPtr(), host,
                     source),
      kMaxAnswerBytes);
}

void ShieldDnsChecker::StartHedge(const std::string& host) {
  const auto it = pending_.find(host);
  if (it == pending_.end() || it->second.quad9_done ||
      it->second.quad9_hedge_started || IsMuted(kQuad9)) {
    return;
  }
  VLOG(1) << "Zephyrus Shield DNS asks Quad9 again for " << host;
  it->second.quad9_hedge_started = true;
  StartRequest(host, kQuad9Hedge, it->second);
}

void ShieldDnsChecker::OnResponse(const std::string& host,
                                  Source source,
                                  std::optional<std::string> body) {
  const auto it = pending_.find(host);
  if (it == pending_.end()) {
    return;  // already settled by the other resolver
  }
  const bool is_quad9 = source == kQuad9 || source == kQuad9Hedge;
  if (is_quad9) {
    if (it->second.quad9_done) {
      return;
    }
    // A failed primary need not wait for the delayed retry task.
    if (!body && source == kQuad9) {
      StartHedge(host);
    }
    // One attempt failing while the other is still in flight is not yet an
    // answer from Quad9: wait for the twin.
    const Source twin = source == kQuad9 ? kQuad9Hedge : kQuad9;
    if (!body && it->second.loaders[twin]) {
      RetireLoader(std::move(it->second.loaders[source]));
      return;
    }
    it->second.quad9_done = true;
    // The other attempt, if any, is no longer needed.
    RetireLoader(std::move(
        it->second.loaders[source == kQuad9 ? kQuad9Hedge : kQuad9]));
    source = kQuad9;
  }
  Health& health = health_[source];
  Verdict verdict = Verdict::kUnknown;
  if (body) {
    if (!health.cooldown.is_zero()) {
      // A recovered resolver can now answer earlier inconclusive lookups.
      ++recovery_generation_;
    }
    health = Health();
    if (source == kCloudflare) {
      verdict = ParseAnswer(*body);
    } else {
      switch (ParseDnsAnswer(base::as_byte_span(*body), host)) {
        case WireAnswer::kBlocked:
          verdict = Verdict::kBlocked;
          break;
        case WireAnswer::kClean:
          verdict = Verdict::kClean;
          break;
        case WireAnswer::kUnknown:
          break;
      }
    }
  } else if (!IsMuted(source) &&
             (!health.cooldown.is_zero() ||
              ++health.failures >= kFailuresBeforeMute)) {
    health.failures = 0;
    health.cooldown = health.cooldown.is_zero()
                          ? kInitialMute
                          : std::min(health.cooldown * 2, kMaxMute);
    health.muted_until = base::TimeTicks::Now() + health.cooldown;
  }

  VLOG(1) << "Zephyrus Shield DNS answer for " << host << " from "
          << (source == kCloudflare ? "cloudflare" : "quad9") << ": "
          << (body ? base::NumberToString(body->size()) + " bytes" : "no answer")
          << " -> " << (verdict == Verdict::kBlocked ? "blocked"
                        : verdict == Verdict::kClean ? "clean" : "unknown");
  if (verdict == Verdict::kBlocked) {
    Finish(host, Verdict::kBlocked);  // decisive: no need to wait for the other
    return;
  }
  Pending& pending = it->second;
  pending.any_unknown |= verdict == Verdict::kUnknown;
  if (--pending.outstanding == 0) {
    Finish(host, pending.any_unknown ? Verdict::kUnknown : Verdict::kClean);
  }
}

void ShieldDnsChecker::Finish(const std::string& host, Verdict verdict) {
  const auto it = pending_.find(host);
  if (it == pending_.end()) {
    return;
  }
  std::vector<Callback> callbacks = std::move(it->second.callbacks);
  const size_t recovery_generation = it->second.recovery_generation;
  for (std::unique_ptr<network::SimpleURLLoader>& loader : it->second.loaders) {
    RetireLoader(std::move(loader));
  }
  pending_.erase(it);
  if (cache_.size() >= kMaxCachedHosts) {
    cache_.clear();
  }
  const base::TimeDelta ttl = verdict == Verdict::kBlocked   ? kBlockedTtl
                              : verdict == Verdict::kClean   ? kCleanTtl
                                                             : kUnknownTtl;
  cache_[host] = {verdict, base::TimeTicks::Now() + ttl, recovery_generation};
  for (Callback& callback : callbacks) {
    std::move(callback).Run(verdict);
  }}

}  // namespace zephyrus_threat
