#include "VideoEncoder.hpp"

namespace avox {

VideoEncoder::VideoEncoder() {}

VideoEncoder::~VideoEncoder() {}

void VideoEncoder::setDesc(const VTrackDesc& desc_) {
  desc = desc_;
  if (desc.desc.fps == 0) {
    desc.desc.fps = 30;
  }
  // 外部显式设过码率就不覆盖: 那是硬约束(WebRTC 上行), 不是缺省值
  if (bitrate == 0 && !bExplicitBitrate) {
    bitrate = getAutoBitrate();
  }
}

void VideoEncoder::setBitrate(int32_t bitrate_) {
  bitrate = bitrate_;
  bExplicitBitrate = true;
}

void VideoEncoder::setEncodeParam(const VideoEncodeParam& param) {
  encodeParam = param;
  if (encodeParam.gopSeconds > 0) {
    gop = encodeParam.gopSeconds;
  }
  // 码率选项是 kbps, 内部统一按 bps 走(与 setBitrate 同量纲)
  if (encodeParam.bitrateKbps > 0) {
    setBitrate(encodeParam.bitrateKbps * 1000);
  }
}

// 码率控制模式: 用户指定的优先; 未指定时录制取质量定档(体积/画质优先),
// 推流取 CBR(上行是硬约束); 录制下若显式给了码率, 说明用户要按码率收口
EncodeRateMode VideoEncoder::getRateMode() const {
  if (encodeParam.rateMode != EncodeRateMode::inherit) {
    return encodeParam.rateMode;
  }
  if (encodeParam.usage == EncodeUsage::stream) {
    return EncodeRateMode::cbr;
  }
  return bExplicitBitrate ? EncodeRateMode::vbr : EncodeRateMode::quality;
}

int32_t VideoEncoder::getQualityLevel() const {
  if (encodeParam.quality >= 0) {
    return encodeParam.quality;
  }
  return encodeParam.usage == EncodeUsage::stream ? kStreamQuality : kRecordQuality;
}

// 预设: 录制缺省取均衡档, 推流缺省取速度档(延迟优先)
EncodePreset VideoEncoder::getPreset() const {
  if (encodeParam.preset != EncodePreset::inherit) {
    return encodeParam.preset;
  }
  return encodeParam.usage == EncodeUsage::stream ? EncodePreset::speed
                                      : EncodePreset::balanced;
}

// profile: 录制缺省取 high(本机/自家播放器可解), 推流缺省取 compat —— 推流对端
// 按 SDP 的 profile-level-id 解, 编码器擅自发 High 的 NAL 会让对端解不了
EncodeProfile VideoEncoder::getProfileLevel() const {
  if (encodeParam.profile != EncodeProfile::inherit) {
    return encodeParam.profile;
  }
  return encodeParam.usage == EncodeUsage::stream ? EncodeProfile::compat
                                      : EncodeProfile::high;
}

// 自动码率: 实现单源在 VideoEncodeParams.cpp —— 原先编码侧(FFVEncoder 的
// w*h*fps*3)与封装侧(IOMuxerZM 的 w*h*fps*8/1000)各写一版, 同分辨率差 50 倍
int32_t VideoEncoder::getAutoBitrate() const {
  return getAutoVideoBitrate(desc.desc, desc.codecId);
}

}
