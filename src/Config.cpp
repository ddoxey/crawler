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
    //   "output_dir": "/var/lib/crawler/out",
    //   "rate_limit_ms": 500,
    //   "cache_age_limit_s": 86400
    // }
    //
    cache_dir_ = j.at("cache_dir").get<std::string>();
    data_dir_ = j.at("data_dir").get<std::string>();
    plugins_dir_ = j.at("plugins_dir").get<std::string>();
    script_dir_ = j.at("script_dir").get<std::string>();
    rate_limit_ms_ =
      std::chrono::milliseconds{j.value("rate_limit_ms", 1000LL)};
    cache_age_limit_s_ =
      std::chrono::seconds{j.value("cache_age_limit_s", 86400LL)};
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

std::chrono::milliseconds Config::GetRateLimit() const {
  return rate_limit_ms_;
}
