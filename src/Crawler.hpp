#pragma once

#include <string>
#include <set>
#include <optional>
#include <filesystem>

#include "UAgent.hpp"
#include "URL.hpp"
#include "CacheManager.hpp"
#include "URLManager.hpp"
#include "HttpResponse.hpp"
#include "LuaProcessor.hpp"

class Crawler {
 public:
  Crawler(const std::set<URL>& batch,
          const std::chrono::milliseconds& rate_limit,
          const std::filesystem::path& user_agent_list, CacheManager& cache,
          LuaProcessor& luap, URLManager& urlm);
  void Crawl();
  std::optional<HttpResponse> Fetch(const URL& url);

 private:
  void Dwell();
  std::string Fetch(const URL& url) const;
  static size_t WriteBodyCallback(char* ptr, size_t size, size_t nmemb,
                                  void* userdata);
  static size_t WriteHeaderCallback(char* ptr, size_t size, size_t nmemb,
                                    void* userdata);

  std::set<URL> urls_;
  const std::chrono::milliseconds rate_limit_;
  UAgent agent_;
  CacheManager& cache_;
  LuaProcessor& luap_;
  URLManager& urlm_;
  std::chrono::steady_clock::time_point next_allowed_;
};
