#pragma once

#include "../AvoxAudio.h"
#include "../module/Observer.hpp"
#include "../module/Ringbuffer.hpp"
#include "../module/RunTask.hpp"
#include "AudioFrame.hpp"
#include "AudioReshaper.hpp"

namespace avox {

// 音频 tap: 从 AudioRender 抽取正在播放的音频数据,深拷贝入队,
// tap 线程排空 → 重采样/切片(AudioReshaper) → 回调上层(IAudioTapOb)
// 默认关闭,open() 才启动;关闭时 AudioRender::render() 只多一个 atomic
// 判断,零成本
class AudioTap : public RunTask,
                 public AudioReshaper,
                 public Observer<IAudioTapOb> {
 public:
  AudioTap();
  virtual ~AudioTap();

 public:
  // 配置并启动:srcDesc 源格式,outDesc 为空(sampleRate=0)时跟随 srcDesc
  bool open(const AudioDesc& srcDesc, const AudioDesc& outDesc,
            int32_t frameMs);
  // 停止并销毁线程
  void close();
  // 是否开启
  bool bOpen();

  // 满队列策略:true 阻塞反压(转码离线不丢帧),false 丢最旧(默认)
  void setBlock(bool b) { bBlock = b; }
  // 音频线程调用:深拷贝 raw 入队,满则按 bBlock 阻塞或丢最旧
  void push(const AvoxData& raw, int64_t pts);

 protected:
  // AudioReshaper:定长块就绪时 dispatch 给 observer
  virtual void onProcess() override;
  // RunTask:排空 queue → reshape → onProcess
  virtual void onRunTask() override;

 private:
  // 有界队列
  RingBuffer<AudioFramePtr> queue{32};
  // 满则阻塞(true)或丢最旧(false)
  bool bBlock = false;
  // 开启标志
  std::atomic<bool> bRunning{false};
  // 源格式(用于跟随)
  AudioDesc srcDesc = {};
  // 是否已派发过 onAudioDesc
  std::atomic<bool> bDescDispatched{false};
};

}
