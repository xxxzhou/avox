#pragma once

#include <AVFoundation/AVFoundation.h>
#include <AudioToolbox/AudioToolbox.h>

#include <atomic>
#include <mutex>

#include "avox/audio/AudioOutput.hpp"

#ifdef AVOX_ENABLE_FFMPEG
#include "avox_ffmpeg/FFResample.hpp"
#endif

namespace avox {

class IOSAudioRender : public AudioOutput {
 public:
  IOSAudioRender();
  virtual ~IOSAudioRender();

 private:
  // 控制路径锁: 只护设备生命周期/音量。数据路径(回调↔生产)走无锁原子索引,
  // 实时回调绝不取锁 —— 取锁失败=整块静音, 实测占播放时长 2.35%
  std::mutex mtx;
  AudioComponentInstance audioUnit = nullptr;
  AudioDesc renderDesc = {};
  int32_t lastQueueMs = 0;
  std::atomic<uint32_t> totalFrames{0};
  uint32_t playedFrames = 0;
  // 设备就绪且已启动: 回调/查询据此提前退出的判据(替代对 audioUnit 的裸读)
  std::atomic<bool> running{false};

  // 无锁环形缓冲(SPSC): 容量取2的幂便于掩码取模, 生产/消费各独占一个索引
  std::vector<uint8_t> ringBuffer;
  uint32_t ringSize = 0;
  uint32_t ringMask = 0;
  std::atomic<uint32_t> wPos{0};
  std::atomic<uint32_t> rPos{0};
  // 冲刷请求: 由回调(唯一写 rPos 者)兑现, 免得控制线程与实时线程争同一索引
  std::atomic<uint32_t> flushTarget{0};
  std::atomic<bool> flushReq{false};

  // 播放诊断: 回调内只做原子累加(实时线程不写日志), 由 onRender 每2s限频汇总
  std::atomic<uint64_t> silenceFrames{0};  // 回调吐静音的采样帧(=可闻丢音)
  std::atomic<uint64_t> dropFrames{0};     // onRender 整帧塞不下被丢弃的帧数
  std::atomic<uint64_t> lateFeeds{0};      // 供给间隔超1.5帧长(生产被卡)次数
  std::atomic<uint32_t> maxLateMs{0};      // 观测窗内最大供给间隔(ms)
  int64_t lastFeedMs = 0;                  // 仅生产线程读写
  int64_t lastDiagMs = 0;
  int64_t diagWindow = 0;
  void reportDiag(int64_t nowMs);

#ifdef AVOX_ENABLE_FFMPEG
  std::unique_ptr<FFResample> resample = nullptr;
#endif

  static OSStatus renderCallback(void* inRefCon,
                                 AudioUnitRenderActionFlags* ioActionFlags,
                                 const AudioTimeStamp* inTimeStamp,
                                 UInt32 inBusNumber, UInt32 inNumberFrames,
                                 AudioBufferList* ioData);

 protected:
  virtual void onInit() override;
  virtual void onRender(const AvoxData& frame) override;
  virtual void onClose() override;

 public:
  virtual bool empty() override;
  virtual int32_t getQueueMS() override;
  virtual bool full() override;
  virtual void pause(bool pause) override;
  virtual void flush() override; 
  virtual void speed(double speed) override;
  virtual void setVolume(float volume) override;
  virtual float getVolume() override;
};

// 将 AudioFormat 转换为 AudioStreamBasicDescription
AudioStreamBasicDescription audioFormatToASBD(const AudioDesc& desc,
                                              double sampleRate = 0);

}
