#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "URL.hpp"

class LuaProcessor {
 public:
  explicit LuaProcessor(const std::filesystem::path& scripts_dir,
                        const URL& domain);

  /// Human‐readable status
  std::string GetStatus() const;

  /// The number of domains
  size_t Size() const;

  /// Is domain present
  bool HasScript() const;

  /// Run all the preloaded `process` functions for this URL's domain.
  /// Returns a vector of result‐tables (one per script).
  std::optional<nlohmann::json> Process(const URL& url,
                                        const std::string& content) const;

 private:
  void InitLua();  // opens libs
  std::optional<std::filesystem::path> FindScript() const;
  bool LoadScript();  // initializes env_ and funcs_
  static nlohmann::json LuaTableToJson(const sol::table& obj);
  static nlohmann::json LuaTableToJson(const sol::object& obj);

  std::filesystem::path scripts_dir_;

  URL domain_;
  sol::state lua_;
  sol::environment env_;
  sol::protected_function func_;
  bool debug_{false};
};
