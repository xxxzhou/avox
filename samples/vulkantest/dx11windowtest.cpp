
#include <thread>

#include "avox/AvoxPlayer.h"
#include "avox_vulkan/VkTemplate.hpp"
#include "avox_windows/WinCommon.hpp"
#include "avox_windows/WinExport.h"

using namespace avox;

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  IMediaPlayer* mp = createMediaPlayer();
  mp->setHardDecode(true);
  mp->setIoPlan(IoPlan::ffmpeg); 
  mp->open("D:/Work/github/avox/assets/video/avox_electron.mp4");
  // mp->open("rtsp://127.0.0.1/live/test");
  // mp->open("D://Back/tt.mp4");
  mp->getSurfaceRender()->setVulkan(true);
  mp->getSurfaceRender()->setSurface(nullptr);
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