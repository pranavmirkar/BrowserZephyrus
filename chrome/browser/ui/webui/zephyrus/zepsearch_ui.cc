// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/webui/zephyrus/zepsearch_ui.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/zephyrus/zepsearch_config.h"
#include "chrome/browser/ui/webui/zephyrus/zepsearch_resources.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace zephyrus::search {

namespace {

constexpr size_t kMaxResponseBytes = 2 * 1024 * 1024;
constexpr size_t kMaxResults = 100;

const net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("zepsearch_results", R"(
      semantics {
        sender: "ZepSearch internal results page"
        description: "Fetches results for a search explicitly submitted by the user."
        trigger: "A ZepSearch results page is opened or its page is changed."
        data: "Search text, page number, and optional search filters."
        destination: OTHER
      }
      policy {
        cookies_allowed: NO
        setting: "The user can select a different search provider."
        policy_exception_justification: "No enterprise policy exception is needed."
      })");

std::string Text(const base::DictValue& dict,
                 std::string_view key,
                 size_t limit = 4000) {
  const std::string* value = dict.FindString(key);
  return value ? std::string(base::TruncateUTF8ToByteSize(*value, limit))
               : std::string();
}

// Backend markup is data. Only these fields cross into the privileged page,
// and the page inserts every one of them with textContent.
base::ListValue TextItems(const base::DictValue& response,
                          std::string_view key,
                          std::string_view fallback_key) {
  base::ListValue output;
  const base::ListValue* items = response.FindList(key);
  if (!items) {
    return output;
  }
  for (const auto& item : *items) {
    if (output.size() >= 20) {
      break;
    }
    std::string value;
    if (item.is_string()) {
      value = item.GetString();
    } else if (item.is_dict()) {
      value = Text(item.GetDict(), fallback_key);
      if (value.empty()) {
        value = Text(item.GetDict(), "title");
      }
    } else if (item.is_list() && !item.GetList().empty() &&
               item.GetList()[0].is_string()) {
      value = item.GetList()[0].GetString();
    }
    if (!value.empty()) {
      output.Append(std::string(base::TruncateUTF8ToByteSize(value, 4000)));
    }
  }
  return output;
}

base::DictValue Error(std::string message) {
  base::DictValue out;
  out.Set("ok", false);
  out.Set("error", std::move(message));
  return out;
}

base::DictValue SanitizeResponse(const base::DictValue& response) {
  base::DictValue out;
  out.Set("ok", true);
  out.Set("query", Text(response, "query", 1000));
  base::ListValue rows;
  if (const auto* results = response.FindList("results")) {
    for (const auto& item : *results) {
      if (rows.size() >= kMaxResults) {
        break;
      }
      if (!item.is_dict()) {
        continue;
      }
      const auto& source = item.GetDict();
      GURL url(Text(source, "url", 8192));
      if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.has_username() ||
          url.has_password()) {
        continue;
      }
      base::DictValue row;
      row.Set("url", url.spec());
      row.Set("title", Text(source, "title", 500));
      row.Set("content", Text(source, "content"));
      row.Set("engine", Text(source, "engine", 100));
      rows.Append(std::move(row));
    }
  }
  out.Set("results", std::move(rows));
  out.Set("answers", TextItems(response, "answers", "answer"));
  out.Set("suggestions", TextItems(response, "suggestions", "suggestion"));
  out.Set("infoboxes", TextItems(response, "infoboxes", "content"));
  out.Set("unresponsive_engines",
          TextItems(response, "unresponsive_engines", "engine"));
  return out;
}

std::string ErrorDetail(const base::Value* body, int http_status) {
  if (body && body->is_dict()) {
    const auto* detail = body->GetDict().Find("detail");
    if (detail) {
      if (detail->is_string()) {
        return std::string(
            base::TruncateUTF8ToByteSize(detail->GetString(), 500));
      }
      if (detail->is_list()) {
        for (const auto& entry : detail->GetList()) {
          if (entry.is_dict()) {
            std::string message = Text(entry.GetDict(), "msg", 500);
            if (!message.empty()) {
              return message;
            }
          }
        }
      }
    }
  }
  return "ZepSearch returned HTTP " + base::NumberToString(http_status) + ".";
}

void HandleResource(const std::string& path,
                    content::WebUIDataSource::GotDataCallback callback) {
  const char* bytes = kSearchHtml;
  if (path == "search.js") {
    bytes = kSearchJs;
  } else if (path == "search.css") {
    bytes = kSearchCss;
  } else if (path == "favicon.svg") {
    bytes = kSearchFavicon;
  }
  std::move(callback).Run(base::MakeRefCounted<base::RefCountedString>(bytes));
}

class SearchHandler : public content::WebUIMessageHandler {
 public:
  explicit SearchHandler(Profile* profile) : profile_(profile) {}
  ~SearchHandler() override = default;

  void RegisterMessages() override {
    web_ui()->RegisterMessageCallback(
        "zepSearch",
        base::BindRepeating(&SearchHandler::Search, base::Unretained(this)));
  }

 private:
  void Reply(int id, base::DictValue response) {
    if (!IsJavascriptAllowed()) {
      return;
    }
    web_ui()->CallJavascriptFunctionUnsafe("zepSearchResponse", base::Value(id),
                                           base::Value(std::move(response)));
  }

  void Search(const base::ListValue& args) {
    AllowJavascript();
    ++generation_;
    loader_.reset();  // Cancels an older request for this page.
    if (args.size() != 4 || !args[0].is_int() || !args[1].is_string() ||
        !args[2].is_int() || !args[3].is_dict()) {
      return;
    }
    const int request_id = args[0].GetInt();
    const std::string& query = args[1].GetString();
    const int page = args[2].GetInt();
    if (query.empty() || query.size() > 1000 || page < 1 || page > 10000) {
      Reply(request_id, Error("Invalid query or page number."));
      return;
    }
    GURL base_url = GetApiBaseUrl();
    if (!base_url.is_valid()) {
      Reply(request_id, Error("ZepSearch API address is invalid. Check "
                              "--zepsearch-api-base-url."));
      return;
    }
    GURL url =
        net::AppendQueryParameter(base_url.Resolve("api/search"), "q", query);
    url = net::AppendQueryParameter(url, "pageno", base::NumberToString(page));
    const auto& options = args[3].GetDict();
    for (std::string_view key :
         {"categories", "engines", "language", "safesearch", "time_range"}) {
      const std::string* value = options.FindString(key);
      if (!value || value->empty() || value->size() > 100) {
        continue;
      }
      if (key == "safesearch" && *value != "0" && *value != "1" &&
          *value != "2") {
        continue;
      }
      if (key == "time_range" && *value != "day" && *value != "month" &&
          *value != "year") {
        continue;
      }
      url = net::AppendQueryParameter(url, key, *value);
    }
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = url;
    request->method = "GET";
    request->load_flags = net::LOAD_DISABLE_CACHE;
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    loader_ = network::SimpleURLLoader::Create(std::move(request),
                                               kTrafficAnnotation);
    loader_->SetAllowHttpErrorResults(true);
    loader_->SetTimeoutDuration(base::Seconds(15));
    loader_->DownloadToString(
        profile_->GetDefaultStoragePartition()
            ->GetURLLoaderFactoryForBrowserProcess()
            .get(),
        base::BindOnce(&SearchHandler::OnResponse, weak_factory_.GetWeakPtr(),
                       generation_, request_id),
        kMaxResponseBytes);
  }

  void OnResponse(int generation,
                  int request_id,
                  std::optional<std::string> body) {
    if (generation != generation_) {
      return;
    }
    const int net_error = loader_->NetError();
    const int status =
        loader_->ResponseInfo() && loader_->ResponseInfo()->headers
            ? loader_->ResponseInfo()->headers->response_code()
            : 0;
    loader_.reset();
    if (net_error != net::OK || !body) {
      Reply(request_id, Error("ZepSearch is offline or unreachable. Check the "
                              "hotspot and retry."));
      return;
    }
    std::optional<base::Value> parsed = base::JSONReader::Read(*body, 0);
    if (status < 200 || status >= 300) {
      Reply(request_id,
            Error(ErrorDetail(parsed ? &*parsed : nullptr, status)));
      return;
    }
    if (!parsed || !parsed->is_dict() ||
        !parsed->GetDict().FindList("results")) {
      Reply(request_id, Error("ZepSearch sent an invalid response."));
      return;
    }
    Reply(request_id, SanitizeResponse(parsed->GetDict()));
  }

  raw_ptr<Profile> profile_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  int generation_ = 0;
  base::WeakPtrFactory<SearchHandler> weak_factory_{this};
};

}  // namespace

ZepSearchUIConfig::ZepSearchUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         chrome::kChromeUIZepSearchHost) {}

ZepSearchUI::ZepSearchUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  auto* source = content::WebUIDataSource::CreateAndAdd(
      profile, chrome::kChromeUIZepSearchHost);
  source->SetRequestFilter(base::BindRepeating([](const std::string& path) {
                             // URLToRequestPath includes the query string.
                             // A search navigation therefore arrives as
                             // "?q=...", not an empty root path.
                             return path.empty() || path[0] == '?' ||
                                    path == "search.js" ||
                                    path == "search.css" ||
                                    path == "favicon.svg";
                           }),
                           base::BindRepeating(&HandleResource));
  web_ui->AddMessageHandler(std::make_unique<SearchHandler>(profile));
}

ZepSearchUI::~ZepSearchUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(ZepSearchUI)

}  // namespace zephyrus::search
