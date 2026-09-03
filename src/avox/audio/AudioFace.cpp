#include "AudioFace.hpp"

#include "../module/AvoxManager.hpp"
#include "../module/ModuleMgr.hpp"

namespace avox {

void AudioFace::setModelLevel(ModelLevel level) { modelLevel = level; }

void addAudioFaceOb(IAudioFace* face, IAudioFaceOb* ob) {
  if (face) {
    auto* audioFace = dynamic_cast<AudioFace*>(face);
    if (audioFace) {
      audioFace->addObserver(ob);
    }
  }
}

void removeAudioFaceOb(IAudioFace* face, IAudioFaceOb* ob) {
  if (face) {
    auto* audioFace = dynamic_cast<AudioFace*>(face);
    if (audioFace) {
      audioFace->removeObserver(ob);
    }
  }
}

// 通过 AvoxManager 工厂表创建音频→blendshape 推理器(组件 loadModule 时注册),
// none 或组件未注册返回 nullptr
IAudioFace* createAudioFace(AudioFaceType type) {
  ModuleMgr::Get().ensureStarted();
  const char* key = (type == AudioFaceType::wav2arkit) ? "wav2arkit" : nullptr;
  return key ? AvoxManager::Get().audioFaceHub.create(key) : nullptr;
}

}
