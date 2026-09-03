#include <random>
#include <string>
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

#ifdef AVOX_ENABLE_FREETYPE
#include "avox_freetype/FreetypeExport.h"
#endif

#ifdef _WIN32
#include <windows.h>

#include "avox_windows/WinCommon.hpp"
#endif

using namespace avox;

class TextRender : public ISurfaceRenderOb {
 public:
  TextRender(ISurfaceRender* render_) : render(render_) {};
  virtual ~TextRender() = default;

 private:
  ISurfaceRender* render = nullptr;
  std::string charPool =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  std::random_device rd;
#if AVOX_ENABLE_FREETYPE
  IFontLayer* fontLayer = nullptr;
#endif

 public:
  virtual void onSurface() override {
#if AVOX_ENABLE_FREETYPE
    fontLayer = enableRenderFont(render);
    fontLayer->setFont("simhei.ttf", 30);
    fontLayer->drawText("Hello, Vulkan 字体渲染测试12345!");
    fontLayer->setColor(1, 0, 0, 0);
#endif
  }
  virtual void onRender() override {
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, charPool.size() - 1);
    std::string randomText = "";
    for (int i = 0; i < 10; ++i) {
      randomText += charPool[dis(gen)];
    }
    // log(LogLevel::info, "randomText: ", randomText);
    #if AVOX_ENABLE_FREETYPE
    fontLayer->drawText(randomText.c_str());
    #endif
  }
};

int main(int argc, char* argv[]) {
  IRtcPlayer* sp = createWebRtcPlayer();
  sp->setRollType(RtcRollType::offer);
  const char* url =
      "http://127.0.0.1/index/api/webrtc?app=live&stream=test&type=push";
  ISdpAgentOb* sdpOb = createZlTestSdpAgent(sp, url);
  sp->setSdpAgentOb(sdpOb);
  IAudioManager* audioMgr = getAudioManager(ADeviceSdk::wasapi);
  IVideoManager* videoMgr = getVideoManager(VDeviceSdk::win_capture);
  int32_t count = videoMgr->getDeviceCount();
  int32_t vIndex = 1;
  for (int32_t i = 0; i < count; i++) {
    std::string name = videoMgr->getDevice(i)->getDeviceName();
    if (name.find("RenderDoc") != std::string::npos) {
      vIndex = i;
      break;
    }
    log(LogLevel::info, "video device: ", name);
  }
  sp->setAudioSource(audioMgr->getDevice(0));
  sp->setVideoSource(videoMgr->getDevice(vIndex));
#if AVOX_ENABLE_FREETYPE
  TextRender* textRender = new TextRender(sp->getLocalSurfaceRender());
  addSurfaceRenderOb(sp->getLocalSurfaceRender(), textRender);
#endif
  sp->getLocalSurfaceRender()->setSurface(nullptr);
  sp->getRemoteSurfaceRender()->setSurface(nullptr);
  sp->open();
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
