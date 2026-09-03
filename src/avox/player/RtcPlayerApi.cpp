#include "avox/AvoxPlayer.h"

#include "avox_zlmediakit/TestSdpOb.hpp"
#include "../module/AvoxManager.hpp"

namespace avox {

IRtcPlayer* createWebRtcPlayer() {
  return AvoxManager::Get().rtcPlayerHub.create("webrtc");
}

ISdpAgentOb* createZlTestSdpAgent(IRtcPlayer* player, const char* serverUrl) {
  if (!player || !serverUrl) {
    return nullptr;
  }
  auto ob = new TestSdpOb(serverUrl);
  ob->setPlayer(player);
  return ob;
}

}
