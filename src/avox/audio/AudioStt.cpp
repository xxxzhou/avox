#include "AudioStt.hpp"

#include "../module/AvoxManager.hpp"
#include "../module/ModuleMgr.hpp"

namespace avox {

void AudioStt::setModelLevel(ModelLevel level) { modelLevel = level; }

void AudioStt::setRecognizerType(RecognizerType type) {
  if(currentType == type){
    return;
  }
  currentType = type;
  onRecognizerChange();
}

RecognizerType AudioStt::getRecognizerType() {
  return currentType;
}

void addAudioSttOb(IAudioStt* stt, IAudioSttOb* ob) {
  if (stt) {
    auto* audioStt = dynamic_cast<AudioStt*>(stt);
    if (audioStt) {
      audioStt->addObserver(ob);
    }
  }
}

void removeAudioSttOb(IAudioStt* stt, IAudioSttOb* ob) {
  if (stt) {
    auto* audioStt = dynamic_cast<AudioStt*>(stt);
    if (audioStt) {
      audioStt->removeObserver(ob);
    }
  }
}

// 通过 AvoxManager 工厂表创建语音识别器(组件 loadModule 时注册),
// none 或组件未注册返回 nullptr
IAudioStt* createAudioStt(AudioSttType type) {
  ModuleMgr::Get().ensureStarted();
  const char* key = (type == AudioSttType::sherpa) ? "sherpa" : nullptr;
  return key ? AvoxManager::Get().audioSttHub.create(key) : nullptr;
}

}
