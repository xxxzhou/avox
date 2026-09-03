#pragma once

#include "avox/AvoxCodec.h"
#include "avox/AvoxPlayer.h"

#ifdef AVOX_ENABLE_FAAD2
#include "faad.h"
#endif

namespace avox {

#ifdef AVOX_ENABLE_FAAD2
AudioFormat faadAudioFromat(int32_t format);
#endif

}
