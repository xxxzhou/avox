#pragma once

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <atomic>
#include <memory>
#include <mutex>

#include "../WinCommon.hpp"
#include "avox/audio/AudioOutput.hpp"
#ifdef AVOX_ENABLE_FFMPEG
#include "avox_ffmpeg/FFResample.hpp"
#endif
namespace avox {

class WasAudioRender : public AudioOutput {
 public:
  WasAudioRender();
  virtual ~WasAudioRender();

 private:
  MComPtr<IMMDeviceEnumerator> deviceEnumerator;
  MComPtr<IMMDevice> device;
  MComPtr<IAudioClient> audioClient;
  MComPtr<IAudioRenderClient> renderClient;
  // 默认设备切换监听:回调只置共享标志,重建在渲染线程做
  MComPtr<IMMNotificationClient> deviceNotifier;
  std::shared_ptr<std::atomic<bool>> deviceSwitchPending;
  uint64_t retryAtMs = 0;  // 重建失败退避(设备全拔等暂不可用)
  bool pausedState = false;
  uint32_t sampleCount = 0;
  std::mutex mtx;
  AudioDesc renderDesc = {};
  int32_t lastQueueMs = 0;  // 平滑用的上次值
  // 重采样,如果音频处理需要特定格式,需要重采样
#ifdef AVOX_ENABLE_FFMPEG
  std::unique_ptr<FFResample> resample = nullptr;
#endif
 protected:
  virtual void onInit() override;
  virtual void onRender(const AvoxData& frame) override;
  virtual void onClose() override;

 private:
  // mtx 已持锁版本:onRender 内做设备重建/失效检测时复用
  bool initLocked();
  void closeLocked();
  void scheduleDeviceReinitLocked();
  void reinitIfPendingLocked();

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

// 将 WAVEFORMATEX 转换为 AudioFormat
AudioFormat waveFormatToAudioFormat(const WAVEFORMATEX* format);

}
