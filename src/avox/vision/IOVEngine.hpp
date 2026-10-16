#pragma once

#include <string>

#include "avox/AvoxDef.h"

namespace avox {

// ============== OpenVINO 推理引擎 ==============
// 对应 IONNXSession, 但 OpenVINO 的 Core/CompiledModel/InferRequest 是有状态对象池,
// 抽象形状不同 (device + FP16/LATENCY + reshape), 故独立接口而非扩展 IONNXSession。
class IOVEngine {
 public:
  virtual ~IOVEngine() = default;

// 加载并编译模型。内部自动选最优 device:
// 试 GPU(Intel iGPU, FP16) → 失败试 CPU(Intel/AMD 通用, FP32) → 全失败返回 false。
  virtual bool loadModel(const std::string& modelPath,
                         int inferHeight, int inferWidth, int scale) = 0;

  // 同步推理。input/output 都是 NCHW float32:
  //   input 元素数 = 3*H*W; output 元素数 = 3*(H*scale)*(W*scale)。
  virtual bool infer(const float* input, float* output) = 0;

  // 实际选中的 device ("GPU"/"CPU"), 日志/调试用。
  virtual std::string device() const = 0;
};

}
