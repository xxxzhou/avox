#include "Muxer.hpp"

#include <TargetConditionals.h>

#include "../AvoxMuxer.h"
#include "RawMuxer.hpp"

namespace avox {

const char* getRecorderStateStr(RecorderState state) {
  switch (state) {
#define XX(name, value, str) \
  case RecorderState::name:  \
    return str;
    AVOX_MAP_RECORDER_STATE(XX)
#undef XX
    default:
      return "invalid";
  }
}

const char* getMuxerTypeStr(MuxerType type) {
  switch (type) {
#define XX(name, value, str) \
  case MuxerType::name:      \
    return str;
    AVOX_MAP_MUXER_TYPE(XX)
#undef XX
    default:
      return "invalid";
  }
}

void IMuxerContext::setRawMuxer(MediaMuxer* muxer_) { muxer = muxer_; }

MediaMuxer* IMuxerContext::getRawMuxer() { return muxer; }

const char* getDefaultEncoderName(VCodecId codecId, bool bHard) {
  if (codecId == VCodecId::h264) {
// 注意不同编码器，sps/vps解码有些可以，有些不行
#ifdef __ANDROID__
    // AVOX_ANDROID_H264_ENCODER h264
    return bHard ? AVOX_ANDROID_H264_ENCODER : AVOX_FF_H264_ENCODER;
#elif __APPLE__
#if TARGET_OS_OSX
    // macOS 统一走 FFmpeg VideoToolbox: 原生 IOSVEncoder 实测 30fps 输入只出
    // 20fps(丢帧), 且 quality 档位与统一档不一致; 原生车道留给 iOS。
    return AVOX_FF_H264_ENCODER;
#else
    return bHard ? AVOX_IOS_H264_ENCODER : AVOX_FF_H264_ENCODER;
#endif
#elif defined(__ONLY_LINUX__)
    // Linux 硬编走 VAAPI(FFVaapiEncoder), 软编兜底名由渠道注入
    //(Linux 白名单无视频软编, 兜底未命中时 openFailed 明确报错)
    return bHard ? AVOX_FFVAAPI_H264_ENCODER : AVOX_FF_H264_ENCODER;
#else
    // h264_qsv h264_vulkan h264 libx264
    return bHard ? AVOX_FFDX11_H264_ENCODER : AVOX_FF_H264_ENCODER;
#endif
  } else if (codecId == VCodecId::h265) {
    // hevc_qsv hevc_vulkan hevc libx265
#ifdef __ANDROID__
    // AVOX_ANDROID_H264_ENCODER hevc
    return bHard ? AVOX_ANDROID_H265_ENCODER : AVOX_FF_H265_ENCODER;
#elif __APPLE__
#if TARGET_OS_OSX
    return AVOX_FF_H265_ENCODER;
#else
    return bHard ? AVOX_IOS_H265_ENCODER : AVOX_FF_H265_ENCODER;
#endif
#elif defined(__ONLY_LINUX__)
    // Linux 硬编走 VAAPI(同 h264)
    return bHard ? AVOX_FFVAAPI_H265_ENCODER : AVOX_FF_H265_ENCODER;
#else
    // hevc_qsv hevc_vulkan hevc libx265 AVOX_FFVULKAN_H265_ENCODER
    // AVOX_FFDX11_H265_ENCODER
    return bHard ? AVOX_FFDX11_H265_ENCODER : AVOX_FF_H265_ENCODER;
#endif
  }
  return AVOX_FF_H264_ENCODER;
}

}