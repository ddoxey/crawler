#include "Crawler.hpp"
#include <curl/curl.h>
#include <iostream>

static size_t writeCallback(void* contents, size_t size, size_t nmemb,
                            std::string* output) {
  size_t totalSize = size * nmemb;
  output->append((char*)contents, totalSize);
  return totalSize;
}

Crawler::Crawler(const std::set<URL>& batch, CacheManager& cache,
                 LuaProcessor& luap)
    : urls_{batch}, cache_{cache}, luap_{luap} {
}

void Crawler::Crawl() {
  for (const auto& url : urls_) {
    auto content = cache_.Fetch(url);
    if (!content.has_value()) {
      content = Fetch(url);
      if (content.has_value()) {
        cache_.Store(url, *content);
      }
    }
    if (content.has_value()) {
      luap_.Process(url, *content);
    }
  }
}

std::optional<std::string> Crawler::Fetch(const URL& url) {
  CURL* curl = curl_easy_init();
  std::string response;

  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url.ToString().c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
  } else {
    std::cerr << "Failed to initialize CURL." << std::endl;
    return {};
  }

  return response;
}
