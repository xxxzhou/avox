#include <thread>

#include "avox/Avox.hpp"
#include "avox/AvoxSource.h"
#include "avox/module/LogHelper.hpp"
#include "avox_freetype/FreetypeExport.h"
#include "avox_vulkan/VkTemplate.hpp"

#include "avox/AvoxPlayer.h"

#ifdef AVOX_ENABLE_ZLMEDIAKIT
#include "avox_zlmediakit/ZlmExport.h"
#endif

#ifdef _WIN32
#include <windows.h>

#include "avox_windows/WinCommon.hpp"
#endif

using namespace avox;

IRtcPlayer* sp = nullptr;
IRawSource* source = nullptr;
IMediaMuxer* muxer = nullptr;

ISdpAgentOb* sdpOb = nullptr;

int main(int argc, char* argv[]) {
  sp = createWebRtcPlayer();
  sp->setRollType(RtcRollType::offer);
  const char* url =
      "http://127.0.0.1/index/api/webrtc?app=live&stream=test&type=play";  
  sdpOb = createZlTestSdpAgent(sp, url);
  sp->setSdpAgentOb(sdpOb);
  sp->getRemoteSurfaceRender()->setSurface(nullptr);  
  sp->open();  
  // win32 消息循环
  bool m_running = true;
  MSG msg;
  while (m_running) {
    // 处理所有待处理的消息
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        m_running = false;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    // std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  sp->close();
  return 0;
}
