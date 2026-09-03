#pragma once

#include "AudioRender.hpp"

namespace avox {

// 音频设备输出层
// 声明设备专属的纯虚方法,平台子类(WasAudioRender/AndAudioRender/IOSAudioRender)必须实现
// ARenderTask / SourcePlayer 直接持有 AudioOutput*
class AVOX_EXPORT AudioOutput : public AudioRender {
 public:
  AudioOutput() = default;
  virtual ~AudioOutput() = default;

 protected:
  // 设备生命周期钩子(纯虚,子类必须实现)
  virtual void onInit() = 0;
  virtual void onRender(const AvoxData& frame) = 0;
  virtual void onClose() = 0;

 public:
  // 设备缓冲/时钟控制(纯虚,子类必须实现)
  virtual bool empty() = 0;
  virtual int32_t getQueueMS() = 0;
  virtual bool full() = 0;
  virtual void pause(bool pause) = 0;
  virtual void flush() = 0;
  virtual void speed(double speed) = 0;
  // setVolume/getVolume 由平台子类 override(WASAPI/AT/AU 各有实现)
};

// 获取当前平台默认音频输出(工厂: AvoxManager 注册, WasapiOutput/AndroidATOutput/IOSAUOutput)
AudioOutput* getDefaultAudioOutput();

}
