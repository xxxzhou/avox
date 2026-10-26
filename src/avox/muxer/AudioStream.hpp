#pragma once

#include "../audio/AudioEncoder.hpp"
#include "../codec/AudioEncoderChain.hpp"

namespace avox {

class AudioStream : public IEncoderOb,
                    public IMuxerContext,
                    private AudioEncoderChain::Host {
public:
  AudioStream();
  virtual ~AudioStream();

protected:
  ATrackDesc desc = {};
  // 编码器对音频格式有要求，需要转换
  ATrackDesc outDesc = {};
  // 上层想要的输出格式(setAudioDesc 入参, 供车道接线协商用)
  AudioDesc wantDesc = {};
  // 音频编码旋钮(open 前设置; 未设置项保持编码器现状缺省)
  AudioEncodeParam encodeParam = {};
  AudioEncoderChain encoderChain;
  // 编码器打不开只报一次(音频不走换道: 封装侧轨描述已按本编码器协商结果定下)
  bool bWarnedEncOpenFail = false;

  // 候选链宿主: 建实例后设输出格式并取回协商结果
  virtual bool laneInit(AudioEncoder &impl, const ACodecDesc &) override;
  virtual void laneClose(AudioEncoder &impl) override;
  virtual void laneSaveConfigs(AudioEncoder &oldImpl) override;
  virtual void laneChanged(AudioEncoder &newImpl, bool bFirst) override;
  AudioEncoder *encoder() { return encoderChain.impl(); }

public:
  ATrackDesc setAudioDesc(const ATrackDesc &desc,const AudioDesc& outDesc);
  // 音频编码旋钮(码率/VBR/profile; 声道与采样率走 setAudioDesc)
  void setEncodeParam(const AudioEncodeParam& param) { encodeParam = param; }
  void encoderFrame(const AvoxAFrame &frame);
  void flushEncoder() { if (encoder()) encoder()->flush(); }
  // 停编码器(同 VideoStream::stopEncoder): 排空 → 析构 → 断 muxer 链
  void stopEncoder();

  // IEncoderOb
public:
  virtual void onPacket(AvoxPacket &packet) override;
};

}
