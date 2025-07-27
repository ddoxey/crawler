#include "LuaProcessor.hpp"
#include <iostream>
#include <sstream>

LuaProcessor::LuaProcessor(const std::filesystem::path& scripts_dir,
                           const URL& domain)
    : scripts_dir_{scripts_dir}, domain_{domain.GetDomain()} {
  if (const char* dbg = std::getenv("DEBUG")) {
    debug_ = (std::string(dbg) != "0");
  }
  InitLua();
  LoadScript();
}

void LuaProcessor::InitLua() {
  // only open what we need
  lua_.open_libraries(sol::lib::base, sol::lib::package, sol::lib::string,
                      sol::lib::table, sol::lib::debug, sol::lib::os);
  lua_["DEBUG"] = debug_;
}

std::optional<std::filesystem::path> LuaProcessor::FindScript() const {
  std::filesystem::path entry = scripts_dir_ / domain_.ToString() / "init.lua";
  if (std::filesystem::exists(entry)) {
    return {entry};
  }
  std::cerr << "[LuaProcessor] No such file: " << domain_.GetDomain()
            << "/init.lua\n";
  return std::nullopt;
}

bool LuaProcessor::LoadScript() {
  auto init_script = FindScript();
  if (!init_script.has_value())
    return false;

  if (debug_) {
    std::cerr << "[LuaProcessor] Loading " << *init_script << "\n";
  }

  sol::environment env(lua_, sol::create, lua_.globals());
  lua_.script_file(init_script->string(), env);

  sol::protected_function func = env["process"];
  if (!func.valid()) {
    std::cerr << "[LuaProcessor] " << *init_script << " defines no process()\n";
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
    std::cerr << "[LuaProcessor] No scripts for " << domain << "\n";
    return std::nullopt;
  }

  sol::protected_function_result result = func_(content, url.ToString());

  if (debug_) {
    std::cerr << "[LuaProcessor] func_.pointer() = " << func_.pointer() << "\n";

    std::cerr << "[LuaProcessor] → result.valid() = " << std::boolalpha
              << result.valid() << "  return_count = " << result.return_count()
              << "  get_type() = " << static_cast<int>(result.get_type())
              << "\n";
  }

  if (!result.valid()) {
    sol::error err = result;
    std::cerr << "[LuaProcessor] error: " << err.what() << "\n";
    return std::nullopt;
  }

  if (result.return_count() < 1) {
    std::cerr << "[LuaProcessor] 'process' return no results\n";
    return std::nullopt;
  }

  if (result.get_type() != sol::type::table) {
    std::cerr << "[LuaProcessor] 'process' did not return a table\n";
    if (debug_) {
      auto t = result.get_type();
      using UT = std::underlying_type_t<sol::type>;
      std::cerr << "Lua returned type: " << static_cast<UT>(t) << "\n";
    }
    return std::nullopt;
  }

  nlohmann::json j = LuaTableToJson(sol::table{result});

  if (debug_) {
    std::cerr << j.dump(2) << std::endl;  // pretty-print with 2-space indent
  }

  return {j};
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
  nlohmann::json j;

  if (is_array_like(tbl)) {
    for (std::size_t i = 1; i <= tbl.size(); ++i) {
      j.push_back(LuaTableToJson(sol::object(tbl[i])));
    }
  } else {
    for (auto& pair : tbl) {
      const sol::object& key = pair.first;
      const sol::object& val = pair.second;

      nlohmann::json key_json;
      if (key.is<std::string>()) {
        key_json = key.as<std::string>();
      } else if (key.is<int>()) {
        key_json = std::to_string(key.as<int>());
      } else {
        key_json = "<unsupported key>";
      }

      j[key_json] = LuaTableToJson(val);
    }
  }

  return j;
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
