#pragma once

#include "FFDecoder.hpp"
#include "avox/audio/AudioDecoder.hpp"

namespace avox {

class FFADecoder : public AudioDecoder, public FFDecoder {
 public:
  FFADecoder();
  virtual ~FFADecoder();

 protected:
  AvoxAFrame aframe = {};
  // 音频如果是平面格式,需要重组
  std::vector<uint8_t> planeAudio;

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
  virtual void onFrame(AVFrame* avFrame, bool bDrop) override;
  virtual void onError(int32_t error) override;

 
};

}