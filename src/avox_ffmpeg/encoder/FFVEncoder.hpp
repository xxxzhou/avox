#pragma once

#include "FFEncoder.hpp"
#include "avox/video/VideoEncoder.hpp"

namespace avox {

class FFVEncoder : public FFEncoder, public VideoEncoder {
public:
  FFVEncoder();
  virtual ~FFVEncoder();

protected:
  bool bHwEncode = false;
  YuvType yuvType = YuvType::other;

public:
  virtual DecodeResult onPreEncoder() override;
  virtual DecodeResult encode(const YUVFrame &frame) override;
  // flush 重置编码器 GOP/参考帧,seek 后下一帧自然为 IDR
  virtual void flush() override;

protected:
  virtual void onAttachContext() {};
};

}