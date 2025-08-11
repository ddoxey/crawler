#pragma once

#include <string>
#include <set>
#include <optional>
#include "URL.hpp"
#include "CacheManager.hpp"
#include "URLManager.hpp"
#include "HttpResponse.hpp"
#include "LuaProcessor.hpp"

class Crawler {
 public:
  Crawler(const std::set<URL>& batch, CacheManager& cache, LuaProcessor& luap,
          URLManager& urlm);
  void Crawl();
  std::optional<HttpResponse> Fetch(const URL& url);

 private:
  std::string Fetch(const URL& url) const;
  static size_t WriteBodyCallback(char* ptr, size_t size, size_t nmemb,
                                  void* userdata);
  static size_t WriteHeaderCallback(char* ptr, size_t size, size_t nmemb,
                                    void* userdata);

  std::set<URL> urls_;
  CacheManager& cache_;
  LuaProcessor& luap_;
  URLManager& urlm_;
};
