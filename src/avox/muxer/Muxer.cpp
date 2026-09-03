#include "Muxer.hpp"

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
    // h264_qsv h264_vulkan h264 libx264
    return bHard ? AVOX_IOS_H264_ENCODER : AVOX_FF_H264_ENCODER;
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
    // hevc_qsv hevc_vulkan hevc libx264
    return bHard ? AVOX_IOS_H265_ENCODER : AVOX_FF_H265_ENCODER;
#else
    // hevc_qsv hevc_vulkan hevc libx265 AVOX_FFVULKAN_H265_ENCODER
    // AVOX_FFDX11_H265_ENCODER
    return bHard ? AVOX_FFDX11_H265_ENCODER : AVOX_FF_H265_ENCODER;
#endif
  }
  return AVOX_FF_H264_ENCODER;
}

}