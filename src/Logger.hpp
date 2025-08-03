#pragma once

#include <cstdio>  // for fileno()
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unistd.h>  // for isatty()

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
        if (d == 1)
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

// returns true if a message at level `msg` should be suppressed
inline bool ShouldMute(Level msg) {
  auto lvl = CurrentLevel();
  switch (msg) {
    case Level::Debug:
      // only print when we’re in Debug mode
      return lvl != Level::Debug;

    case Level::Info:
      // print in Info or Debug modes
      return (lvl != Level::Debug && lvl != Level::Info);

    case Level::Warning:
    case Level::Error:
      // always print
      return false;

    default:
      return true;
  }
}

inline bool is_tty() {
  return ::isatty(::fileno(stderr)) != 0;
}

// ANSI escape sequences
static constexpr char const* RESET = "\033[0m";
static constexpr char const* CYAN = "\033[36m";
static constexpr char const* GREEN = "\033[32m";
static constexpr char const* YELLOW = "\033[33m";
static constexpr char const* RED = "\033[31m";

inline constexpr char const* colorCode(Level L) {
  switch (L) {
    case Level::Debug:
      return CYAN;
    case Level::Info:
      return GREEN;
    case Level::Warning:
      return YELLOW;
    case Level::Error:
      return RED;
    default:
      return RESET;
  }
}

// A tiny RAII proxy that does prefix in ctor, suffix in dtor,
// and funnels every << through itself.
class LogEntry {
 public:
  LogEntry(Level L) : lvl(L), muted(ShouldMute(L)), lock(log_mutex()) {
    if (!muted) {
      if (is_tty()) {
        std::cerr << colorCode(lvl);
      }
    }
  }

  ~LogEntry() {
    if (!muted) {
      if (is_tty()) {
        std::cerr << RESET;
      }
      std::cerr << std::endl;
    }
  }

  LogEntry(LogEntry&&) noexcept = default;
  LogEntry& operator=(LogEntry&&) noexcept = default;

  // (and to be explicit)
  LogEntry(const LogEntry&) = delete;
  LogEntry& operator=(const LogEntry&) = delete;

  // payload insertion
  template <typename T>
  LogEntry& operator<<(T const& v) {
    if (!muted) {
      std::cerr << v;
    }
    return *this;
  }

  LogEntry& operator<<(std::ostream& (*m)(std::ostream&)) {
    if (!muted) {
      m(std::cerr);
    }
    return *this;
  }

 private:
  Level lvl;
  bool muted;
  std::unique_lock<std::mutex> lock;

  // one mutex for all entries, to prevent interleaving
  static std::mutex& log_mutex() {
    static std::mutex m;
    return m;
  }
};

struct Logger {
  Level lvl;
  constexpr Logger(Level L) : lvl(L) {
  }

  // the magic: first << on Logger gives you a LogEntry
  template <typename T>
  LogEntry operator<<(T const& v) const {
    LogEntry e(lvl);
    e << v;
    return e;  // most compilers will apply NRVO here, but it's not guaranteed
  }

  LogEntry operator<<(std::ostream& (*m)(std::ostream&)) const {
    LogEntry e(lvl);
    e << m;
    return e;
  }
};

inline constexpr Logger debug{Level::Debug};
inline constexpr Logger info{Level::Info};
inline constexpr Logger warning{Level::Warning};
inline constexpr Logger error{Level::Error};
}  // namespace logr

// ─── macros to guard whole blocks
// ────────────────────────────────────────────── Usage:
//   IF_DEBUG {
//     logr::debug << "expensive: "
//                 << expensive_function();
//   }

#define IF_DEBUG if (logr::CurrentLevel() <= logr::Level::Debug)
#define IF_INFO if (logr::CurrentLevel() <= logr::Level::Info)
#define IF_WARNING if (logr::CurrentLevel() <= logr::Level::Warning)
#define IF_ERROR if (logr::CurrentLevel() <= logr::Level::Error)
