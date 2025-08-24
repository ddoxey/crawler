#pragma once

#include <filesystem>
#include <unordered_map>
#include "URL.hpp"

class Config {
 public:
  const std::chrono::milliseconds kDefaultRateLimit{500};

  Config();
  Config(const std::filesystem::path& conf_file);

  Config(const Config& conf) = default;

  std::filesystem::path GetCacheDir() const;

  std::chrono::seconds GetCacheAgeLimit() const;

  std::filesystem::path GetDataDir() const;

  std::filesystem::path GetPluginsDir() const;

  std::filesystem::path GetScriptDir() const;

  std::filesystem::path GetPemDir() const;

  std::filesystem::path GetUserUAgentList() const;

  const std::chrono::milliseconds GetRateLimit(const URL& domain) const;

 private:
  std::filesystem::path config_file_;
  std::filesystem::path cache_dir_;
  std::chrono::seconds cache_age_limit_s_;
  std::filesystem::path data_dir_;
  std::filesystem::path plugins_dir_;
  std::filesystem::path script_dir_;
  std::filesystem::path pem_dir_;
  std::filesystem::path user_agent_list_;
  std::unordered_map<URL, std::chrono::milliseconds> rate_limit_ms_;
};
