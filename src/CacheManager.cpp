#include "CacheManager.hpp"

#include <fstream>
#include <filesystem>

bool CacheManager::IsExpired(const std::filesystem::path& file) const {
  auto ftime = std::filesystem::last_write_time(file);
  auto now = std::filesystem::file_time_type::clock::now();
  auto age = now - ftime;
  auto max_age = std::chrono::duration_cast<decltype(age)>(max_age_s_);
  return age > max_age;
}

bool CacheManager::IsCached(const URL& url) const {
  std::filesystem::path cache_filename = dir_ / url.GetSha256();
  return std::filesystem::exists(cache_filename) && !IsExpired(cache_filename);
}

std::optional<std::string> CacheManager::Fetch(const URL& url) const {
  std::filesystem::path cache_filename = dir_ / url.GetSha256();
  if (std::filesystem::exists(cache_filename) && !IsExpired(cache_filename)) {
    std::ifstream file(cache_filename);
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
  }
  return {};
}

void CacheManager::Store(const URL& url, const std::string& content) {
  std::filesystem::path cache_filename = dir_ / url.GetSha256();
  std::ofstream file(cache_filename);
  file << content;
}

void CacheManager::Store(const URL& url, const nlohmann::json& data,
                         const std::string& ext) {
  std::filesystem::path cache_filename = dir_ / url.GetSha256();
  cache_filename.replace_extension(ext);
  std::ofstream file(cache_filename);
  file << data.dump(2) << std::endl;
}

void CacheManager::Store(const URL& url, const HttpResponse& response) {
  Store(url, response.GetBody());
  nlohmann::json headers;
  for (const auto& [key, val] : response.GetHeaders()) {
    headers[key] = val;
  }
  Store(url, headers, "headers");
}
