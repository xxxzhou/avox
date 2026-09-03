#pragma once

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

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
