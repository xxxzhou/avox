#pragma once

#include "FFHelper.hpp"

#include "avox/muxer/IOMuxer.hpp"

namespace avox {

// 媒体流复用器，FFmpeg实现
class IOMuxerFF : public IOMuxer {
public:
  IOMuxerFF();
  virtual ~IOMuxerFF();

private:
  AVFormatContextPtr fmtCtx = nullptr;
  AVStream *videoStream = nullptr;
  AVStream *audioStream = nullptr;  
  int32_t configIndex = 0;
  std::vector<uint8_t> adtsPacket;
  AVPacketPtr packet = nullptr;
  // 连续非致命写失败计数: 只记首帧避免脏流刷屏, 成功时清零
  int32_t nonFatalDropCount = 0;

protected:
  virtual bool onInit() override;
  virtual void onPushPacket(const AvoxPacket& packet) override;
  virtual void onClose() override;
};

}