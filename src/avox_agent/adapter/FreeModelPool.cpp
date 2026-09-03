#include "FreeModelPool.hpp"

#include <utility>

#include "avox/module/LogHelper.hpp"

// httplib 只在本编译单元出现 (与 HttplibTransport 同一约定: 把它收敛在少数几个 .cpp 里)。
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#else
#include "httplib.h"
#endif

namespace avox {

FreeModelPool::FreeModelPool(std::string baseUrlValue, std::string pathPrefixValue,
                            std::string apiKeyValue, ModelPoolSource sourceValue)
    : baseUrl(std::move(baseUrlValue)),
      pathPrefix(std::move(pathPrefixValue)),
      apiKey(std::move(apiKeyValue)),
      source(sourceValue) {}

bool FreeModelPool::stale() const { return models.stale(); }

size_t FreeModelPool::count() const { return models.count(); }

std::string FreeModelPool::pickNext() { return models.pickNext(); }

void FreeModelPool::markRateLimited(const std::string& model) {
  if (model.empty()) return;
  models.markRateLimited(model);
}

std::string FreeModelPool::rankedDebug() const { return models.rankedDebug(); }

void FreeModelPool::refresh() {
  httplib::Client client(baseUrl);
  client.set_connection_timeout(10);
  client.set_read_timeout(30);
  httplib::Headers headers;
  if (!apiKey.empty()) headers.emplace("Authorization", "Bearer " + apiKey);

  const std::string path = pathPrefix + "/models";
  auto response = client.Get(path.c_str(), headers);
  if (!response || response->status != 200) {
    // 拉取失败保持旧内容: 一个过期的池仍比空池有用 (旧模型多半还能用), 而 ModelRoutePolicy
    // 会在 count()==0 时走 fallback。
    LOGFLF(LogLevel::warn, "[free-models] GET ", path.c_str(),
           " 失败 status=", (response ? response->status : -1));
    return;
  }
  models.load(response->body, source);
  LOGFLF(LogLevel::info, "[free-models] 已加载 ",
         std::to_string(models.count()).c_str(), " 个免费模型");
}

}
