#include "UAgent.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

using namespace std;

static inline bool IsCommentOrEmpty(const string& s) {
  if (s.empty())
    return true;
  char c = s[0];
  return c == '#' || c == ';';
}

string UAgent::TrimCopy(string s) {
  auto issp = [](unsigned char c) { return std::isspace(c); };

  // strip CR for CRLF files
  if (!s.empty() && s.back() == '\r')
    s.pop_back();

  // left trim
  auto b = s.begin();
  while (b != s.end() && issp(static_cast<unsigned char>(*b)))
    ++b;

  // right trim
  auto e = s.end();
  while (e != b && issp(static_cast<unsigned char>(*(e - 1))))
    --e;

  return string(b, e);
}

UAgent::UAgent(const std::filesystem::path& list_path)
    : rng_([&] {
        std::random_device rd;
        return std::mt19937::result_type(rd());
      }()) {
  ifstream in(list_path);
  if (!in) {
    throw runtime_error("UAgent: failed to open UA list: " +
                        list_path.string());
  }

  uas_.reserve(64);
  string line;
  while (std::getline(in, line)) {
    string s = TrimCopy(std::move(line));
    if (s.empty() || IsCommentOrEmpty(s))
      continue;
    uas_.push_back(std::move(s));
  }

  if (uas_.empty()) {
    throw runtime_error("UAgent: no user-agent strings loaded from: " +
                        list_path.string());
  }
}

const char* UAgent::c_str() const {
  std::uniform_int_distribution<std::size_t> dist(0, uas_.size() - 1);
  const std::size_t i = dist(rng_);
  return uas_[i].c_str();
}
