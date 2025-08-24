#include "Config.hpp"

#include <cstdlib>            // for std::getenv
#include <fstream>            // for std::ifstream
#include <nlohmann/json.hpp>  // or whatever JSON library you use

using json = nlohmann::json;

Config::Config()
    : Config([&]() {
        // 1) Build the list of candidate directories
        std::vector<std::filesystem::path> dirs;
        if (const char* h = std::getenv("HOME")) {
          dirs.push_back(std::filesystem::path{h} / ".cache" / "crawler");
        }
        dirs.push_back(std::filesystem::current_path() / "crawler");
        dirs.push_back(std::filesystem::path{"/etc"} / "crawler");

        // 2) Find the first one that exists and contains conf.json
        for (auto const& dir : dirs) {
          if (std::filesystem::exists(dir) &&
              std::filesystem::exists(dir / "conf.json")) {
            return dir / "conf.json";
          }
        }

        return std::filesystem::path{};
      }()) {
}

Config::Config(const std::filesystem::path& config_file)
    : config_file_{config_file} {
  if (config_file_.empty() || !std::filesystem::exists(config_file_)) {
    throw std::runtime_error("crawler config.json not found");
  }

  // 4) Now load the JSON file:
  std::ifstream in{config_file_};
  if (!in.is_open()) {
    throw std::runtime_error("Failed to open " + config_file_.string());
  }

  try {
    json j;
    in >> j;

    // Example: suppose your conf.json looks like this:
    // {
    //   "cache_dir": "/home/user/.cache/crawler/cache",
    //   "plugins_dir": "/opt/crawler/plugins",
    //   "data_dir": "/var/lib/crawler/data",
    //   "user_agent_list": "/var/lib/crawler/user_agent.list",
    //   "output_dir": "/var/lib/crawler/out",
    //   "pem_dir": "/var/lib/crawler/pem",
    //   "rate_limit_ms": {
    //     "example.com": 500
    //   },
    //   "cache_age_limit_s": 86400
    // }
    //
    cache_dir_ = j.at("cache_dir").get<std::string>();
    data_dir_ = j.at("data_dir").get<std::string>();
    plugins_dir_ = j.at("plugins_dir").get<std::string>();
    script_dir_ = j.at("script_dir").get<std::string>();
    pem_dir_ = j.at("pem_dir").get<std::string>();
    user_agent_list_ = j.at("user_agent_list").get<std::string>();
    cache_age_limit_s_ =
      std::chrono::seconds{j.value("cache_age_limit_s", 86400LL)};

    rate_limit_ms_.clear();
    const auto& rl = j.at("rate_limit_ms");
    if (rl.is_object()) {
      for (const auto& [k, v] : rl.items()) {
        if (!v.is_number_integer())
          continue;  // skip bad entries
        long long ms = v.get<long long>();
        if (ms <= 0)
          continue;  // ignore nonsensical values

        std::string key = k;
        std::transform(key.begin(), key.end(), key.begin(),
                       ::tolower);  // normalize

        // Expect keys to already be registrable domains (eTLD+1)
        rate_limit_ms_.emplace(std::move(key), std::chrono::milliseconds{ms});
      }
    }
  } catch (const std::exception& ex) {
    throw std::runtime_error(std::string("Error parsing config.json: ") +
                             ex.what());
  }
}

std::filesystem::path Config::GetCacheDir() const {
  return cache_dir_;
}

std::chrono::seconds Config::GetCacheAgeLimit() const {
  return cache_age_limit_s_;
}

std::filesystem::path Config::GetDataDir() const {
  return data_dir_;
}

std::filesystem::path Config::GetPluginsDir() const {
  return plugins_dir_;
}

std::filesystem::path Config::GetScriptDir() const {
  return script_dir_;
}

std::filesystem::path Config::GetPemDir() const {
  return pem_dir_;
}

std::filesystem::path Config::GetUserUAgentList() const {
  return user_agent_list_;
}

const std::chrono::milliseconds Config::GetRateLimit(const URL& domain) const {
  if (rate_limit_ms_.count(domain) == 0)
    return kDefaultRateLimit;
  return rate_limit_ms_.at(domain);
}
