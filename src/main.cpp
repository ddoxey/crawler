#include <iostream>
#include <future>
#include <unordered_set>

#include "CacheManager.hpp"
#include "Config.hpp"
#include "Crawler.hpp"
#include "Logger.hpp"
#include "LuaProcessor.hpp"
#include "URLManager.hpp"

int main(int argc, char* argv[]) {
  // Build an allow-list from any command-line args, all lower-cased
  std::unordered_set<URL> allowed;
  for (int i = 1; i < argc; ++i) {
    std::string s = argv[i];
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    allowed.insert(URL(s));
  }
  if (allowed.empty()) {
    logr::info << "Crawler starting for all configured domains ...";
  } else {
    logr::info << "Crawling only these domains:";
    for (auto const& d : allowed) {
      logr::info << "  - " << d;
    }
  }

  Config conf;

  logr::info << " cache dir: " << conf.GetCacheDir();
  logr::info << "  data dir: " << conf.GetDataDir();
  logr::info << "plugin dir: " << conf.GetPluginsDir();
  logr::info << "script dir: " << conf.GetScriptDir();

  CacheManager cache(conf.GetCacheDir(), conf.GetCacheAgeLimit());
  URLManager urlm(conf.GetDataDir());

  auto batches = urlm.GetBatchesByDomain();

  if (batches.size() == 0) {
    logr::warning << "No URLs configured in: " << conf.GetDataDir();
    return 1;
  }

  std::vector<std::future<void>> futures;
  futures.reserve(batches.size());

  for (auto& [domain, batch] : batches) {
    if (!allowed.empty() && !allowed.count(domain)) {
      continue;
    }

    futures.emplace_back(
      std::async(std::launch::async, [domain, batch, &cache, &conf]() mutable {
        logr::info << domain << " crawler running";
        LuaProcessor luap(conf.GetScriptDir(), domain);
        if (luap.HasScript()) {
          Crawler crawler(batch, cache, luap);
          crawler.Crawl();
        }
      }));
  }

  // block until all done
  for (auto& f : futures)
    f.get();

  return 0;
}
