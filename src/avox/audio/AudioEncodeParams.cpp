#include "AudioEncodeParams.hpp"

namespace avox {

int32_t getAutoAudioBitrate(const AudioDesc& desc) {
  const int32_t channels = desc.channels > 0 ? desc.channels : 2;
  if (channels <= 1) {
    return 96000;
  }
  if (channels == 2) {
    return 160000;
  }
  if (channels <= 4) {
    return 256000;
  }
  if (channels <= 6) {
    return 384000;
  }
  return 512000;
}

int32_t resolveAudioBitrate(const AudioEncodeParam& param,
                            const AudioDesc& desc) {
  if (param.bitrateKbps == 0) {
    return 0;
  }
  if (param.bitrateKbps < 0) {
    return getAutoAudioBitrate(desc);
  }
  // 显式值钳到可用区间(8k..1024k bps 量级)
  int32_t bps = param.bitrateKbps * 1000;
  const int32_t kMinBitrate = 8000;
  const int32_t kMaxBitrate = 1024000;
  if (bps < kMinBitrate) {
    bps = kMinBitrate;
  } else if (bps > kMaxBitrate) {
    bps = kMaxBitrate;
  }
  return bps;
}

AudioEncodeProfile resolveAudioProfile(AudioEncodeProfile profile) {
  return profile == AudioEncodeProfile::inherit ? AudioEncodeProfile::lc
                                                : profile;
}

}
