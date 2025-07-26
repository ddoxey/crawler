#pragma once

#include <optional>
#include <filesystem>
#include "URL.hpp"

class CacheManager {
 public:
  CacheManager(const std::filesystem::path& dir,
               const std::chrono::seconds max_age_seconds)
      : dir_{dir}, max_age_s_{max_age_seconds} {
  }
  CacheManager(const CacheManager&) = delete;

  bool IsCached(const URL& url) const;

  std::optional<std::string> Fetch(const URL& url) const;

  void Store(const URL& url, const std::string& content);

 private:
  bool IsExpired(const std::filesystem::path& file) const;

  std::filesystem::path dir_;
  std::chrono::seconds max_age_s_;
};
