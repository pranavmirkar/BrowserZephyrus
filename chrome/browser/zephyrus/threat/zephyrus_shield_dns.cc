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
#include "base/task/single_thread_task_runner.h"
#include "base/values.h"
#include "chrome/browser/zephyrus/threat/zephyrus_dns_wire.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
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
constexpr size_t kMaxAnswerBytes = 8 * 1024;
constexpr size_t kMaxCachedHosts = 4096;
constexpr base::TimeDelta kCloudflareTimeout = base::Seconds(3);
// The second opinion must not hold a page back for long when it is slow.
constexpr base::TimeDelta kQuad9Timeout = base::Seconds(2);
// After this many failed requests in a row a resolver is left alone for a while.
constexpr int kFailuresBeforeMute = 3;
constexpr base::TimeDelta kMuteFor = base::Minutes(5);
constexpr base::TimeDelta kCleanTtl = base::Minutes(10);
constexpr base::TimeDelta kBlockedTtl = base::Minutes(30);
constexpr base::TimeDelta kUnknownTtl = base::Minutes(1);
// A page can navigate frames to endless made-up subdomains. These bound what that
// can cost: at most this many hosts in flight, and this many per minute; the
// rest are answered "unknown" (treated as clean) at once, and nothing is sent.
constexpr size_t kMaxInFlight = 8;
constexpr size_t kMaxPerMinute = 120;

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
  if (host.empty() || host.size() > 253 || host.find('.') == std::string_view::npos ||
      host.front() == '.' || host.back() == '.' ||
      host.find(':') != std::string_view::npos ||  // IPv6 literal
      IsAsciiDigitsAndDots(host)) {                 // IPv4 literal
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
      const std::string* data = record ? record->FindString("data") : nullptr;
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

void ShieldDnsChecker::Check(const std::string& host, Callback callback) {
  const auto cached = cache_.find(host);
  if (cached != cache_.end()) {
    if (base::TimeTicks::Now() < cached->second.expires) {
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
  const bool nobody_to_ask = IsMuted(kCloudflare) && IsMuted(kQuad9);
  if (nobody_to_ask || pending_.size() >= kMaxInFlight ||
      recent_requests_.size() >= kMaxPerMinute) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), Verdict::kUnknown));
    return;
  }
  recent_requests_.push_back(now);

  Pending pending;
  pending.callbacks.push_back(std::move(callback));
  Pending& entry = pending_.emplace(host, std::move(pending)).first->second;
  for (const Source source : {kCloudflare, kQuad9}) {
    if (!IsMuted(source)) {
      StartRequest(host, source, entry);
    }
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
  } else {
    const std::vector<uint8_t> query = BuildDnsQuery(host);
    if (query.empty()) {
      // Not a name a resolver can be asked about: this source has no opinion.
      pending.any_unknown = true;
      return;
    }
    std::string encoded;
    base::Base64UrlEncode(query, base::Base64UrlEncodePolicy::OMIT_PADDING, &encoded);
    url = net::AppendQueryParameter(GURL(kQuad9Url), "dns", encoded);
    accept = "application/dns-message";
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
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
  std::unique_ptr<network::SimpleURLLoader>& loader = pending.loaders[source];
  loader = network::SimpleURLLoader::Create(std::move(request), kAnnotation);
  loader->SetTimeoutDuration(source == kCloudflare ? kCloudflareTimeout
                                                   : kQuad9Timeout);
  ++pending.outstanding;
  loader->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&ShieldDnsChecker::OnResponse, weak_factory_.GetWeakPtr(), host,
                     source),
      kMaxAnswerBytes);
}

void ShieldDnsChecker::OnResponse(const std::string& host,
                                  Source source,
                                  std::optional<std::string> body) {
  const auto it = pending_.find(host);
  if (it == pending_.end()) {
    return;  // already settled by the other resolver
  }
  Health& health = health_[source];
  Verdict verdict = Verdict::kUnknown;
  if (body) {
    health.failures = 0;
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
  } else if (++health.failures >= kFailuresBeforeMute) {
    health.failures = 0;
    health.muted_until = base::TimeTicks::Now() + kMuteFor;
  }

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
  pending_.erase(it);
  if (cache_.size() >= kMaxCachedHosts) {
    cache_.clear();
  }
  const base::TimeDelta ttl = verdict == Verdict::kBlocked   ? kBlockedTtl
                              : verdict == Verdict::kClean   ? kCleanTtl
                                                             : kUnknownTtl;
  cache_[host] = {verdict, base::TimeTicks::Now() + ttl};
  for (Callback& callback : callbacks) {
    std::move(callback).Run(verdict);
  }
}

}  // namespace zephyrus_threat
