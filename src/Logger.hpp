#pragma once

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace logr {

enum class Level { Debug = 0, Info, Warning, Error, None };

inline Level CurrentLevel() {
  static Level lvl = [] {
    Level base = Level::Info;
    if (auto* home = std::getenv("HOME")) {
      std::ifstream in{std::string(home) + "/.logging.json"};
      if (in) {
        try {
          auto j = nlohmann::json::parse(in);
          if (auto it = j.find("level"); it != j.end() && it->is_string()) {
            auto s = it->get<std::string>();
            if (s == "debug")
              base = Level::Debug;
            else if (s == "info")
              base = Level::Info;
            else if (s == "warning")
              base = Level::Warning;
            else if (s == "error")
              base = Level::Error;
          }
        } catch (...) {
        }
      }
    }
    if (auto* dbg = std::getenv("DEBUG")) {
      try {
        int d = std::stoi(dbg);
        if (d <= 1)
          base = Level::Debug;
        else if (d == 2)
          base = Level::Info;
        else if (d == 3)
          base = Level::Warning;
        else
          base = Level::Error;
      } catch (...) {
      }
    }
    return base;
  }();
  return lvl;
}

class NullBuffer : public std::streambuf {
  int overflow(int c) override {
    return c;
  }
};
inline NullBuffer nullBuffer;
inline std::ostream nullStream{&nullBuffer};

class Logger {
  Level lvl_;

 public:
  explicit Logger(Level L) : lvl_(L) {
  }

  template <typename T>
  std::ostream& operator<<(T const& v) const {
    return (lvl_ < CurrentLevel() ? nullStream : std::cerr) << v;
  }
  using Manip = std::ostream& (*)(std::ostream&);
  std::ostream& operator<<(Manip m) const {
    return (lvl_ < CurrentLevel() ? m(nullStream) : m(std::cerr));
  }
};

inline Logger debug{Level::Debug};
inline Logger info{Level::Info};
inline Logger warning{Level::Warning};
inline Logger error{Level::Error};

}  // namespace logr

// ─── macros to guard whole blocks
// ────────────────────────────────────────────── Usage:
//   IF_DEBUG {
//     logr::debug << "expensive: "
//                 << expensive_function()
//                 << std::endl;
//   }

#define IF_DEBUG if (logr::CurrentLevel() <= logr::Level::Debug)
#define IF_INFO if (logr::CurrentLevel() <= logr::Level::Info)
#define IF_WARNING if (logr::CurrentLevel() <= logr::Level::Warning)
#define IF_ERROR if (logr::CurrentLevel() <= logr::Level::Error)
