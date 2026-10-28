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
  // 平面格式帧缓冲布局是 [ch][frameSamples]; 输入帧长度与帧长无整数关系,
  // 必须逐声道攒满, 按字节连续切片会让声道互相穿插
  bool bPlane = false;
  int32_t planeFilled = 0;
  // 编码时间轴: 按输出样本累计. 输入帧长(EAC3 1536)与编码帧长(AAC 1024)
  // 不整除, 逐块沿用输入 pts 会把时间戳抖成 21/32/11ms, 播放器按时间轴
  // 排帧就会周期性补/丢样本
  bool bClockAnchored = false;
  int64_t clockPts = 0;
  int64_t clockSamples = 0;
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
  // 平面格式逐声道攒满一帧(多声道必需); data/size 已是重采样后的数据
  DecodeResult fillFramePlanar(uint8_t* data, int32_t size, int64_t pts);
  // 下一帧时间戳: 首帧锚点 + 已编码样本数换算毫秒(累计计算, 无截断漂移)
  int64_t framePts() const;
  virtual DecodeResult encode(const AvoxAFrame &frame) {
    return DecodeResult::noSupport;
  };
  // 编码要求特定frame大小，在decode填充curFrame满后调用
  virtual DecodeResult encode() = 0;

protected:
  virtual AudioDesc getSupportDesc(const AudioDesc &desc);
};

}
