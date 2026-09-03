#pragma once

#include <memory>
#include <string>
#include <vector>

#include "avox/AvoxDef.h"
#include "avox/module/AvoxManager.hpp"
#include "avox/vision/IONNXSession.hpp"
#include "avox/vision/OnnxModel.hpp"
#include "avox/vision/OnnxSessionCache.hpp"

namespace avox {

// ONNX 模型消费者基类: 统一"类加载、对象复用"。子类 (TextRecognizer/YOLODetector/OnnxTranslator/...)
// 继承它, 经 session() 取 session, 不再自己 onnxSessionHub.create + loadModel。
// - Shared (默认): 经 OnnxSessionCache 全局复用, 不 unload 则常驻; 退出随进程泄漏。
// - Private: 本对象独占 (不入全局 cache), 析构由 unique_ptr 自动释放 (运行期安全) —— 等价旧行为,
//   用于隔离 numThreads/GPU 配置或真并行推理 (现状无人需要)。
// 两种模式都返回借用裸指针, 调用方不要 delete。
class OnnxModelUser {
 public:
  enum class Mode { Shared, Private };

  OnnxModelUser() = default;
  virtual ~OnnxModelUser() = default;  // privateOwned 由 unique_ptr 自动释放 (运行期安全)

 private:
  std::vector<std::unique_ptr<IONNXSession>> privateOwned;  // 仅 Private 用; Shared 恒空
  // Private: 经 hub 新建 + loadModel, 存入 privateOwned (本对象持有)
  IONNXSession* acquirePrivate(const std::string& path, bool useGPU, int deviceId, int numThreads) {
    IONNXSession* s = AvoxManager::Get().onnxSessionHub.create("onnx");
    if (!s) {
      lastModelError = "onnxSessionHub 不可用 (avox_onnx 未加载?)";
      return nullptr;
    }
    if (!s->loadModel(path, useGPU, deviceId, numThreads)) {
      lastModelError = "loadModel 失败: " + path;
      delete s;
      return nullptr;
    }
    privateOwned.emplace_back(s);
    return s;
  }

 protected:
  std::string lastModelError;  // Private 模式 acquire 失败时记原因

 public:
  // enum 入口 (静态路径模型); Mode 留末位默认 Shared, 调用方只传 (model, useGPU, ...)
  IONNXSession* session(OnnxModel m, bool useGPU = false, int deviceId = 0,
                        int numThreads = 4, Mode mode = Mode::Shared) {
    if (mode == Mode::Shared) {
      return AvoxManager::Get().onnxSessionCache.acquire(m, useGPU, deviceId, numThreads);
    }
    return acquirePrivate(onnxModelPath(m), useGPU, deviceId, numThreads);
  }
  // 字符串路径入口 (运行期路径, 如 Android translation 的 cache 拷贝目录)
  IONNXSession* session(const std::string& path, bool useGPU = false, int deviceId = 0,
                        int numThreads = 4, Mode mode = Mode::Shared) {
    if (mode == Mode::Shared) {
      return AvoxManager::Get().onnxSessionCache.acquire(path, useGPU, deviceId, numThreads);
    }
    return acquirePrivate(path, useGPU, deviceId, numThreads);
  }
};

}
