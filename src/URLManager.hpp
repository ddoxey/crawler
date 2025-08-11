#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "URL.hpp"
#include <vector>

class URLManager {
 public:
  URLManager(const std::filesystem::path& dir);

  void LoadFromFile(const std::filesystem::path& filename);

  const std::vector<URL>& GetURLs() const;

  std::unordered_map<URL, std::set<URL>> GetBatchesByDomain() const;

  void Store(const URL& domain, const std::unordered_set<URL>& urls) const;

 private:
  std::vector<URL> urls_;
  std::filesystem::path dir_;
};
