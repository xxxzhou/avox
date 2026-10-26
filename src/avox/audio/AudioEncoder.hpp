#pragma once

#include "../module/Observer.hpp"
#include "../muxer/AVEncoder.hpp"
#include "AudioEncodeParams.hpp"
#include "AudioFrame.hpp"

#ifdef AVOX_ENABLE_FFMPEG
#include "avox_ffmpeg/FFResample.hpp"
#endif

namespace avox {

// 音频编码器
// 基类主要做二件事
// 一是把输入音频重采样成编码器需要的格式
class AudioEncoder : public AVEncoder, public Observer<IEncoderOb> {
public:
  AudioEncoder();
  virtual ~AudioEncoder() = default;

protected:
  // 输入音频格式
  ATrackDesc desc = {};
  AudioDesc outDesc = {};
  // 输出音频格式,比如ffmpeg里的aac编码只支持fltp
  ATrackDesc enDesc = {};
  // 编码应该需要特定的大小
  AudioFrame curFrame = {};
  // 重采样,如果编码器需要特定格式,需要重采样
#ifdef AVOX_ENABLE_FFMPEG
  std::unique_ptr<FFResample> resample = nullptr;
#endif
  // 输出与输入的倍率
  double speed = 1.0;
  int64_t lastPts = 0;
  // 音频编码旋钮(未设置项保持编码器现状缺省)
  AudioEncodeParam encodeParam = {};

public:
  // 设置想要的输出格式
  void setOutDesc(const AudioDesc& desc);
  // 音频编码旋钮下发(非虚: 只搬字段)
  void setEncodeParam(const AudioEncodeParam& param) { encodeParam = param; }
  // 已解析码率(bps): 0 = 不设(编码器现状缺省)
  int32_t getTargetBitrate() const;
  // VBR 质量 1..5; -1 = 关
  int32_t getVbrQuality() const { return encodeParam.vbrQuality; }
  // 已解析 profile(inherit → LC)
  AudioEncodeProfile getProfileLevel() const {
    return resolveAudioProfile(encodeParam.profile);
  }
  virtual ATrackDesc setDesc(const ATrackDesc &desc);
  DecodeResult fillFrame(const AvoxAFrame &frame);
  virtual DecodeResult encode(const AvoxAFrame &frame) {
    return DecodeResult::noSupport;
  };
  // 编码要求特定frame大小，在decode填充curFrame满后调用
  virtual DecodeResult encode() = 0;

protected:
  virtual AudioDesc getSupportDesc(const AudioDesc &desc);
};

}
