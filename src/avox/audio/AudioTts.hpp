#pragma once

#include "../AvoxAudio.h"
#include "../AvoxDef.h"
#include "../module/Observer.hpp"

namespace avox {

// 语音合成内部基类: 对称 AudioStt。
// IAudioTts 对外接口 + Observer<IAudioTtsOb> 回调分发;
// 通用配置(modelLevel/speed/sid)在基类, 合成逻辑纯虚留给 SherpaAudioTts
// 等子类。 与 STT 方向相反: 文本进(synthesize) → PCM 出(经
// IAudioTtsOb::onTtsAudio)。
class AVOX_EXPORT AudioTts : public IAudioTts, public Observer<IAudioTtsOb> {
 public:
  AudioTts() = default;
  virtual ~AudioTts() = default;

 protected:
  ModelLevel modelLevel = ModelLevel::base;
  float speed = 1.0f;  // 语速, [SPEED:x] 标记驱动
  int sid = 0;         // 说话人 ID, 多说话人模型用

 public:
  // 通用配置在基类实现(仅赋值)
  void setModelLevel(ModelLevel level) override;
  void setSpeed(float speed) override;
  void setSpeaker(int sid) override;
  // 合成相关纯虚, 子类实现
  void setAudioDesc(AudioDesc desc) override = 0;
  void start() override = 0;
  void synthesize(const char* text) override = 0;
  void stop() override = 0;
  bool loading() override = 0;
};

}
