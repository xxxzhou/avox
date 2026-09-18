#pragma once

#include "../AvoxAudio.h"
#include "../AvoxDef.h"
#include "../module/Observer.hpp"
#include "AudioFrame.hpp"

namespace avox {

class AVOX_EXPORT AudioStt : public IAudioStt, public Observer<IAudioSttOb> {
 public:
  AudioStt() = default;
  virtual ~AudioStt() = default;

 protected:
  ModelLevel modelLevel = ModelLevel::base;

 public:
  // IAudioStt 接口
  void setModelLevel(ModelLevel level) override;
  void setRecognizerType(RecognizerType type) override;
  RecognizerType getRecognizerType() override;
  void setAudioDesc(AudioDesc desc) override = 0;
  void start() override = 0;
  void recognize(const AvoxData& adata, int64_t pts) override = 0;
  void stop() override = 0;
  bool loading() override = 0;
  virtual void onRecognizerChange() {}

 protected:
  RecognizerType currentType = RecognizerType::offline;
  // offline 自带反压: 内部队列满则阻塞喂料方(批量转写零丢帧), streaming 丢最旧不阻塞解码
  bool bBlockingFeed() const { return currentType == RecognizerType::offline; }
};

}
