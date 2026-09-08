#pragma once

#include "../Avox.hpp"
#include "../module/Observer.hpp"

namespace avox {

// FFmpeg 软编编码器名, 构建期可注入 (CMake AVOX_DIST_FLAVOR):
// agpl 渠道默认 libx264/libx265 (GPL); commercial(LGPL) 渠道 Windows 传 h264_mf/hevc_mf
#ifndef AVOX_FF_H264_ENCODER
#define AVOX_FF_H264_ENCODER "libx264"
#endif
#ifndef AVOX_FF_H265_ENCODER
#define AVOX_FF_H265_ENCODER "libx265"
#endif
#define AVOX_ANDROID_H264_ENCODER "android h264 decoder"
#define AVOX_ANDROID_H265_ENCODER "android h265 decoder"
#define AVOX_IOS_H264_ENCODER "ios h264 decoder"
#define AVOX_IOS_H265_ENCODER "ios h265 decoder"
#define AVOX_FFVULKAN_H264_ENCODER "ff_h264_vulkan"
#define AVOX_FFVULKAN_H265_ENCODER "ff_hevc_vulkan"
#define AVOX_FFDX11_H264_ENCODER "ff_h264_dx11"
#define AVOX_FFDX11_H265_ENCODER "ff_hevc_dx11"
#define AVOX_FFVAAPI_H264_ENCODER "ff_h264_vaapi"
#define AVOX_FFVAAPI_H265_ENCODER "ff_hevc_vaapi"

struct MuxerDesc {
  std::string name;
};

class IMuxerContext {
 public:
  IMuxerContext() {}
  virtual ~IMuxerContext() {}

 protected:
  class MediaMuxer* muxer = nullptr;

 public:
  void setRawMuxer(class MediaMuxer* muxer);
  class MediaMuxer* getRawMuxer();
};

class IMuxerOb {
 public:
  IMuxerOb() = default;
  virtual ~IMuxerOb() = default;

 public:
  virtual void onMuxerOpen(class MediaMuxer* muxer) = 0;
  virtual void onMuxerClose() = 0;
};

// 返回视频默认的编码器名称
AVOX_EXPORT const char* getDefaultEncoderName(VCodecId codecId, bool bHard);

}