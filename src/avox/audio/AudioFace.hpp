#pragma once

#include "../AvoxAudio.h"
#include "../AvoxDef.h"
#include "../module/Observer.hpp"

namespace avox {

// 音频→blendshape 内部基类: 对称 AudioStt/AudioTts。
// IAudioFace 对外接口 + Observer<IAudioFaceOb> 回调分发;
// 通用配置(modelLevel)在基类, 推理逻辑纯虚留给 Wav2ArkitFace 等子类(avox_avatar
// 插件)。 方向: PCM 进(feed) → ARKit52 blendshape 出(经
// IAudioFaceOb::onFaceBlendshape)。
class AVOX_EXPORT AudioFace : public IAudioFace, public Observer<IAudioFaceOb> {
 public:
  AudioFace() = default;
  virtual ~AudioFace() = default;

 protected:
  ModelLevel modelLevel = ModelLevel::base;

 public:
  // 通用配置在基类实现(仅赋值)
  void setModelLevel(ModelLevel level) override;
  // 推理相关纯虚, 子类实现
  void setAudioDesc(AudioDesc desc) override = 0;
  void start() override = 0;
  void feed(const AvoxData& pcm, int64_t pts) override = 0;
  void stop() override = 0;
  bool loading() override = 0;
};

}
