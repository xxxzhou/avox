#include "AudioTts.hpp"

#include "../module/AvoxManager.hpp"
#include "../module/ModuleMgr.hpp"

namespace avox {

void AudioTts::setModelLevel(ModelLevel level) { modelLevel = level; }
void AudioTts::setSpeed(float s) { speed = s; }
void AudioTts::setSpeaker(int s) { sid = s; }

void addAudioTtsOb(IAudioTts* tts, IAudioTtsOb* ob) {
  if (tts) {
    auto* audioTts = dynamic_cast<AudioTts*>(tts);
    if (audioTts) {
      audioTts->addObserver(ob);
    }
  }
}

void removeAudioTtsOb(IAudioTts* tts, IAudioTtsOb* ob) {
  if (tts) {
    auto* audioTts = dynamic_cast<AudioTts*>(tts);
    if (audioTts) {
      audioTts->removeObserver(ob);
    }
  }
}

// 通过 AvoxManager 工厂表创建语音合成器(组件 loadModule 时注册),
// none 或组件未注册返回 nullptr
IAudioTts* createAudioTts(AudioTtsType type) {
  ModuleMgr::Get().ensureStarted();
  const char* key = (type == AudioTtsType::sherpa) ? "sherpa" : nullptr;
  return key ? AvoxManager::Get().audioTtsHub.create(key) : nullptr;
}

}
