#include <thread>

#include "avox/Avox.hpp"
#include "avox/AvoxSource.h"
#include "avox_vulkan/VkTemplate.hpp"
#include "avox_windows/WinCommon.hpp"

#ifdef AVOX_ENABLE_FREETYPE
#include "avox_freetype/FreetypeExport.h"
#endif

#ifdef AVOX_ENABLE_SHERPA
#include "avox_sherpa/SherpaExport.h"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#include "avox/module/LogHelper.hpp"

using namespace avox;

ISourcePlayer* sp = nullptr;
IMediaMuxer* muxer = nullptr;
IFontLayer* fontLayer = nullptr;
ISubtitle* subtitle = nullptr;

int main(int argc, char* argv[]) {
  sp = createDevicePlayer();
  muxer = sp->getMuxer();
  IAudioManager* audioMgr = getAudioManager(ADeviceSdk::wasapi);
  IVideoManager* videoMgr = getVideoManager(VDeviceSdk::win_capture);
  int32_t count = videoMgr->getDeviceCount();
  int32_t vIndex = 0;
  for (int32_t i = 0; i < count; i++) {
    std::string name = videoMgr->getDevice(i)->getDeviceName();
    if (name.find("Visual Studio Code") != std::string::npos ||
        name.find("OBS") != std::string::npos) {
      vIndex = i;
      break;
      log(LogLevel::info, "select video device: ", name);
    }
  }
  sp->setAudioSource(audioMgr->getDevice(0));
  sp->setVideoSource(videoMgr->getDevice(vIndex));
  sp->getSurfaceRender()->setVulkan(true);
  sp->getSurfaceRender()->setSurface(nullptr);
  subtitle = sp->getSubtitle();
  subtitle->enableAsr();
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
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  sp->close();
  return 0;
}