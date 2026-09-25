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
  // codecCtx 是否已 avcodec_open2 成功: 未open的ctx internal为空, flush必崩
  // (FFmpeg8 avcodec_flush_buffers 无 internal 空守卫)
  bool bCtxOpened = false;
  AVFramePtr frame = nullptr;
  int32_t frameCount = 0;

public:
  DecodeResult onFrame(PackType type);   
};
}