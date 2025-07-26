#include <iostream>
#include <future>
#include <unordered_set>

#include "Config.hpp"
#include "CacheManager.hpp"
#include "LuaProcessor.hpp"
#include "URLManager.hpp"
#include "Crawler.hpp"

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
    std::cout << "Crawler starting for all configured domains ..." << std::endl;
  } else {
    std::cout << "Crawling only these domains:\n";
    for (auto const& d : allowed) {
      std::cout << "  - " << d << "\n";
    }
  }

  Config conf;

  std::cout << " cache dir: " << conf.GetCacheDir() << std::endl;
  std::cout << "  data dir: " << conf.GetDataDir() << std::endl;
  std::cout << "plugin dir: " << conf.GetPluginsDir() << std::endl;
  std::cout << "script dir: " << conf.GetScriptDir() << std::endl;

  CacheManager cache(conf.GetCacheDir(), conf.GetCacheAgeLimit());
  URLManager urlm(conf.GetDataDir());

  auto batches = urlm.GetBatchesByDomain();

  if (batches.size() == 0) {
    std::cerr << "No URLs configured in: " << conf.GetDataDir() << std::endl;
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
        std::cout << domain << " crawler running\n";
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
