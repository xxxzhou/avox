#pragma once

#include "../FFHelper.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

class FFEncoder {
public:
  FFEncoder();
  virtual ~FFEncoder();

public:
  AVCodecContextPtr codecCtx = nullptr;
  AVFramePtr frame = nullptr;
  int32_t frameCount = 0;

public:
  DecodeResult onFrame(PackType type);   
};
}