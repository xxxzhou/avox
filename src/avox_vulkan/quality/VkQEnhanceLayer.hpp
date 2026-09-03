#pragma once

#include "../layer/VkLayer.hpp"
#include "avox/AvoxLayer.h"
#include "avox/vision/IONNXSession.hpp"
#include "avox/vision/IOVEngine.hpp"
#include "avox/module/RunTask.hpp"
#include <memory>
#include <mutex>
#include <condition_variable>

namespace avox {

class VkCommand;

// Real-ESRGAN 画质增强层
// 数据流:
//   主管线 onCommand: preprocess shader (inTexs RGBA → resize+RGBA→NCHW → SSBO_A)
//   主线程 onFrame:   map SSBO_A → 提交给推理线程
//   RunTask 线程:     ONNX(SSBO_A) → 写 SSBO_B NCHW float32
//   主线程 onFrame:   检查推理完成 → postprocess shader (SSBO_B → outTexs RGBA)
// 推理分辨率 = dstWidth/scale × dstHeight/scale (如 Upscale2x 2560×1440 → 推理 640×360)
class VkQEnhanceLayer : public VkLayer,
                              public IParamet<QualityEnhanceParamet>,
                              public RunTask {
  AVOX_LAYER_GETNAME(VkQEnhanceLayer)

 private:
  // 推理后端 (运行期降级: OV GPU → OV CPU → ORT CPU)
  IONNXSession* onnxSession = nullptr;          // ORT CPU 兜底 (OnnxSessionCache 借用, 不 delete)
  std::unique_ptr<IOVEngine> ovEngine;          // OpenVINO (可选, plugin 在才有; GPU/CPU 自动)
  bool useOpenVino = false;                     // 运行期标记: true=OpenVINO, false=ORT
  std::string inputName, outputName;            // ORT 路径用
  int32_t scale = 4;
  bool modelLoaded = false;

  // ── GPU buffers ──
  std::unique_ptr<VkWrapBuffer> preprocessBuffer = nullptr;
  std::unique_ptr<VkWrapBuffer> postprocessBuffer = nullptr;
  // 后处理独立 command + pipeline
  std::unique_ptr<VkCommand> postprocessCmd = nullptr;
  VkPipeline postprocessPipeline = VK_NULL_HANDLE;
  std::unique_ptr<UBOLayout> postprocessLayout = nullptr;
  VkPipelineShaderStageCreateInfo postprocessStageInfo = {};

  // 帧计数
  int32_t frameCount = 0;
  // 输出分辨率
  int32_t dstWidth = 0, dstHeight = 0;
  // 推理分辨率 (preprocess shader 的 dispatch 尺寸和 SSBO 大小)
  int32_t inferWidth = 0, inferHeight = 0;

  // ── 异步推理 (RunTask 线程) ──
  std::mutex inferMutex;
  std::condition_variable inferCv;
  std::vector<float> inferInput;
  bool inferInputReady = false;
  bool inferOutputReady = false;
  bool hasPostprocessResult = false;

 public:
  VkQEnhanceLayer();
  virtual ~VkQEnhanceLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override { return true; }
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
  virtual void onInitVkBuffer() override;
  virtual void onCommand() override;
  virtual bool onFrame() override;
  virtual void onUpdateParamet() override;
  virtual void onInitPipe() override;
  virtual void onRunTask() override;

 private:
  void calcOutputSize();
  bool loadModel();
  void initPostprocessPipe();
  void submitInfer(std::vector<float>&& input);
  bool pollInferResult();
  void runPostprocess();
};
}
