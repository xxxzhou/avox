#include "RtcAudioEncoderFactory.hpp"

#include "api/audio_codecs/audio_encoder_factory_template.h"
#include "api/audio_codecs/g711/audio_encoder_g711.h"
#include "api/audio_codecs/g722/audio_encoder_g722.h"
#include "api/audio_codecs/opus/audio_encoder_opus.h"

namespace avox {

webrtc::scoped_refptr<webrtc::AudioEncoderFactory>
CreateAvoxAudioEncoderFactory() {
  return webrtc::CreateAudioEncoderFactory<
      webrtc::AudioEncoderOpus, AudioEncoderAAC, webrtc::AudioEncoderG711,
      webrtc::AudioEncoderG722>();
}

}