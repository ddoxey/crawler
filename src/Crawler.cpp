#include "Crawler.hpp"
#include <curl/curl.h>
#include <iostream>

Crawler::Crawler(const std::set<URL>& batch, CacheManager& cache,
                 LuaProcessor& luap)
    : urls_{batch}, cache_{cache}, luap_{luap} {
  if (const char* dbg = std::getenv("DEBUG")) {
    debug_ = (std::string(dbg) != "0");
  }
}

void Crawler::Crawl() {
  for (URL url : urls_) {
    if (debug_) {
      std::cerr << std::endl;
    }
    for (size_t attempt = 1; attempt <= 3; attempt++) {
      auto content = cache_.Fetch(url);
      if (debug_) {
        std::cerr << " Attempt: " << attempt << std::endl;
        std::cerr << "     URL: " << url << std::endl;
        std::cerr << "  SHA256: " << url.GetSha256() << std::endl;
      }
      if (!content.has_value()) {
        auto response = Fetch(url);
        if (!response.has_value()) {
          break;
        }
        if (response->IsOkay()) {
          if (debug_) {
            std::cerr << "HTTP OK" << std::endl;
          }
          content.reset();
          content = response->GetBody();
          cache_.Store(url, *response);
        } else if (response->IsRedirect()) {
          auto location = response->GetHeader("Location");
          if (location.has_value()) {
            cache_.Store(url, *response);
            if (debug_) {
              std::cerr << "REDIRECT: " << *location << std::endl;
            }
            url = *location;
            continue;
          }
          if (debug_) {
            std::cerr << "BAD REDIRECT" << std::endl;
          }
          break; // bad redirect
        }
      }
      if (content.has_value()) {
        if (auto result = luap_.Process(url, *content); result.has_value()) {
            cache_.Store(url, *result);
        }
        break;
      }
      if (debug_) {
        std::cerr << std::endl;
      }
    }
  }
}

std::optional<HttpResponse> Crawler::Fetch(const URL& url) {
  CURL* curl = curl_easy_init();

  if (!curl) {
    std::cerr << "[Crawler] failed to init CURL\n";
    return std::nullopt;
  }

  HttpResponse resp;

  curl_easy_setopt(curl, CURLOPT_URL, url.ToString().c_str());

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

  curl_easy_cleanup(curl);

  if (code != CURLE_OK) {
    std::cerr << "[Crawler] CURL error: " << curl_easy_strerror(code) << "\n";
    return std::nullopt;
  }

  return resp;
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
