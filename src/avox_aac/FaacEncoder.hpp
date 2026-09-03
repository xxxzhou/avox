#pragma once

#ifdef AVOX_ENABLE_FAAC

#include "avox/AvoxAudio.h"
#include "avox/audio/AudioEncoder.hpp"
#include "faac.h"

namespace avox {

class FaacEncoder : public AudioEncoder {
public:
  FaacEncoder();
  virtual ~FaacEncoder();

protected:
  faacEncHandle handle = nullptr;
  unsigned long inputSamples = 0;
  unsigned long maxOutputBytes = 0;
  std::vector<uint8_t> aacData;

protected:
  virtual AudioDesc getSupportDesc(const AudioDesc &desc) override;

public:
  virtual DecodeResult onPreEncoder() override;
  virtual DecodeResult encode(const AvoxAFrame &frame) override;
  virtual DecodeResult encode() override;
};
}

#endif