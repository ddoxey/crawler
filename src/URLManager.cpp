#include "URL.hpp"
#include "Logger.hpp"
#include "URLManager.hpp"
#include <fstream>
#include <iostream>

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
