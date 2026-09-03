#pragma once

#include "../FFResample.hpp"
#include "FFEncoder.hpp"
#include "avox/audio/AudioEncoder.hpp"

namespace avox {

class FFAEncoder : public FFEncoder, public AudioEncoder {
public:
  FFAEncoder();
  virtual ~FFAEncoder();

protected:
  virtual AudioDesc getSupportDesc(const AudioDesc &desc) override;

public:
  virtual DecodeResult onPreEncoder() override;
  virtual DecodeResult encode(const AvoxAFrame &frame) override;
  virtual DecodeResult encode() override;
};

}
