#pragma once

#include "../module/Observer.hpp"
#include "AudioFrame.hpp"

#ifdef AVOX_ENABLE_FFMPEG
#include "avox_ffmpeg/FFResample.hpp"
#endif

namespace avox {

// 音频前置处理,主要包含重采样,分隔固定音频数据
class AVOX_EXPORT AudioReshaper {
 public:
  AudioReshaper() = default;
  virtual ~AudioReshaper() = default;

 protected:
  AudioDesc srcDesc = {};
  // 音频处理可能要求特定的输出格式，如采样率需要是16000的倍数
  AudioDesc outDesc = {};
  // webrtc音频处理需要10ms的音频数据
  AudioFrame curFrame = {};
  // 每秒音频字节数
  int32_t frameSize = 0;
  // 每次处理的时间,单位ms
  int32_t frameMs = 10;
  // 重采样,如果音频处理需要特定格式,需要重采样
#ifdef AVOX_ENABLE_FFMPEG
  std::unique_ptr<FFResample> resample = nullptr;
#endif
  // 处理后的buffer
  std::vector<uint8_t> pbuffer;

 protected:
  // desc可能被修改，使用AudioProcess的需要注意使用返回的desc
  bool initConfig(const AudioDesc& src, AudioDesc& out);

 public:
  void process(const AvoxAFrame& frame);
  // 排干收尾: 把 swr 内部残余(filter delay)喂进 curFrame, 连同 curFrame 里
  // 没满的尾部, 作为最后若干帧经 onProcess 吐出。供会话/流停止前排干尾部用。
  void flush();

 protected:
  // process填充curFrame满后，调用onProcess
  virtual void onProcess() {};
};

}