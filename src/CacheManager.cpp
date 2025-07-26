#include "CacheManager.hpp"

#include <fstream>
#include <filesystem>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

static std::string HashUrl(const std::string& url) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256((const unsigned char*)url.c_str(), url.size(), hash);
  std::ostringstream oss;
  for (auto byte : hash) {
    oss << std::hex << std::setw(2) << std::setfill('0') << (int)byte;
  }
  return oss.str();
}

bool CacheManager::IsExpired(const std::filesystem::path& file) const {
  auto ftime = std::filesystem::last_write_time(file);
  auto now = std::filesystem::file_time_type::clock::now();
  auto age = now - ftime;
  auto max_age = std::chrono::duration_cast<decltype(age)>(max_age_s_);
  return age > max_age;
}

bool CacheManager::IsCached(const URL& url) const {
  std::filesystem::path cache_filename = dir_ / HashUrl(url.ToString());
  return std::filesystem::exists(cache_filename) && !IsExpired(cache_filename);
}

std::optional<std::string> CacheManager::Fetch(const URL& url) const {
  std::filesystem::path cache_filename = dir_ / HashUrl(url.ToString());
  if (std::filesystem::exists(cache_filename) && !IsExpired(cache_filename)) {
    std::ifstream file(cache_filename);
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
  }
  return {};
}

void CacheManager::Store(const URL& url, const std::string& content) {
  std::filesystem::path cache_filename = dir_ / HashUrl(url.ToString());
  std::ofstream file(cache_filename);
  file << content;
}
