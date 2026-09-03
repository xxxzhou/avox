#pragma once

#include "../module/RunTask.hpp"
#include "../player/MPCommon.hpp"
#include "AudioDecoder.hpp"

namespace avox {

// 单独线程从AudioTrack队列读取包数据,调用解码器解码
class ADecoderTask : public IPlayerContext, public RunTask {
 public:
  ADecoderTask();
  virtual ~ADecoderTask();

 protected:
  AudioDesc srcDesc = {};
  ACodecId codecId = ACodecId::none;
  PBAudioInfo pbInfo = {};
  // 解码器允许的延迟,单位ms
  int32_t delayMs = 5000;

 protected:
  class AudioTrack* trackContext = nullptr;
  std::unique_ptr<AudioDecoder> decode = nullptr;
  PacketBufPtr confPkt = nullptr;
  std::mutex configMutex;

 public:
  bool start(class AudioTrack* trackContext);
  void flush();
  void close();

 public:
  AudioDecoder* getDecoder() { return decode.get(); }
  void addConfigRecord(ConfigAddType type);

  // RunTask
 protected:
  virtual void onRunTask() override;
};

}