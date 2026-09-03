#pragma once

#include "../module/Observer.hpp"
#include "../muxer/AVEncoder.hpp"
#include "AudioFrame.hpp"

#ifdef AVOX_ENABLE_FFMPEG
#include "avox_ffmpeg/FFResample.hpp"
#endif

namespace avox {

// 音频编码器
// 基类主要做二件事
// 一是把输入音频重采样成编码器需要的格式
// 二是把音频帧填充到编码器需要的固定帧大小上
// 子类具体实现编码器并回调转发
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

public:
  // 设置想要的输出格式
  void setOutDesc(const AudioDesc& desc);
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