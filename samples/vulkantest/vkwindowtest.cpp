
#include <thread>

#include "avox/AvoxPlayer.h"
#include "avox_vulkan/VkTemplate.hpp"
#include "avox_windows/WinExport.h"

#ifdef AVOX_ENABLE_FREETYPE
#include "avox_freetype/FreetypeExport.h"
#endif

#ifdef _WIN32
#include <windows.h>

#include "avox_windows/WinCommon.hpp"
#endif

#include "avox/Avox.hpp"

using namespace avox;

IMediaPlayer* mp = nullptr;
IMediaPlayer* mp1 = nullptr;
IMediaMuxer* muxer = nullptr;

// int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
int main() {
  // hinst = hInstance;
  mp = createMediaPlayer();
  muxer = mp->getMuxer(true);
  mp->setHardDecode(true);
  // mp->getOption()->setBool("mp.lowlatency", true);
  mp->getOption()->setNumber("mp.lowlatency.speed", 1.2);
  mp->getOption()->setInt("mp.delay.ms", 2000);
  // tdp udp
  // mp->getOption()->setString("io.rtsp.transport", "udp");
  mp->getOption()->setInt("io.timeout.ms", 15000);
  // 日志打印
  // mp->getOption()->setBool("log.source.packet", true);
  // mp->getOption()->setBool("log.decoder.frame", true);
  // mp->getOption()->setBool("log.render.frame", true);
  // mp->getSurfaceRender()->setVulkan(false);
  mp->getSurfaceRender()->setSurface(nullptr);
  // mp->getSurfaceRender()->enableYuvOut(YuvType::yuv420P);
  // mp->getSurfaceRender()->setOffSurface(YuvType::yuv420P);
  // mp->open("rtsp://127.0.0.1/live/test");
  // mp->open("rtsp://192.168.1.100:554/live/0123456789ab_0");
  mp->setIoPlan(IoPlan::ffmpeg);
  mp->open("D://Back//美好_h265.mp4");
  // mp->setIoPlan(IoPlan::zlmediakit);
  // mp->open("D://Back//2.mp4");
  // 多播放器
  // mp1 = createMediaPlayer();
  // mp1->getOption()->setBool("log.source.packet", true);
  // mp1->setIoPlan(IoPlan::ffmpeg);
  // mp1->open("D://Back//美好_h265.mp4");
  //  mp->open("D://Back/testh265.mkv");
  //  mp->open("rtsp://127.0.0.1/live/test");
  // mp->open("rtsp://192.168.1.100/live/test");
  // mp->open("D://27-01_225.mp4");
  // mp->open("D://Back/新鸳鸯蝴蝶梦-黄安-58124.aac");
  // mp->open("D://Back/中国人-刘德华-148755.aac");
  // mp->speed(2.0);
  // mp->open("rtsp://127.0.0.1/live/test");
  // 单声道
  // mp->open("D://Back/a2_mono_16000.aac");
  // mp->setHardDecode(false);

  // mp->speed(4.0);
  // mp->open("rtsp://192.168.1.100/live/test");
  // mp->open("D://2.mp4");
  // std::thread th([&]() {
  //   std::this_thread::sleep_for(std::chrono::seconds(3));
  //   // D://1.mp4
  //   // rtsp://127.0.0.1/live/test
  //   muxer->setMuxerType(MuxerType::ffmpeg);
  //   muxer->open("D://1.mp4");
  //   std::this_thread::sleep_for(std::chrono::seconds(10));
  //   muxer->close();
  // });
  bool m_running = true;
  MSG msg;
  while (m_running) {
    // 处理所有待处理的消息
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        m_running = false;
        break;
      }
      if (msg.message == WM_KEYDOWN) {
        // 获取虚拟键码
        int virtualKey = (int)msg.wParam;
        log(LogLevel::info, "virtualKey:", (char)virtualKey);
        switch (virtualKey) {
          case 'A': {
            mp->speed(8);
            break;
          }
          case 'S': {
            mp->speed(4);
            break;
          }
          case 'D': {
            mp->speed(1);
            break;
          }
          case 'F': {
            mp->speed(0.5);
            break;
          }
          case 'G': {
            mp->speed(16);
            break;
          }
          case 'P': {
            IImageBuffer* shotBuf = createImageBuffer();
            if (shotBuf && mp->getSurfaceRender()->screenShot(shotBuf)) {
              saveImagePath("D://screenshot.png", shotBuf);
              log(LogLevel::info, "screenshot saved: screenshot.png");
            } else {
              log(LogLevel::warn, "screenshot failed");
            }
            if (shotBuf) {
              delete shotBuf;
            }
            break;
          }
          case VK_LEFT: {
            int64_t pos = mp->getPosition();
            mp->seek(pos - 10000);
            log(LogLevel::info, "seek backward 10s, pos:", pos,
                " to:", pos - 10000);
            break;
          }
          case VK_RIGHT: {
            int64_t pos = mp->getPosition();
            mp->seek(pos + 10000);
            log(LogLevel::info, "seek forward 10s, pos:", pos,
                " to:", pos + 10000);
            break;
          }
          default:
            break;
        }
      }
      // log(LogLevel::info, "now renderPts:", mp->getPosition());
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
