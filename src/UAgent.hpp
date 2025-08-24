#pragma once
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <vector>

class UAgent {
 public:
  // list_path: text file with one User-UAgent per line (blank lines and lines
  // starting with '#' or ';' are ignored). Throws std::runtime_error on error.
  explicit UAgent(const std::filesystem::path& list_path);

  // Raw pointer to randomly selected string
  const char* c_str() const;

  // Useful for tests/metrics
  std::size_t Size() const noexcept {
    return uas_.size();
  }

 private:
  static std::string TrimCopy(std::string s);

  std::vector<std::string> uas_;
  mutable std::mt19937 rng_;
};
