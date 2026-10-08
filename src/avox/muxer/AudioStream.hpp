#pragma once

#include "../audio/AudioEncoder.hpp"

namespace avox {

class AudioStream : public IEncoderOb, public IMuxerContext {
public:
  AudioStream();
  virtual ~AudioStream();

protected:
  ATrackDesc desc = {};
  // 编码器对音频格式有要求，需要转换
  ATrackDesc outDesc = {};
  std::unique_ptr<AudioEncoder> encoder = nullptr;

public:
  ATrackDesc setAudioDesc(const ATrackDesc &desc,const AudioDesc& outDesc);
  void encoderFrame(const AvoxAFrame &frame);
  void flushEncoder() { if (encoder) encoder->flush(); }
  // 停编码器(同 VideoStream::stopEncoder): 排空 → 析构 → 断 muxer 链
  void stopEncoder();

  // IEncoderOb
public:
  virtual void onPacket(AvoxPacket &packet) override;
};

}