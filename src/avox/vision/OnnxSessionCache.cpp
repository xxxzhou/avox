#include "OnnxSessionCache.hpp"

#include "avox/module/AvoxManager.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

std::string OnnxSessionCache::makeKey(const std::string& path, bool useGPU, int deviceId) {
  // key 形如 "path|useGPU|deviceId"; path 自身不含 '|'
  return path + "|" + (useGPU ? "1" : "0") + "|" + std::to_string(deviceId);
}

IONNXSession* OnnxSessionCache::acquire(OnnxModel m, bool useGPU, int deviceId, int numThreads) {
  return acquire(onnxModelPath(m), useGPU, deviceId, numThreads);
}

IONNXSession* OnnxSessionCache::acquire(const std::string& path, bool useGPU, int deviceId, int numThreads) {
  if (path.empty()) return nullptr;
  std::string key = makeKey(path, useGPU, deviceId);
  std::lock_guard<std::mutex> lk(mtx);
  auto it = sessions.find(key);
  if (it != sessions.end()) return it->second.get();
  // 未命中: 经 onnxSessionHub 工厂新建 (avox_onnx plugin 注册 "onnx") + loadModel
  IONNXSession* s = AvoxManager::Get().onnxSessionHub.create("onnx");
  if (!s) {
    LOGFLF(LogLevel::error, "[OnnxSessionCache] onnxSessionHub 不可用 (avox_onnx 未加载?): ", path);
    return nullptr;
  }
  if (!s->loadModel(path, useGPU, deviceId, numThreads)) {
    LOGFLF(LogLevel::error, "[OnnxSessionCache] loadModel 失败: ", path);
    delete s;
    return nullptr;
  }
  sessions.emplace(key, std::unique_ptr<IONNXSession>(s));
  return s;
}

}
