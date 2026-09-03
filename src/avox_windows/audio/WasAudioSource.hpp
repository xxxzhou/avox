#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propkey.h>
#include <string>
#include <vector>

#include "../WinCommon.hpp"
#include "avox/module/RunTask.hpp"
#include "avox/source/AudioSource.hpp"

namespace avox {

// WASAPI 采集公共基类
// Activate/Initialize/GetBufferSize 与读包逻辑在此公共化
// 麦(event驱动)与 loopback(定时轮询)的差异由子类的 streamFlags/onRunTask 决定
// 基类继承 RunTask, 故 onOpen/onClose/bOpening 公共; onRunTask 仍由子类实现
class WasAudioBase : public AudioSource, public RunTask {
 public:
  WasAudioBase(MComPtr<IMMDevice> device, ADeviceKind kind);
  virtual ~WasAudioBase();

 protected:
  // Initialize 时使用的流标志, 麦与 loopback 不同
  virtual DWORD streamFlags() const = 0;
  MComPtr<IMMDevice> mmDevice;
  MComPtr<IAudioClient> audioClient;
  MComPtr<IAudioCaptureClient> captureClient;
  uint32_t bufferFrameCount = 0;
  // 读取当前所有可用包并派发为音频帧
  void drainPackets(std::vector<uint8_t>& buffer, uint32_t sampleSize);

 public:
  virtual bool onOpen() override;
  virtual void onClose() override;
  virtual bool bOpening() override;
};

// 麦克风采集: event 驱动
class WasMicSource : public WasAudioBase {
 public:
  WasMicSource(MComPtr<IMMDevice> device);

 protected:
  virtual DWORD streamFlags() const override {
    return AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
  }
  virtual void onRunTask() override;

 private:
  HANDLE eventHandle = nullptr;
};

// 声卡回环采集: 定时轮询(loopback 与 EVENTCALLBACK 互斥, 不能用事件回调)
class WasLoopbackSource : public WasAudioBase {
 public:
  WasLoopbackSource(MComPtr<IMMDevice> device);

 protected:
  virtual DWORD streamFlags() const override {
    return AUDCLNT_STREAMFLAGS_LOOPBACK;
  }
  virtual void onRunTask() override;
};

class WasAudioSourceMgr : public AudioManager<WasAudioBase> {
 public:
  WasAudioSourceMgr();
  virtual ~WasAudioSourceMgr();

 protected:
  virtual void onInitDevices() override;
};

}
