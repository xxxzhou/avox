#pragma once

#include "../RtcHelper.hpp"

#include "api/audio_codecs/audio_decoder.h"
#include "avox/audio/AudioDecoder.hpp"
#include <deque>

namespace avox {

#define AVOX_TEST_AUDIO_RECORDER 0

class RtcAudioDecoder : public webrtc::AudioDecoder,
                        public avox::IAudioDecoderOb {
public:
  RtcAudioDecoder(const AudioDesc &adesc);
  virtual ~RtcAudioDecoder();

protected:
  AudioDesc adesc = {};
  AvoxAFrame frame = {};
  std::unique_ptr<avox::AudioDecoder> decode = nullptr;
  std::vector<uint8_t> aacData;
  std::vector<uint8_t> rtpData;
  uint32_t preRtpTime = 0;

#if AVOX_TEST_AUDIO_RECORDER
  // 用来测试记录PCM数据
  bool bRecordPcm = false;
  std::ofstream fileStream;
  std::string filePath = "D:\\test.pcm";
#endif

protected:
  void findDecoder();

public:
  virtual void Reset() override;
  virtual int SampleRateHz() const override;
  virtual size_t Channels() const override;
  virtual int PacketDuration(const uint8_t *encoded,
                             size_t encoded_len) const override;
  virtual std::vector<ParseResult> ParsePayload(webrtc::Buffer &&payload,
                                                uint32_t timestamp) override;

protected:
  virtual int DecodeInternal(const uint8_t *encoded, size_t encoded_len,
                             int sample_rate_hz, int16_t *decoded,
                             SpeechType *speech_type) override;

  // IAudioDecoderOb
public:
  virtual void onDecode(const AvoxAFrame &frame) override;
};

}