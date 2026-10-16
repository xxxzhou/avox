#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

#include "../codec/AudioDecoderChain.hpp"
#include "../module/RunTask.hpp"
#include "../player/MPCommon.hpp"
#include "AudioDecoder.hpp"

namespace avox {

// 单独线程从AudioTrack队列读取包数据,调用解码器解码
class ADecoderTask : public IPlayerContext, public RunTask,
                     private AudioDecoderChain::Host {
 public:
  ADecoderTask();
  virtual ~ADecoderTask();

 private:
  // 车道链回调: 只做候选链不知道的事(接线/解绑/重放配置包)
  virtual bool laneInit(AudioDecoder &impl, const ACodecDesc &desc) override;
  virtual void laneClose(AudioDecoder &impl) override;
  virtual void laneSaveConfigs(AudioDecoder &oldImpl) override;
  virtual void laneChanged(AudioDecoder &newImpl, bool bFirst) override;
  AudioDecoder *decoder() { return chain.impl(); }

 protected:
  AudioDesc srcDesc = {};
  ACodecId codecId = ACodecId::none;
  PBAudioInfo pbInfo = {};
  // 解码器允许的延迟,单位ms
  int32_t delayMs = 5000;
  // 候选车道链: 与抽帧腿/WebRTC 收流共用同一份策略
  AudioDecoderChain chain;
  // 换道要重放的配置包(本腿自行缓存: AudioDecoder 没有 copyConfigs)
  std::vector<PacketBufPtr> configPkts;

 protected:
  class AudioTrack* trackContext = nullptr;
  PacketBufPtr confPkt = nullptr;
  std::mutex configMutex;
  // flush请求: 命令线程只置位, 音频线程在onRunTask消费执行(防跨线程操作codecCtx)
  std::atomic<bool> bFlushCtx{false};

 public:
  bool start(class AudioTrack* trackContext);
  void flush();
  void close();

 public:
  AudioDecoder* getDecoder() { return chain.impl(); }
  void addConfigRecord(ConfigAddType type);

  // RunTask
 protected:
  virtual void onRunTask() override;
};

}
