#pragma once

#include "RtcAudioEncoder.hpp"
#include "../RtcHelper.hpp"
#include "api/audio_codecs/audio_encoder_factory.h"

namespace avox {

struct AudioEncoderAAC {
  struct Config {
    int sample_rate_hz;
    int num_channels;
  };

  static std::optional<webrtc::AudioCodecInfo> QueryAudioEncoder(
      const Config& config) {
    // 编码器信息   
    return webrtc::AudioCodecInfo({config.sample_rate_hz,
                                   static_cast<size_t>(config.num_channels),
                                   64000, 6000, 510000});
  }

  static std::optional<Config> SdpToConfig(
      const webrtc::SdpAudioFormat& format) {
    if (equalsIgnoreCase(format.name, "aac") ||
        equalsIgnoreCase(format.name, "mpeg4-generic")) {
      Config config;
      config.sample_rate_hz = format.clockrate_hz;
      config.num_channels = static_cast<int>(format.num_channels);
      return config;
    }
    return std::nullopt;
  }

  static void AppendSupportedEncoders(
      std::vector<webrtc::AudioCodecSpec>* specs) { 
    specs->push_back({{"mpeg4-generic", 8000, 1}, {8000, 1, 128000}});
    specs->push_back({{"mpeg4-generic", 16000, 1}, {16000, 1, 128000}});
    specs->push_back({{"mpeg4-generic", 16000, 2}, {16000, 2, 128000}});
    specs->push_back({{"mpeg4-generic", 32000, 1}, {32000, 1, 128000}});
    specs->push_back({{"mpeg4-generic", 32000, 2}, {32000, 2, 128000}});
    specs->push_back({{"mpeg4-generic", 48000, 2}, {48000, 2, 128000}});
  }

  static std::unique_ptr<webrtc::AudioEncoder> MakeAudioEncoder(
      const AudioEncoderAAC::Config& config,
      int payload_type,  
      std::optional<webrtc::AudioCodecPairId> codec_pair_id = std::nullopt,
      const webrtc::FieldTrialsView* field_trials = nullptr) {
    AudioDesc adesc = {};
    adesc.channels = config.num_channels;
    adesc.sampleRate = config.sample_rate_hz;
    adesc.format = AudioFormat::AVOX_AUDIO_S16;
    // 直接傳入你的 RtcAudioEncoder 構造函數
    return std::make_unique<RtcAudioEncoder>(adesc, payload_type);
  }
};

webrtc::scoped_refptr<webrtc::AudioEncoderFactory>
CreateAvoxAudioEncoderFactory();

}