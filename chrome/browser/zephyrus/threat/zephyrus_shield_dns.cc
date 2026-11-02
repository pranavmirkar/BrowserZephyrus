// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/threat/zephyrus_shield_dns.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "base/values.h"
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

constexpr char kResolver[] = "https://security.cloudflare-dns.com/dns-query";
constexpr size_t kMaxAnswerBytes = 8 * 1024;
constexpr size_t kMaxCachedHosts = 4096;
constexpr base::TimeDelta kRequestTimeout = base::Seconds(3);
constexpr base::TimeDelta kCleanTtl = base::Minutes(10);
constexpr base::TimeDelta kBlockedTtl = base::Minutes(30);
constexpr base::TimeDelta kUnknownTtl = base::Minutes(1);
// A page can navigate frames to endless made-up subdomains. These bound what that
// can cost: at most this many requests in flight, and this many per minute; the
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
  if (pending_.size() >= kMaxInFlight || recent_requests_.size() >= kMaxPerMinute) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), Verdict::kUnknown));
    return;
  }
  recent_requests_.push_back(now);

  GURL url = net::AppendQueryParameter(GURL(kResolver), "name", host);
  url = net::AppendQueryParameter(url, "type", "A");
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE | net::LOAD_BYPASS_CACHE;
  request->headers.SetHeader("Accept", "application/dns-json");

  constexpr net::NetworkTrafficAnnotationTag kAnnotation =
      net::DefineNetworkTrafficAnnotation("zephyrus_shield_dns", R"(
        semantics {
          sender: "Zephyrus Shield DNS"
          description:
            "Asks a free filtering DNS service (Cloudflare's security "
            "resolver) whether a website's host name is known to host phishing "
            "or malware, so the browser can warn before the page loads."
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
  Pending pending;
  pending.loader = network::SimpleURLLoader::Create(std::move(request),
                                                    kAnnotation);
  pending.loader->SetTimeoutDuration(kRequestTimeout);
  pending.callbacks.push_back(std::move(callback));
  network::SimpleURLLoader* loader = pending.loader.get();
  pending_.emplace(host, std::move(pending));
  loader->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&ShieldDnsChecker::OnResponse, weak_factory_.GetWeakPtr(),
                     host),
      kMaxAnswerBytes);
}

void ShieldDnsChecker::OnResponse(const std::string& host,
                                  std::optional<std::string> body) {
  Finish(host, body ? ParseAnswer(*body) : Verdict::kUnknown);
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
