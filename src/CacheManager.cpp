#include "CacheManager.hpp"

#include <fstream>
#include <filesystem>

bool CacheManager::IsExpired(const std::filesystem::path& file) const {
  std::error_code ec;
  auto ftime = std::filesystem::last_write_time(file, ec);
  if (ec)
    return true;  // unreadable? treat as expired
  auto now = std::filesystem::file_time_type::clock::now();
  auto age = now - ftime;
  // errors with written timestamp or current system time
  if (age < decltype(age)::zero())
    age = decltype(age)::zero();
  auto max_age = std::chrono::duration_cast<decltype(age)>(max_age_s_);
  return age > max_age;
}

bool CacheManager::IsCached(const URL& url) const {
  std::filesystem::path cache_filename = dir_ / url.GetSha256();
  return std::filesystem::exists(cache_filename) && !IsExpired(cache_filename);
}

std::optional<std::string> CacheManager::Fetch(const URL& url) const {
  std::filesystem::path p = dir_ / url.GetSha256();
  std::error_code ec;
  if (!std::filesystem::exists(p, ec) || IsExpired(p))
    return std::nullopt;

  std::ifstream in(p, std::ios::binary);
  if (!in)
    return std::nullopt;

  // Read whole file
  std::string data((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  return in ? std::optional<std::string>(std::move(data)) : std::nullopt;
}

void CacheManager::Store(const URL& url, const std::string& content) {
  std::filesystem::create_directories(dir_);  // error_code overload preferred
  std::filesystem::path filename = dir_ / url.GetSha256();
  std::filesystem::path tmp = filename;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.flush();
    if (!out) { /* handle error */
      return;
    }
  }
  std::filesystem::rename(tmp, filename);
}

void CacheManager::Store(const URL& url, const nlohmann::json& data,
                         const std::string& ext) {
  std::filesystem::create_directories(dir_);
  std::filesystem::path filename = dir_ / url.GetSha256();
  filename.replace_extension(ext);
  std::filesystem::path tmp = filename;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    auto dumped = data.dump(2);
    out.write(dumped.data(), static_cast<std::streamsize>(dumped.size()));
    out.put('\n');
    out.flush();
    if (!out) { /* handle error */
      return;
    }
  }
  std::filesystem::rename(tmp, filename);
}

void CacheManager::Store(const URL& url, const HttpResponse& response) {
  Store(url, response.GetBody());
  nlohmann::json headers;
  for (const auto& [key, val] : response.GetHeaders()) {
    headers[key] = val;
  }
  Store(url, headers, "headers");
}
