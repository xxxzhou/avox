#pragma once

#include "../video/VideoEncoder.hpp"

namespace avox {

// 打开线程,使用编码器把原始数据编码
class VideoStream : public IEncoderOb, public IMuxerContext {
public:
  VideoStream();
  virtual ~VideoStream();

protected:
  VTrackDesc desc = {};
  // 是否硬编
  bool bHardEncoder = true;
  // 几秒一个GOP
  int32_t gop = 3;
  std::unique_ptr<VideoEncoder> encoder = nullptr;  

public:
  void setHardEncode(bool bHard);
  bool getHardEncode();
  void setVideoDesc(const VTrackDesc &desc);
  void encoderFrame(const YUVFrame &frame);
  void encoderFrame(const GpuFrame &frame);
  // flush 编码器(seek 时重置 GOP/参考帧,下一帧自然 IDR)
  void flushEncoder() { if (encoder) encoder->flush(); }

  // IEncoderOb
public:
  virtual void onPacket(AvoxPacket &packet) override;
};

}
