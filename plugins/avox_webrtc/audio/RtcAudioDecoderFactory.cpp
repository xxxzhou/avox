#include "RtcAudioDecoderFactory.hpp"
#include "api/audio_codecs/L16/audio_decoder_L16.h"
#include "api/audio_codecs/audio_codec_pair_id.h"
#include "api/audio_codecs/audio_decoder.h"
#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/audio_decoder_factory_template.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/g711/audio_decoder_g711.h"
#include "api/audio_codecs/g722/audio_decoder_g722.h"
#include "api/audio_codecs/opus/audio_decoder_multi_channel_opus.h"
#include "api/audio_codecs/opus/audio_decoder_opus.h"
#include "api/scoped_refptr.h"

namespace avox {

using namespace webrtc;

std::optional<AudioDecoderAAC::Config>
AudioDecoderAAC::SdpToConfig(const webrtc::SdpAudioFormat &format) {
  // 检查format.name是否aac
  if (equalsIgnoreCase(format.name, "aac") ||
      equalsIgnoreCase(format.name, "mpeg4-generic")) {
    AudioDecoderAAC::Config config;
    config.num_channels = format.num_channels;
    config.sample_rate_hz = format.clockrate_hz;
    return config;
  }
  return std::nullopt;
}

void AudioDecoderAAC::AppendSupportedDecoders(
    std::vector<webrtc::AudioCodecSpec> *specs) {
  // AudioCodecInfo aacinfo{48000, 1, 64000, 6000, 510000};
  specs->push_back({{"mpeg4-generic", 8000, 1}, {8000, 1, 128000}});
  specs->push_back({{"mpeg4-generic", 16000, 1}, {16000, 1, 128000}});
  specs->push_back({{"mpeg4-generic", 16000, 2}, {16000, 2, 128000}});
  specs->push_back({{"mpeg4-generic", 32000, 1}, {32000, 1, 128000}});
  specs->push_back({{"mpeg4-generic", 32000, 2}, {32000, 2, 128000}});
  specs->push_back({{"mpeg4-generic", 48000, 2}, {48000, 2, 128000}});
}

std::unique_ptr<webrtc::AudioDecoder>
AudioDecoderAAC::MakeAudioDecoder(AudioDecoderAAC::Config config,
                                  std::optional<AudioCodecPairId> codec_pair_id,
                                  const FieldTrialsView *field_trials) {
  AudioDesc adesc = {};
  adesc.channels = config.num_channels;
  adesc.sampleRate = config.sample_rate_hz;
  adesc.format = AudioFormat::AVOX_AUDIO_S16;
  return std::make_unique<RtcAudioDecoder>(adesc);
}

scoped_refptr<AudioDecoderFactory> CreateAvoxAudioDecoderFactory() {
  return CreateAudioDecoderFactory<AudioDecoderOpus, AudioDecoderAAC,
                                   AudioDecoderG722, AudioDecoderG711>();
}

}
