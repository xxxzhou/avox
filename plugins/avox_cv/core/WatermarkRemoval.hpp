#pragma once

#include "InpaintConfig.hpp"
#include "InpaintTypes.hpp"
#include "avox/AvoxVision.h"
#include <memory>

namespace avox {

// ============== 水印去除主类 (实现 IWatermarkRemoval) ==============

class WatermarkRemoval : public IWatermarkRemoval {
 public:
  WatermarkRemoval();
  ~WatermarkRemoval() override;

  // === IWatermarkRemoval 接口实现 ===

  void setModelLevel(ModelLevel level) override;
  void setInpaintMode(InpaintMode mode) override;
  void setMaskDilate(int dilatePixels) override;
  void setDetectThreshold(float threshold) override;
  void setUseGPU(bool useGPU) override;

  bool open() override;
  void close() override;
  bool ready() override;

  bool process(IImageBuffer* input, IImageBuffer* output) override;
  bool detect(IImageBuffer* input, IImageBuffer* maskOutput) override;
  bool inpaint(IImageBuffer* input, IImageBuffer* mask, IImageBuffer* output) override;

  int getWatermarkCount() override;
  bool getWatermarkBBox(int index, float* x, float* y, float* w, float* h) override;
  float getDetectTimeMs() override;
  float getInpaintTimeMs() override;

  // === 扩展接口 ===

  void loadModel() { open(); }
  void setDetectMode(DetectMode mode);
  void setConfig(const InpaintConfig& config);

  void beginVideo(int width, int height);
  InpaintResult processVideoFrame(IImageBuffer* input, IImageBuffer* output);
  void endVideo();

  std::vector<WatermarkSeg> detectWithMask(IImageBuffer* input);
  InpaintMask getSegMask() const;
  const InpaintResult& getLastResult() const { return lastResult; }

 private:
  class Impl;
  std::unique_ptr<Impl> impl;
  InpaintResult lastResult;
};

}
