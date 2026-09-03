#include <thread>

#include "avox/AvoxPlayer.h"
#include "avox_vulkan/VkTemplate.hpp"
#include "avox_windows/WinExport.h"
#include "avox/AvoxPlayer.h"

#ifdef AVOX_ENABLE_FREETYPE
#include "avox_freetype/FreetypeExport.h"
#endif

#ifdef AVOX_ENABLE_SHERPA
#include "avox_sherpa/SherpaExport.h"
#endif

#ifdef _WIN32
#include <windows.h>
#include "avox_windows/WinCommon.hpp"
#endif

#include "avox_zlmediakit/ZlmExport.h"

#include "avox/Avox.hpp"

using namespace avox;

IMediaPlayer* mp = nullptr;
IMediaMuxer* muxer = nullptr;
IFontLayer* fontLayer = nullptr;
ISubtitle* subtitle = nullptr;

int main(int argc, char* argv[]) {
  // hinst = hInstance;
  mp = createMediaPlayer();
  muxer = mp->getMuxer(false);
  mp->setHardDecode(true);
  // mp->setIoPlan(IoPlan::zlmediakit);
  mp->getSurfaceRender()->setSurface(nullptr);
  // mp->getSurfaceRender()->setOffSurface(YuvType::nv12);
  // mp->getSurfaceRender()->setVulkan(false);
  mp->setIoPlan(IoPlan::ffmpeg);
  // mp->getOption()->setBool("log.source.packet", true);
  // mp->getOption()->setBool("log.decoder.frame", true);
  // mp->getOption()->setBool("log.render.frame", true);
  // mp->open("D://Back//美好_h265.mp4");
  // mp->open("D:/Back/0202.mkv");
  // mp->open("D://Back//tt1.mp4");
  mp->open("D://Back//美好02.mp4");
  // mp->open("rtsp://127.0.0.1/live/test");
  subtitle = mp->getSubtitle();
  subtitle->enableTranslation();
  subtitle->enableAsr();
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
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  // testAsynWindow();
  // th.join();
  mp->close();
  return 0;
}