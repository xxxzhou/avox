#include <cstdio>
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

IRtcEventOb* sdpOb = nullptr;

int main(int argc, char* argv[]) {
  sp = createWebRtcPlayer();
  sp->setRollType(RtcRollType::offer);
  const char* url = argc > 1
      ? argv[1]
      : "http://127.0.0.1/index/api/webrtc?app=live&stream=test&type=play";
  sdpOb = createZlTestSdpAgent(sp, url);
  sp->addOb(sdpOb);   // 信令观察者统一走 addOb 挂载
  sp->getRemoteSurfaceRender()->setSurface(nullptr);
  sp->open();
  // win32 消息循环; 8 秒后自动退出 (退出挂起排查)
  bool m_running = true;
  int autoQuitMs = 8000;
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
    if (autoQuitMs > 0) {
      autoQuitMs -= 1;
      if (autoQuitMs == 0) {
        m_running = false;
        break;
      }
    }
    // std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  fprintf(stderr, "[bisect] main: player->close begin\n");
  sp->close();
  fprintf(stderr, "[bisect] main: close enqueued, deleting player\n");
  delete sp;
  sp = nullptr;
  fprintf(stderr, "[bisect] main: player deleted, exiting main (atexit next)\n");
  return 0;
}
