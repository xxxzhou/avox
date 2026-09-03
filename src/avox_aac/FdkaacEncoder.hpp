#pragma once

#ifdef AVOX_ENABLE_FDKAAC

#include "aacenc_lib.h"
#include "avox/AvoxAudio.h"
#include "avox/audio/AudioEncoder.hpp"

namespace avox {

class FdkaacEncoder : public AudioEncoder {
public:
  FdkaacEncoder();
  virtual ~FdkaacEncoder();

protected:
  HANDLE_AACENCODER handle = nullptr;
  AACENC_InfoStruct info = {};
  std::vector<uint8_t> aacData;
  int inputChannels = 0;
  int inputSampleRate = 0;
  int inputBitsPerSample = 16;

protected:
  virtual AudioDesc getSupportDesc(const AudioDesc &desc) override;

public:
  virtual DecodeResult onPreEncoder() override;
  virtual DecodeResult encode(const AvoxAFrame &frame) override;
  virtual DecodeResult encode() override;
};
}
#endif