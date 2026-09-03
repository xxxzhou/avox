#pragma once

#include "../FFHelper.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox/source/PacketBuf.hpp"

namespace avox {

class FFDecoder {
public:
  FFDecoder();
  virtual ~FFDecoder();

protected:
  AVCodecContextPtr codecCtx = nullptr;
  AVFramePtr avFrame = nullptr;
  AVPacketPtr avPacket = nullptr;
  PacketBufPtr packBuf = nullptr;
  std::vector<uint8_t> packData;
  int64_t preVPts = 0;

protected:
  // 解码完成，子类具体实现
  virtual void onFrame(AVFrame *avFrame, bool bDrop) = 0;
  virtual void onError(int32_t error) {}

public:
  // 使用FFmpeg解码原始包
  DecodeResult decodePacket(const AvoxPacket &packet);
  void flushContext();
};

}
