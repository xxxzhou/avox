#pragma once

#include "../module/Observer.hpp"
#include "../muxer/AVEncoder.hpp"
#include "ImageBuffer.hpp"
#include "VideoRender.hpp"

namespace avox {

class AVOX_EXPORT VideoEncoder : public AVEncoder, public Observer<IEncoderOb> {
 public:
  VideoEncoder();
  virtual ~VideoEncoder();

 protected:
  VTrackDesc desc = {};
  int32_t gop = 3;
  // AvoxSurfaceType surface = nullptr;

 protected:
  // 自动计算bitrate
  int32_t getAutoBitrate();

 public:
  void setDesc(const VTrackDesc& desc);
  void setBitrate(int32_t bitrate);
  // virtual void onSurface() {};
  virtual DecodeResult encode(const YUVFrame& frame) {
    return DecodeResult::noSupport;
  };
  virtual DecodeResult encode(const GpuFrame& frame) {
    return DecodeResult::noSupport;
  };
};

}