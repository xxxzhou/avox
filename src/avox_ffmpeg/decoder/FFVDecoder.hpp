#pragma once

#include "FFDecoder.hpp"
#include "avox/video/VideoDecoder.hpp"

namespace avox {

class FFVDecoder : public VideoDecoder, public FFDecoder {
public:
  FFVDecoder();
  virtual ~FFVDecoder();

  // AVDecoder
public:
  // 初始化
  virtual bool onVaild() override;
  // 初始化
  virtual DecodeResult onPreDecoder() override;
  // 解码
  virtual DecodeResult decode(const AvoxPacket & packet) override;
  // flush
  virtual void flush() override;
  virtual void onClose() override;

  // FFDecoder
protected:
  // 解码完成，子类具体实现
  virtual void onFrame(AVFrame *avFrame, bool bDrop) override;
  virtual void onError(int32_t error) override;

protected:
  virtual void onAttachContext();
  virtual void onDetachContext();
};

}