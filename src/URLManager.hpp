#pragma once

#include <set>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <string>
#include "URL.hpp"

class URLManager {
 public:
  URLManager(const std::filesystem::path& dir);

  void LoadFromFile(const std::filesystem::path& filename);

  const std::vector<URL>& GetURLs() const;

  std::unordered_map<URL, std::set<URL>> GetBatchesByDomain() const;

 private:
  std::vector<URL> urls_;
  std::filesystem::path dir_;
};
