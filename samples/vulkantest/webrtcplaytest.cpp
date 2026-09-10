#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>

#include "avox/Avox.hpp"
#include "avox/AvoxSource.h"
#include "avox/module/LogHelper.hpp"
#include "avox_freetype/FreetypeExport.h"
#include "avox_vulkan/VkTemplate.hpp"
#include "avox/AvoxImage.h"

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

// 抓当前渲染帧存 PNG (与 macrtcplaytest 同款), 用于延迟测量/远程看画面
static bool saveShot(ISurfaceRender* render, const char* path) {
  IImageBuffer* buf = createImageBuffer();
  bool ok = false;
  if (buf && render->screenShot(buf)) {
    ok = saveImagePath(path, buf);
    fprintf(stderr, "[rtc] screenshot %s: %s\n", path, ok ? "ok" : "save failed");
  } else {
    fprintf(stderr, "[rtc] screenshot failed: screenShot returned false\n");
  }
  delete buf;
  return ok;
}

int main(int argc, char* argv[]) {
  sp = createWebRtcPlayer();
  sp->setRollType(RtcRollType::offer);
  const char* url = argc > 1
      ? argv[1]
      : "http://127.0.0.1/index/api/webrtc?app=live&stream=test&type=play";
  // argv[2] 自动退出毫秒 (0=一直播); argv[3] 存图路径, 起播 2.5s 后每 500ms 覆盖存一张
  const int autoQuitMs = argc > 2 ? atoi(argv[2]) : 8000;
  const char* shotPath = argc > 3 ? argv[3] : nullptr;
  sdpOb = createZlTestSdpAgent(sp, url);
  sp->addOb(sdpOb);   // 信令观察者统一走 addOb 挂载
  sp->getRemoteSurfaceRender()->setSurface(nullptr);
  sp->open();
  const auto start = std::chrono::steady_clock::now();
  auto lastShot = start;
  bool running = true;
  MSG msg;
  while (running) {
    // 处理所有待处理的消息
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        running = false;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    if (autoQuitMs > 0 && elapsedMs >= autoQuitMs) {
      running = false;
      break;
    }
    if (shotPath && elapsedMs > 2500 &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastShot)
                .count() >= 500) {
      lastShot = now;
      saveShot(sp->getRemoteSurfaceRender(), shotPath);
    }
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
