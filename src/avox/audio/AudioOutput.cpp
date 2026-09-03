#include "AudioOutput.hpp"

#include "../module/AvoxManager.hpp"

namespace avox {

AudioOutput* getDefaultAudioOutput() {
  ARenderType aRenderType = getDefaultAudioType();
  const auto& audioClass = AvoxManager::Get().aRender.initFunc(aRenderType);
  AudioRender* audioRender = audioClass.initFunc();
  if (!audioRender) {
    LOGFLF(LogLevel::warn, "getDefaultAudioOutput failed");
    return nullptr;
  }
  // 工厂返回的是平台子类(WasAudioRender 等),IS-A AudioOutput
  return static_cast<AudioOutput*>(audioRender);
}

}
