#include "AudioProcess.hpp"

#include "../player/Player.hpp"
#include "../module/AvoxManager.hpp"

namespace avox {

bool AudioProcess::init(const AudioDesc& src) {
  srcDesc = src;
  outDesc = src;
  // onInit确定子类具体需要的outDesc
  if (!onInit()) {
    return false;
  }
  // initConfig根据格式确定重采样
  bool bInit = initConfig(srcDesc, outDesc);
  if(!bInit) {
    return false;
  }
  return true;
}

AudioProcess* createWebRtcAudioProcess() {
  return AvoxManager::Get().audioProcessHub.create("webrtc");
}

}
