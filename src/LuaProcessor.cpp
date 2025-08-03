#include "LuaProcessor.hpp"
#include "Logger.hpp"

#include <iostream>
#include <sstream>

LuaProcessor::LuaProcessor(const std::filesystem::path& scripts_dir,
                           const URL& domain)
    : scripts_dir_{scripts_dir}, domain_{domain.GetDomain()} {
  InitLua();
  LoadScript();
}

void LuaProcessor::InitLua() {
  // only open what we need
  lua_.open_libraries(sol::lib::base, sol::lib::package, sol::lib::string,
                      sol::lib::table, sol::lib::debug, sol::lib::os);
  IF_DEBUG {
    lua_["DEBUG"] = true;
  }
}

std::optional<std::filesystem::path> LuaProcessor::FindScript() const {
  std::filesystem::path entry = scripts_dir_ / domain_.ToString() / "init.lua";
  if (std::filesystem::exists(entry)) {
    return {entry};
  }
  logr::debug << "[LuaProcessor] No such file: " << domain_.GetDomain()
              << "/init.lua";
  return std::nullopt;
}

bool LuaProcessor::LoadScript() {
  auto init_script = FindScript();
  if (!init_script.has_value())
    return false;

  logr::debug << "[LuaProcessor] Loading " << *init_script;

  sol::environment env(lua_, sol::create, lua_.globals());
  lua_.script_file(init_script->string(), env);

  sol::protected_function func = env["process"];
  if (!func.valid()) {
    logr::debug << "[LuaProcessor] " << *init_script << " defines no process()";
    return false;
  }

  env_ = std::move(env);
  func_ = std::move(func);
  return true;
}

bool LuaProcessor::HasScript() const {
  return func_.valid();
}

std::optional<nlohmann::json> LuaProcessor::Process(
  const URL& url, const std::string& content) const {
  const auto domain = url.GetDomain();

  if (domain != domain_) {
    logr::debug << "[LuaProcessor] No scripts for " << domain;
    return std::nullopt;
  }

  sol::protected_function_result result = func_(content, url.ToString());

  if (!result.valid()) {
    sol::error err = result;
    logr::warning << "[LuaProcessor] error: " << err.what();
    return std::nullopt;
  }

  if (result.return_count() < 1) {
    logr::warning << "[LuaProcessor] 'process' return no results";
    return std::nullopt;
  }

  if (result.get_type() != sol::type::table) {
    logr::warning << "[LuaProcessor] 'process' did not return a table";
    IF_WARNING {
      auto t = result.get_type();
      using UT = std::underlying_type_t<sol::type>;
      logr::warning << "Lua returned type: " << static_cast<UT>(t);
    }
    return std::nullopt;
  }

  nlohmann::json result_j = LuaTableToJson(sol::table{result});

  IF_DEBUG {
    logr::debug << result_j.dump(2);
  }

  return {result_j};
}

bool is_array_like(const sol::table& tbl) {
  std::size_t i = 1;
  for (auto& pair : tbl) {
    if (!pair.first.is<int>() || pair.first.as<int>() != static_cast<int>(i)) {
      return false;
    }
    ++i;
  }
  return true;
}

nlohmann::json LuaProcessor::LuaTableToJson(const sol::table& tbl) {
  nlohmann::json tbl_j;

  if (is_array_like(tbl)) {
    for (std::size_t i = 1; i <= tbl.size(); ++i) {
      tbl_j.push_back(LuaTableToJson(sol::object(tbl[i])));
    }
  } else {
    for (auto& pair : tbl) {
      const sol::object& key = pair.first;
      const sol::object& val = pair.second;

      nlohmann::json key_j;
      if (key.is<std::string>()) {
        key_j = key.as<std::string>();
      } else if (key.is<int>()) {
        key_j = std::to_string(key.as<int>());
      } else {
        key_j = "<unsupported key>";
      }

      tbl_j[key_j] = LuaTableToJson(val);
    }
  }

  return tbl_j;
}

nlohmann::json LuaProcessor::LuaTableToJson(const sol::object& obj) {
  switch (obj.get_type()) {
    case sol::type::nil:
      return nullptr;
    case sol::type::boolean:
      return obj.as<bool>();
    case sol::type::number:
      return obj.as<double>();
    case sol::type::string:
      return obj.as<std::string>();
    case sol::type::table:
      return LuaTableToJson(obj.as<sol::table>());
    default:
      return "<unsupported value>";
  }
}
