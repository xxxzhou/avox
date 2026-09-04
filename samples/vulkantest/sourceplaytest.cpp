#include <thread>

#include "avox/Avox.hpp"
#include "avox/AvoxSource.h"
#include "avox/module/LogHelper.hpp"
#include "avox_freetype/FreetypeExport.h"
#include "avox_vulkan/VkTemplate.hpp"

#ifdef _WIN32
#include <windows.h>

#include "avox_windows/WinCommon.hpp"
#endif
#if AVOX_ENABLE_ZLMEDIAKIT
#include "avox_zlmediakit/ZlmExport.h"
#endif

using namespace avox;

ISourcePlayer* sp = nullptr;
IRawSource* source = nullptr;
IMediaMuxer* muxer = nullptr;

int main(int argc, char* argv[]) {
  sp = createDevicePlayer();
  muxer = sp->getMuxer();
  IAudioManager* audioMgr = getAudioManager(ADeviceSdk::wasapi);
  // 用法: sourceplaytest [win_capture|win_mf|win_decklink] [设备索引], 默认win_capture窗口采集
  VDeviceSdk sdk = VDeviceSdk::win_capture;
  if (argc > 1 && std::string(argv[1]) == "win_mf") {
    sdk = VDeviceSdk::win_mf;
  }
  if (argc > 1 && std::string(argv[1]) == "win_decklink") {
    sdk = VDeviceSdk::win_decklink;
  }
  IVideoManager* videoMgr = getVideoManager(sdk);
  int32_t count = videoMgr->getDeviceCount();
  log(LogLevel::info, "video device count:", count, " sdk:", (int)sdk);
  int32_t vIndex = 0;
  if (argc > 2) {
    vIndex = atoi(argv[2]);
  } else if (sdk == VDeviceSdk::win_capture) {
    vIndex = 1;
    for (int32_t i = 0; i < count; i++) {
      // Visual Studio Code/RenderDoc
      std::string name = videoMgr->getDevice(i)->getDeviceName();
      if (name.find("微信") != std::string::npos) {
        vIndex = i;
        break;
      }
    }
  }
  if (vIndex >= count) {
    vIndex = 0;
  }
  if (count > 0) {
    log(LogLevel::info, "select video device:",
        videoMgr->getDevice(vIndex)->getDeviceName());
  }
  sp->setAudioSource(audioMgr->getDevice(0));
  sp->setVideoSource(count > 0 ? videoMgr->getDevice(vIndex) : nullptr);
  sp->getSurfaceRender()->setVulkan(true);
  sp->getSurfaceRender()->setSurface(nullptr);
  sp->open();
  std::thread th([&]() {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    // D://1.mp4
    // rtsp://127.0.0.1/live/test
    // rtsp://192.168.1.100/live/test
    const char* url = "D://out2.mp4";
    // const char* url = "rtsp://127.0.0.1/live/test";
    ATrackDesc adesc = {};
    // 检查会把usetoken用了,所以需要二个链接
    bool bOnvif = false;  // checkOnvif(url, &adesc);
    muxer->setHardEncode(false);
    muxer->setVideoCodec(VCodecId::h264);
    // ACodecId::g711a ACodecId::aac
    muxer->setAudioCodec(ACodecId::g711a);
    muxer->setAudioDesc({1, AudioFormat::AVOX_AUDIO_S16, 8000});
    log(LogLevel::info, "onvif:", bOnvif);
    if (bOnvif) {
      muxer->setMuxerType(MuxerType::onvif);
    } else {
      muxer->setMuxerType(MuxerType::ffmpeg);
    }
    muxer->open(url);
    // muxer->open("rtsp://127.0.0.1/live/test");
    // muxer->open("D://1.mp4");
    std::this_thread::sleep_for(std::chrono::seconds(10));
    muxer->close();
  });
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
      if (msg.message == WM_KEYDOWN) {
        // 获取虚拟键码
        int virtualKey = (int)msg.wParam;
        log(LogLevel::info, "virtualKey:", (char)virtualKey);
        switch (virtualKey) {
          case 'A': {
            muxer->close();
            sp->close();
            sp->open();
            break;
          }
          default:
            break;
        }
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    // std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  // th.join();
  sp->close();
  return 0;
}
