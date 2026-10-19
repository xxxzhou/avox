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
  virtual void drain() override;
  virtual void onClose() override;
  virtual DecodeResult encode(const GpuFrame& frame) override;
  virtual DecodeResult encode(const YUVFrame& frame) override;

 private:
  DecodeResult encode();
  // 建编码器+格式并 configure/start; tier 见 onPreEncoder 的分级说明
  DecodeResult openCodec(const char* mimeType, int32_t tier);
  // 按 tier 下发 profile/码控模式/画质档
  void applyFormatTier(int32_t tier);
  void releaseCodec();
};

}