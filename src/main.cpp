#include <iostream>
#include <future>
#include <unordered_set>

#include "CacheManager.hpp"
#include "Config.hpp"
#include "Crawler.hpp"
#include "Gate.hpp"
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

  // concurrency cap (can make this a Config option later)
  const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
  Gate gate(hw);

  std::vector<std::future<void>> futures;
  futures.reserve(batches.size());

  for (auto& [domain, batch] : batches) {
    if (!allowed.empty() && !allowed.count(domain))
      continue;

    gate.acquire();  // throttle

    // move the batch into the task to avoid copying
    futures.emplace_back(
      std::async(std::launch::async, [dom = domain, bat = std::move(batch),
                                      &cache, &conf, &urlm, &gate]() mutable {
        // RAII release to ensure the permit is returned even on exceptions
        struct Release {
          Gate& g;
          ~Release() {
            g.release();
          }
        } _{gate};

        try {
          logr::info << "Crawler starting: " << dom;

          LuaProcessor luap(conf.GetScriptDir(), dom);
          if (!luap.HasScript()) {
            logr::warning << "No Lua script for " << dom;
            return;
          }

          Crawler crawler(bat, conf.GetRateLimit(dom), conf.GetUserUAgentList(),
                          cache, luap, urlm);
          crawler.Crawl();

          logr::info << "Crawler finished: " << dom;

        } catch (const std::exception& e) {
          logr::error << "Crawler for " << dom << " failed: " << e.what();
        } catch (...) {
          logr::error << "Crawler for " << dom << " failed with unknown error";
        }
      }));
  }

  // Wait for all crawlers
  for (auto& f : futures)
    f.get();

  return 0;
}
