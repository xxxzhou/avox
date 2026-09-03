#pragma once

#include <string>

#include "avox/AvoxDef.h"

namespace avox {

// ============== OpenVINO 推理引擎 ==============
//
// 对应 IONNXSession, 但 OpenVINO 的 Core/CompiledModel/InferRequest 是有状态对象池,
// 抽象形状不同 (device + FP16/LATENCY + reshape), 故独立接口而非扩展 IONNXSession。
// 实现在 plugins/avox_openvino (OVEngine), 经 AvoxManager::openvinoEngineHub 取。
//
// VkQEnhanceLayer 优先用此 (Intel iGPU/CPU, 实测 UHD770 GPU FP16 ~131ms),
// plugin 没装/全失败时降级 IONNXSession (ORT CPU)。一个 binary 跨硬件最优。
class IOVEngine {
 public:
  virtual ~IOVEngine() = default;

  // 加载并编译模型。内部自动选最优 device:
  //   试 GPU(Intel iGPU, FP16) → 失败试 CPU(Intel/AMD 通用, FP32) → 全失败返回 false。
  // inferHeight/inferWidth: 推理输入分辨率 (NCHW [1,3,H,W]); scale: 输出 = 推理×scale。
  // modelPath: .onnx 路径 (OV read_model 直接吃 ONNX, 不转 IR)。
  virtual bool loadModel(const std::string& modelPath,
                         int inferHeight, int inferWidth, int scale) = 0;

  // 同步推理。input/output 都是 NCHW float32:
  //   input 元素数 = 3*H*W; output 元素数 = 3*(H*scale)*(W*scale)。
  virtual bool infer(const float* input, float* output) = 0;

  // 实际选中的 device ("GPU"/"CPU"), 日志/调试用。
  virtual std::string device() const = 0;
};

}
