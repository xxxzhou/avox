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

  // IEncoderOb
public:
  virtual void onPacket(AvoxPacket &packet) override;
};

}