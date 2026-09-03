#include "AacHelper.hpp"

namespace avox {

#ifdef AVOX_ENABLE_FAAD2
AudioFormat faadAudioFromat(int32_t format) {
  switch (format) {
    case FAAD_FMT_16BIT:
      return AudioFormat::AVOX_AUDIO_S16;
    case FAAD_FMT_32BIT:
      return AudioFormat::AVOX_AUDIO_S32;
    case FAAD_FMT_FLOAT:
      return AudioFormat::AVOX_AUDIO_FLT;
    case FAAD_FMT_DOUBLE:
      return AudioFormat::AVOX_AUDIO_DBL;
    case FAAD_FMT_24BIT:
    default:
      return AudioFormat::other;
  }
}
#endif

}
