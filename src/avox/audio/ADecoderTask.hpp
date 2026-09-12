#pragma once

#include <functional>
#include <mutex>
#include <vector>

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
  // 解码器回退链: 首选解码器初始化失败时(如fdk-aac不支持AAC Main profile)
  // 依序换同codecId的下一个注册解码器, 重放缓存的配置包重新初始化
  std::vector<std::pair<ACodecDesc, std::function<AudioDecoder*()>>> fallbacks;
  std::vector<PacketBufPtr> configPkts;
  // 回退后重置打开判定, 让超时检查重新起算
  bool bFallbackTried = false;

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