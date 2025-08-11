#include "URL.hpp"
#include "Logger.hpp"
#include "URLManager.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

URLManager::URLManager(const std::filesystem::path& dir) : dir_{dir} {
  if (!std::filesystem::exists(dir_)) {
    throw std::runtime_error("URLManager: directory does not exist: " +
                             dir_.string());
  }
  if (!std::filesystem::is_directory(dir_)) {
    throw std::runtime_error("URLManager: not a directory: " + dir_.string());
  }
  logr::info << "DIR: " << dir_;

  for (auto const& entry : std::filesystem::directory_iterator(dir_)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    logr::info << "FILE: " << entry;
    try {
      LoadFromFile(entry.path());
    } catch (const std::exception& ex) {
      logr::warning << "Warning: URLManager failed to load \""
                    << entry.path().string() << "\": " << ex.what();
    }
  }
}

void URLManager::LoadFromFile(const std::filesystem::path& filename) {
  std::ifstream infile(filename);
  std::string line;
  while (std::getline(infile, line)) {
    if (!line.empty()) {
      URL url{line};
      if (url.IsValid()) {
        urls_.push_back(std::move(url));
      }
    }
  }
}

const std::vector<URL>& URLManager::GetURLs() const {
  return urls_;
}

std::unordered_map<URL, std::set<URL>> URLManager::GetBatchesByDomain() const {
  std::unordered_map<URL, std::set<URL>> batches;
  for (const auto& url : urls_) {
    auto domain = url.GetDomain();
    batches[domain].insert(url);
  }
  return batches;
}

void URLManager::Store(const URL& domain,
                       const std::unordered_set<URL>& urls) const {
  if (urls.empty())
    return;  // append nothing; never remove

  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);  // best-effort

  std::filesystem::path filename = dir_ / domain.GetSha256();
  filename += ".list";

  // Build batch (deterministic order, newline-sanitized)
  std::vector<std::string> lines;
  lines.reserve(urls.size());
  for (const auto& u : urls) {
    std::string s = u.ToString();
    // sanitize: guard against embedded newlines
    s.erase(
      std::remove_if(s.begin(), s.end(),
                     [](unsigned char c) { return c == '\r' || c == '\n'; }),
      s.end());
    if (!s.empty())
      lines.emplace_back(std::move(s));
  }
  if (lines.empty())
    return;
  std::sort(lines.begin(), lines.end());
  lines.erase(std::unique(lines.begin(), lines.end()), lines.end());

  // Check if we need to prepend a newline (to avoid sticking to last line)
  bool need_leading_nl = false;
  if (std::filesystem::exists(filename, ec)) {
    auto sz = std::filesystem::file_size(filename, ec);
    if (!ec && sz > 0) {
      std::ifstream fin(filename, std::ios::binary);
      if (fin) {
        fin.seekg(-1, std::ios::end);
        char last = '\n';
        fin.read(&last, 1);
        need_leading_nl = (fin && last != '\n');
      }
    }
  }

  // Build one contiguous buffer (reduces interleaving risk under concurrent
  // appends)
  std::string blob;
  blob.reserve((need_leading_nl ? 1 : 0) + lines.size() * 64);
  if (need_leading_nl)
    blob.push_back('\n');
  for (const auto& line : lines) {
    blob.append(line);
    blob.push_back('\n');
  }

  // Append
  std::ofstream out(filename, std::ios::binary | std::ios::app);
  if (!out)
    return;  // optionally log
  out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
  out.flush();
}
