#include "Crawler.hpp"
#include "Logger.hpp"
#include "UAgent.hpp"

#include <algorithm>
#include <chrono>
#include <curl/curl.h>
#include <iostream>
#include <thread>
#include <thread>

Crawler::Crawler(const std::set<URL>& batch,
                 const std::chrono::milliseconds& rate_limit,
                 const std::filesystem::path& user_agent_list,
                 CacheManager& cache, LuaProcessor& luap, URLManager& urlm)
    : urls_{batch},
      rate_limit_{rate_limit},
      agent_{user_agent_list},
      cache_{cache},
      luap_{luap},
      urlm_{urlm} {
  next_allowed_ = std::chrono::steady_clock::now();
}

void Crawler::Crawl() {
  for (URL url : urls_) {
    logr::debug;
    for (size_t attempt = 1; attempt <= 3; attempt++) {
      auto content = cache_.Fetch(url);
      logr::debug << " Attempt: " << attempt;
      logr::debug << "     URL: " << url;
      logr::debug << "  SHA256: " << url.GetSha256();
      if (!content.has_value()) {
        auto response = Fetch(url);
        if (!response.has_value()) {
          break;
        }
        if (response->IsOkay()) {
          logr::debug << "HTTP OK";
          content.reset();
          content = response->GetBody();
          cache_.Store(url, *response);
        }
      }
      if (content.has_value()) {
        if (auto result = luap_.Process(url, *content); result.has_value()) {
          cache_.Store(url, *result);

          if (auto it = result->find("urls");
              it != result->end() && it->is_array()) {
            std::unordered_set<URL> new_urls;
            new_urls.reserve(it->size());
            for (const auto& v : *it) {
              if (v.is_string()) {
                auto new_url = URL(v.get<std::string>()).Resolve(url);
                if (new_url.GetDomain() == url.GetDomain()) {
                  new_urls.insert(std::move(new_url));
                }
              }
            }
            urlm_.Store(url.GetDomain(), new_urls);
          }

          auto redirect = luap_.GetClientRedirect();

          if (redirect.has_value()) {
            url = redirect->base.has_value()
                    ? URL(*redirect->base).Resolve(redirect->url)
                    : url.Resolve(redirect->url);
            if (redirect->delay > 0) {
              std::this_thread::sleep_for(
                std::chrono::seconds(redirect->delay));
            }
            continue;
          }
          break;
        }
        logr::debug;
      }
    }
  }
}

std::optional<HttpResponse> Crawler::Fetch(const URL& url) {
  Dwell();

  CURL* curl = curl_easy_init();

  if (!curl) {
    logr::debug << "[Crawler] failed to init CURL";
    return std::nullopt;
  }

  // A sensible User-UAgent helps with some sites
  curl_easy_setopt(curl, CURLOPT_USERAGENT, agent_.String());

  curl_easy_setopt(curl, CURLOPT_URL, url.ToString().c_str());

  // Follow 3xx redirects automatically
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  // Cap the redirect chain to avoid loops
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);

  // Set Referer automatically on redirects (optional)
  curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 1L);

  // Prefer HTTP/2 when available (optional but nice)
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);

  // Auto-decompress gzip/br (server dependent)
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

  // Verbosity
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);

  // Make sure TLS trust uses your CentOS CA bundle
  curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/pki/tls/certs/ca-bundle.crt");

  HttpResponse resp;

  // Body callback
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBodyCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);

  // Header callback
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteHeaderCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp);

  // Perform the request
  CURLcode code = curl_easy_perform(curl);

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  resp.SetStatusCode(http_code);

  long redirect_count = 0;
  curl_easy_getinfo(curl, CURLINFO_REDIRECT_COUNT, &redirect_count);
  resp.SetRedirectCount(redirect_count);

  // Final effective URL after following redirects (or the original if none)
  char* effective_url = nullptr;
  curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url);
  resp.SetEffectiveUrl(effective_url);

  curl_easy_cleanup(curl);

  if (code != CURLE_OK) {
    logr::warning << "[Crawler] CURL error: " << curl_easy_strerror(code);
    return std::nullopt;
  }

  return resp;
}

void Crawler::Dwell() {
  if (rate_limit_.count() <= 0)
    return;  // disabled

  using clock = std::chrono::steady_clock;

  auto now = clock::now();
  if (now < next_allowed_) {
    std::this_thread::sleep_until(next_allowed_);
    now = clock::now();
  }

  // Reserve the next slot. max(..) avoids bunching if we were behind.
  next_allowed_ = std::max(now, next_allowed_) + rate_limit_;
}

size_t Crawler::WriteBodyCallback(char* ptr, size_t size, size_t nmemb,
                                  void* userdata) {
  auto* resp = static_cast<HttpResponse*>(userdata);
  resp->AppendBody(ptr, size * nmemb);
  return size * nmemb;
}

size_t Crawler::WriteHeaderCallback(char* ptr, size_t size, size_t nmemb,
                                    void* userdata) {
  auto* resp = static_cast<HttpResponse*>(userdata);
  // ptr may include the “\r\n” at the end
  std::string line(ptr, size * nmemb);
  resp->AddHeaderLine(line);
  return size * nmemb;
}
