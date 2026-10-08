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
  // 停编码器: 排空(待出帧在 sink 尚活时出完) → 析构会话 → 断 muxer 链。
  // 关流必须「先停编码器再放 sink」: 硬编回调在 VT 队列上异步到达, sink 先死
  // 就是回调打到已释放队列(ENH 崩溃: enqueueWait 内 mutex SIGSEGV)。
  void stopEncoder();

  // IEncoderOb
public:
  virtual void onPacket(AvoxPacket &packet) override;
};

}
