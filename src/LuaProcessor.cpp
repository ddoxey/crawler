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
  lua_.open_libraries(
    sol::lib::base,
    sol::lib::package,
    sol::lib::string,
    sol::lib::table,
    sol::lib::debug,
    sol::lib::os
  );
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
    std::cerr << "[LuaProcessor] Loading "
              << init_script->string() << "\n";
  }

  // sandboxed environment inheriting globals (including common modules)
  sol::environment env(lua_, sol::create, lua_.globals());
  lua_.script_file(init_script->string(), env);

  // grab the per-domain process() entrypoint
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

std::optional<sol::table> LuaProcessor::Process(
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

  if (debug_)
    DumpResult(result);

  return {result};
}

void LuaProcessor::DumpResult(const sol::table& tbl) {
  std::cout << "Dumping Lua table:\n";
  for (auto& kv : tbl) {
    sol::object key = kv.first;
    sol::object val = kv.second;

    // --- key to string ---
    std::string keyStr;
    switch (key.get_type()) {
      case sol::type::string:
        keyStr = key.as<std::string>();
        break;
      case sol::type::number:
        keyStr = std::to_string(key.as<double>());
        break;
      default:
        keyStr =
          "<type " + std::to_string(static_cast<int>(key.get_type())) + ">";
    }

    std::cout << "  [" << keyStr << "] = ";

    // --- value to string/numeric/bool/nil ---
    switch (val.get_type()) {
      case sol::type::string:
        std::cout << '"' << val.as<std::string>() << '"';
        break;
      case sol::type::number:
        std::cout << val.as<double>();
        break;
      case sol::type::boolean:
        std::cout << std::boolalpha << val.as<bool>();
        break;
      case sol::type::nil:
        std::cout << "nil";
        break;
      default:
        // fallback to printing the raw enum
        std::cout << "<type " << static_cast<int>(val.get_type()) << ">";
    }
    std::cout << "\n";
  }
}
