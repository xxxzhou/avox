#pragma once

#include <memory>

#include "avox/vision/IOVEngine.hpp"

namespace avox {

// ============== OpenVINO 实现 ==============
//
// IOVEngine 的 OpenVINO 实现。pImpl: openvino/openvino.hpp 只在 .cpp include,
// 避免污染调用方(核心 avox.dll)编译单元 — 与 ONNXSession 前向声明 Ort 类型同理。
class OVEngine : public IOVEngine {
 public:
  OVEngine();
  ~OVEngine() override;
  OVEngine(const OVEngine&) = delete;
  OVEngine& operator=(const OVEngine&) = delete;

  bool loadModel(const std::string& modelPath,
                 int inferHeight, int inferWidth, int scale) override;
  bool infer(const float* input, float* output) override;
  std::string device() const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  // 在 device 上编译模型 (reshape 推理分辨率 [1,3,H,W]); 成员函数可访问 impl_/Impl
  bool tryCompile(const std::string& modelPath, int H, int W, int scale,
                  const std::string& dev);
};

}
