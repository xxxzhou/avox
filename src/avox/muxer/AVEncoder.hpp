#pragma once

#include "Muxer.hpp"

namespace avox {

class IEncoderOb {
 public:
  IEncoderOb() = default;
  virtual ~IEncoderOb() = default;

 public:
  virtual void onPacket(AvoxPacket& packet) = 0;
};

class AVOX_EXPORT AVEncoder {
 public:
  AVEncoder() = default;
  virtual ~AVEncoder() = default;

 protected:
  // 如果外面没有设置,生成自动
  int32_t bitrate = 0;

 public:
  virtual bool onVaild() { return true; }
  virtual DecodeResult onPreEncoder() = 0;
  virtual void flush() {}
  virtual void onClose() {};
};

}