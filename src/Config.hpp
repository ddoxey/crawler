#pragma once

#include <filesystem>

class Config {
 public:
  Config();
  Config(const std::filesystem::path& conf_file);

  Config(const Config& conf) = default;

  std::filesystem::path GetCacheDir() const;

  std::chrono::seconds GetCacheAgeLimit() const;

  std::filesystem::path GetDataDir() const;

  std::filesystem::path GetPluginsDir() const;

  std::filesystem::path GetScriptDir() const;

  std::chrono::milliseconds GetRateLimit() const;

 private:
  std::filesystem::path config_file_;
  std::filesystem::path cache_dir_;
  std::chrono::seconds cache_age_limit_s_;
  std::filesystem::path data_dir_;
  std::filesystem::path plugins_dir_;
  std::filesystem::path script_dir_;
  std::chrono::milliseconds rate_limit_ms_;
};
