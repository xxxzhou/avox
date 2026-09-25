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
  // codecCtx 是否已 avcodec_open2 成功: 未open的ctx internal为空, flush必崩
  // (FFmpeg8 avcodec_flush_buffers 无 internal 空守卫), 只有owner线程读写
  bool bCtxOpened = false;
  AVFramePtr avFrame = nullptr;
  AVPacketPtr avPacket = nullptr;
  PacketBufPtr packBuf = nullptr;
  std::vector<uint8_t> packData;
  int64_t preVPts = 0;
  // 未出过帧的连续喂包失败计数: 达到阈值判车道打不开(openFailed), 上层瞬时降级
  int32_t sendFailStreak = 0;
  // 是否成功解出过帧
  bool bDecodedEver = false;
  // 喂包失败判open失败的连续次数阈值, 留余量容忍偶发ENOMEM
  static constexpr int32_t kSendFailOpenFailLimit = 3;

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
