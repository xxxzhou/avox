#pragma once

#include "../audio/AudioProcess.hpp"
#include "DeviceManager.hpp"

namespace avox {

class AudioSource : public IAudioProcessOb,
                    public TDeviceSource<IAudioSource, IAudioSourceOb> {
public:
  AudioSource();
  virtual ~AudioSource() = default;

protected:
  AudioDesc desc = {};
  // 是否添加音频处理
  bool enableProcess = false;
  // 音频处理能否初始化
  bool initProcess = false;
  // 音频3A处理
  std::unique_ptr<AudioProcess> audioProcess = nullptr;
  ADeviceKind deviceKind = ADeviceKind::none;

protected:
  void onFrame(const AvoxAFrame &frame);

  // IAudioProcessOb
public:
  // 设备来源类别
  virtual ADeviceKind getDeviceKind() override { return deviceKind; }
  virtual void onAudioProcess(const AvoxAFrame &frame) override;
};

}