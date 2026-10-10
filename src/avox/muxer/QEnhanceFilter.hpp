#pragma once

#include <memory>

#include "RecordVideoFilter.hpp"

namespace avox {

class CpuQEnhancer;
class ImageBuffer;

// 离线画质增强滤镜(Real-ESRGAN, 原TranscodeRecorder内嵌逻辑外置):
// prepare装配rgba图输出(enableImage+假帧重建), capture把rgba图深拷入队,
// process在编码线程逐帧推理; ENH_NOIMG/ENH_NODUMMY/ENH_NOQUEUE调试门随迁于此
class QEnhanceFilter : public IRecordVideoFilter {
 public:
  explicit QEnhanceFilter(const QualityEnhanceParamet& paramet);
  virtual ~QEnhanceFilter();

 protected:
  const char* name() const override { return "quality enhance"; }
  bool accepts(bool bNoOutput, bool bVideoCopy) const override;
  bool prepare(const VideoDesc& srcDesc, bool bHardEncode,
               WindowRender* render, VideoDesc& outDesc) override;
  bool capture(int64_t pts, int64_t dts, VideoFramePtr& out) override;
  bool process(const VideoFramePtr& in, YUVFrame& out) override;

 private:
  QualityEnhanceParamet paramet = {};
  // prepare传入借用(自身接线与析构disableImage用); recorder的surfaceRender
  // 是其成员, 比本滤镜活得久
  WindowRender* render = nullptr;
  std::unique_ptr<CpuQEnhancer> enhancer;
  std::shared_ptr<ImageBuffer> rgbaBuffer;
};

}
