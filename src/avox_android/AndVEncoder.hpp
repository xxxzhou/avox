#pragma once

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include "avox/video/VideoEncoder.hpp"
#include "avox_egl/GLESContext.hpp"
#include "JniSurfaceTexture.hpp"
#include "avox_egl/EglVideoYuv.hpp"

namespace avox {

class AndVEncoder : public VideoEncoder{
 public:
  AndVEncoder();
  virtual ~AndVEncoder();

 private:
  AMediaCodec* mediaCodec = nullptr;
  AMediaFormat* mediaFormat = nullptr;
  // 提供一个供编码器使用的surface
  std::unique_ptr<EglVideoYuv> eglVideoYuv = nullptr;
  bool bGpu = false;
  GpuFrame tempFrame = {};

 public:
  virtual DecodeResult onPreEncoder() override;
  virtual void flush() override;
  virtual void onClose() override;
  virtual DecodeResult encode(const GpuFrame& frame) override;
  virtual DecodeResult encode(const YUVFrame& frame) override;

 private:
  DecodeResult encode();
};

}