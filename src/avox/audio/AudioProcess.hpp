#pragma once

#include "../module/Observer.hpp"
#include "AudioFrame.hpp"
#include "AudioReshaper.hpp"

#ifdef AVOX_ENABLE_FFMPEG
#include "avox_ffmpeg/FFResample.hpp"
#endif

namespace avox {

class IAudioProcessOb {
 public:
  virtual ~IAudioProcessOb() = default;
  virtual void onAudioProcess(const AvoxAFrame& frame) {};
};

// 音频处理基类
// 音频AudioReshaper处理,主要包含重采样,分隔固定音频数据
// 子类RtcAudioProcess 包含webrtc里的3A处理
class AVOX_EXPORT AudioProcess : public AudioReshaper, public Observer<IAudioProcessOb> {
 public:
  AudioProcess() = default;
  virtual ~AudioProcess() = default;

 protected:
  // 是否启用降噪
  bool enableNs = false;
  // 是否启用回声消除
  bool enableAec = false;
  // 是否启用增强
  bool enableAgc = false;

 public:
  bool init(const AudioDesc& src);
  // 处理音频数据,子类需要重写onProcess

 protected:
  virtual bool onInit() { return true; };
};

// class AudioProcess : public Observer<IAudioProcessOb> {};

AudioProcess* createWebRtcAudioProcess();

}