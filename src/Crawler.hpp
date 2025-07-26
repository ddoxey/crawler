#pragma once

#include <string>
#include <set>
#include <optional>
#include "URL.hpp"
#include "CacheManager.hpp"
#include "LuaProcessor.hpp"

class Crawler {
 public:
  Crawler(const std::set<URL>& batch, CacheManager& cache, LuaProcessor& luap);
  void Crawl();
  std::optional<std::string> Fetch(const URL& url);

 private:
  std::string Fetch(const URL& url) const;
  std::set<URL> urls_;
  CacheManager& cache_;
  LuaProcessor& luap_;
};
