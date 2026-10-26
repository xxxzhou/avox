#include "avox/AvoxPlayer.h"

#ifdef AVOX_ENABLE_ZLMEDIAKIT
#include "avox_zlmediakit/TestSdpOb.hpp"
#endif
#include "../module/AvoxManager.hpp"
#include "../module/ModuleMgr.hpp"

namespace avox {

IRtcPlayer* createWebRtcPlayer() {
  // 静态构建(iOS/macOS)下模块注册靠懒触发, 与 createMediaPlayer 等工厂保持一致
  ModuleMgr::Get().ensureStarted();
  return AvoxManager::Get().rtcPlayerHub.create("webrtc");
}

IRtcEventOb* createZlTestSdpAgent(IRtcPlayer* player, const char* serverUrl) {
#ifdef AVOX_ENABLE_ZLMEDIAKIT
  if (!player || !serverUrl) {
    return nullptr;
  }
  auto ob = new TestSdpOb(serverUrl);
  ob->setPlayer(player);
  return ob;
#else
  (void)player;
  (void)serverUrl;
  return nullptr;
#endif
}

}
