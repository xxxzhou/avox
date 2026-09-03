#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "avox/AvoxDef.h"
#include "avox/vision/IONNXSession.hpp"
#include "avox/vision/OnnxModel.hpp"

namespace avox {

// 共享 ONNX session 缓存: 按 (path, useGPU, deviceId) 缓存已加载 IONNXSession,
// 全局加载一次、对象复用。acquire 命中返回借用指针; 未命中则经 onnxSessionHub 新建 + loadModel。
// 模型随进程常驻 (无 unload 接口; AvoxManager heap-only、从不析构, 跨 DLL 析构不安全故不卸载)。
// 线程安全: 内部 mutex; Ort::Session::Run 自身并发安全, 共享 session 跨线程 run 安全。
// sherpa 不在此缓存 (走 sherpa-onnx C API, 不经 IONNXSession)。
class AVOX_EXPORT OnnxSessionCache {
 public:
  OnnxSessionCache() = default;
  ~OnnxSessionCache() = default;
  OnnxSessionCache(const OnnxSessionCache&) = delete;
  OnnxSessionCache& operator=(const OnnxSessionCache&) = delete;

 private:
  std::unordered_map<std::string, std::unique_ptr<IONNXSession>> sessions;
  std::mutex mtx;
  static std::string makeKey(const std::string& path, bool useGPU, int deviceId);

 public:
  // enum 入口 (静态路径模型: OCR/inpaint; translation 非 Android 亦可)
  IONNXSession* acquire(OnnxModel m, bool useGPU, int deviceId, int numThreads);
  // 字符串路径入口 (运行期路径, 如 Android translation 的 cache 拷贝目录)
  IONNXSession* acquire(const std::string& path, bool useGPU, int deviceId, int numThreads);
};

}
