#pragma once

#include "RtcAudioDecoder.hpp"
#include "api/audio_codecs/audio_decoder_factory.h"

namespace avox {

// WebRTC里的opus非常完善,这里限制AAC时使用此解码器
// 如果是其他格式,则使用webrtc的opus解码器
// 使用CreateBuiltinAudioDecoderFactory
// class RtcAudioDecoderFactory : public webrtc::AudioDecoderFactory {
// public:
//   virtual ~RtcAudioDecoderFactory() {}

// public:
//   virtual std::vector<AudioCodecSpec> GetSupportedDecoders() override;
//   virtual bool IsSupportedDecoder(const SdpAudioFormat &format) override;
//   virtual absl_nullable std::unique_ptr<AudioDecoder>
//   Create(const Environment &env, const SdpAudioFormat &format,
//          std::optional<AudioCodecPairId> codec_pair_id) override;
// };

struct AudioDecoderAAC {
  struct Config {
    bool IsOk() const { return num_channels == 1 || num_channels == 2; }
    int num_channels = 1;
    int sample_rate_hz = 48000;
    int frame_size_ms = 40;
  };
  static std::optional<Config>
  SdpToConfig(const webrtc::SdpAudioFormat &audio_format);
  static void
  AppendSupportedDecoders(std::vector<webrtc::AudioCodecSpec> *specs);
  static std::unique_ptr<webrtc::AudioDecoder> MakeAudioDecoder(
      Config config,
      std::optional<webrtc::AudioCodecPairId> codec_pair_id = std::nullopt,
      const webrtc::FieldTrialsView *field_trials = nullptr);
};

webrtc::scoped_refptr<webrtc::AudioDecoderFactory>
CreateAvoxAudioDecoderFactory();

}
