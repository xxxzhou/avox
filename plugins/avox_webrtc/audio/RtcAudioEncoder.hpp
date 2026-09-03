#pragma once

#include "../RtcHelper.hpp"
#include "api/audio_codecs/audio_encoder.h"
#include "avox/audio/AudioEncoder.hpp"

namespace avox {

class RtcAudioEncoder : public webrtc::AudioEncoder, public avox::IEncoderOb {
 public:
  RtcAudioEncoder(const AudioDesc& adesc, int payloadType);
  virtual ~RtcAudioEncoder();

 protected:
  AudioDesc adesc = {};
  ATrackDesc encoderDesc = {};
  std::unique_ptr<avox::AudioEncoder> encode = nullptr;
  // 编码后的数据，由 onPacket 同步回调设置
  std::vector<uint8_t> encodedData;
  uint32_t currentRtpTimestamp = 0;
  int payloadType = 0;

 protected:
  void findEncoder();

 public:
  // WebRTC AudioEncoder 接口
  virtual int SampleRateHz() const override;
  virtual size_t NumChannels() const override;
  virtual int RtpTimestampRateHz() const override;
  virtual size_t Num10MsFramesInNextPacket() const override;
  virtual size_t Max10MsFramesInAPacket() const override;
  virtual int GetTargetBitrate() const override;
  virtual void Reset() override;
  virtual bool SetFec(bool enable) override;
  virtual bool SetDtx(bool enable) override;
  virtual bool SetApplication(
      webrtc::AudioEncoder::Application application) override;
  virtual void SetMaxPlaybackRate(int frequency_hz) override;
  virtual std::optional<std::pair<webrtc::TimeDelta, webrtc::TimeDelta>> GetFrameLengthRange()
      const override;
  virtual webrtc::AudioEncoder::EncodedInfo EncodeImpl(
      uint32_t rtp_timestamp, webrtc::ArrayView<const int16_t> audio,
      webrtc::Buffer* encoded) override;

  // IEncoderOb
 public:
  virtual void onPacket(AvoxPacket& packet) override;
};

}
