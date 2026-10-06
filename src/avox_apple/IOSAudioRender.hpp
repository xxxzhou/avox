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
  std::mutex mtx;
  AudioComponentInstance audioUnit = nullptr;
  AudioDesc renderDesc = {};
  int32_t lastQueueMs = 0;
  uint32_t totalFrames = 0;
  uint32_t playedFrames = 0;

  // 环形缓冲区
  std::vector<uint8_t> ringBuffer;
  uint32_t writePos = 0;
  uint32_t readPos = 0;
  uint32_t bufferCapacity = 0;

  // 播放诊断: 回调内只做原子累加(实时线程不写日志), 由 onRender 每2s限频汇总
  std::atomic<uint64_t> underrunFrames{0};  // 数据不足被静音填充的采样帧
  std::atomic<uint64_t> lockBusyFrames{0};  // 抢锁失败整块静音的采样帧
  std::atomic<uint64_t> dropFrames{0};      // onRender 整帧塞不下被丢弃的帧数
  std::atomic<uint64_t> lateFeeds{0};       // 供给间隔超1.5帧长(生产被卡)次数
  std::atomic<uint32_t> maxLateMs{0};       // 观测窗内最大供给间隔(ms)
  int64_t lastFeedMs = 0;                   // 仅生产线程读写
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
