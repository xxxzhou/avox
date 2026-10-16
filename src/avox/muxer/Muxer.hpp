#pragma once

#include "../Avox.hpp"
#include "../codec/CodecChain.hpp"
#include "../module/Observer.hpp"

namespace avox {

struct MuxerDesc {
  std::string name;
};

class IMuxerContext {
 public:
  IMuxerContext() {}
  virtual ~IMuxerContext() {}

 protected:
  class MediaMuxer* muxer = nullptr;

 public:
  void setRawMuxer(class MediaMuxer* muxer);
  class MediaMuxer* getRawMuxer();
};

class IMuxerOb {
 public:
  IMuxerOb() = default;
  virtual ~IMuxerOb() = default;

 public:
  virtual void onMuxerOpen(class MediaMuxer* muxer) = 0;
  virtual void onMuxerClose() = 0;
};

// 返回视频默认的编码器名称
AVOX_EXPORT const char* getDefaultEncoderName(VCodecId codecId, bool bHard);

}