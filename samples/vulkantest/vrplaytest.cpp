// VR播放样例: 鱼眼/全景立体片源在平面屏上观看
// 自动猜投影格式(guessVrParamet), 鼠标拖动转视角, 滚轮变焦,
// 双击/R复位视角, 1/2/3切输出模式(单眼/红蓝3D/SBS预览)
// 用法: vrplaytest [url]  (默认源改 kDefaultUrl)

#include <algorithm>

#include <thread>

#include "avox/AvoxPlayer.h"
#include "avox_vulkan/VkTemplate.hpp"
#include "avox_windows/WinExport.h"

#ifdef _WIN32
#include <windows.h>

#include "avox_windows/WinCommon.hpp"
#endif

#include "avox/Avox.hpp"

using namespace avox;

static const char* kDefaultUrl = "D://Back//vr180_8k_sbs.mp4";
// 拖动灵敏度(度/像素): 抓取式, 画面跟随鼠标
static const float kDragDegPerPixel = 0.12f;
// 滚轮每齿视野变化(度)
static const float kWheelFovStep = 5.0f;

static IMediaPlayer* mp = nullptr;
static bool bDragging = false;
static POINT lastPt = {};
// 立体强度(度), '-'/'=' 调节, anaglyph/sbs 模式下生效
static float s_stereo = 0.0f;

// 播完回绕: EOF后渲染循环停走, 视角/模式交互会失去即时生效的帧载体;
// seek(0)后须resume, 否则播放停在暂停态不出帧
struct LoopOb : IMediaPlayerOb {
  void onComplete() override {
    if (mp) {
      mp->seek(0);
      mp->resume();
    }
  }
};
static LoopOb s_loopOb;

static void logViewAngles() {
  float yaw = 0.0f;
  float pitch = 0.0f;
  float fov = 0.0f;
  mp->getSurfaceRender()->getViewAngles(&yaw, &pitch, &fov);
  log(LogLevel::info, "view yaw:", yaw, " pitch:", pitch, " fov:", fov);
}

int main(int argc, char** argv) {
  const char* url = argc > 1 ? argv[1] : kDefaultUrl;
  mp = createMediaPlayer();
  addMediaPlayerOb(mp, &s_loopOb);
  mp->setHardDecode(true);
  mp->getOption()->setInt("io.timeout.ms", 15000);
  ISurfaceRender* render = mp->getSurfaceRender();
  // 自动建Vulkan窗口
  render->setSurface(nullptr);
  mp->setIoPlan(IoPlan::ffmpeg);
  mp->open(url);
  // 等媒体信息就绪后按宽高比自动猜投影格式(判错可改用手选参数)
  VrParamet vrParamet = {};
  for (int i = 0; i < 100; i++) {
    ISourceInfo* info = mp->getSourceInfo();
    if (info && info->videoSize() > 0) {
      VTrackDesc track = info->getVideoDesc(0);
      if (guessVrParamet(track.desc.width, track.desc.height, &vrParamet)) {
        log(LogLevel::info, "guess VR proj:", (int32_t)vrParamet.projection,
            " layout:", (int32_t)vrParamet.eyeLayout, " src:",
            track.desc.width, "x", track.desc.height);
      }
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  render->enableVr(vrParamet);

  bool m_running = true;
  MSG msg;
  while (m_running) {
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        m_running = false;
        break;
      }
      switch (msg.message) {
        case WM_LBUTTONDOWN: {
          bDragging = true;
          lastPt.x = (short)LOWORD(msg.lParam);
          lastPt.y = (short)HIWORD(msg.lParam);
          break;
        }
        case WM_LBUTTONUP: {
          bDragging = false;
          break;
        }
        case WM_MOUSEMOVE: {
          if (bDragging) {
            POINT pt = {(short)LOWORD(msg.lParam), (short)HIWORD(msg.lParam)};
            render->rotateView((float)(lastPt.x - pt.x) * kDragDegPerPixel,
                               (float)(pt.y - lastPt.y) * kDragDegPerPixel);
            lastPt = pt;
          }
          break;
        }
        case WM_MOUSEWHEEL: {
          float delta = GET_WHEEL_DELTA_WPARAM(msg.wParam) > 0
                            ? -kWheelFovStep
                            : kWheelFovStep;
          render->zoomView(delta);
          logViewAngles();
          break;
        }
        case WM_LBUTTONDBLCLK: {
          render->resetView();
          logViewAngles();
          break;
        }
        case WM_KEYDOWN: {
          int virtualKey = (int)msg.wParam;
          switch (virtualKey) {
            case '1': {
              render->setVrOutMode(VrOutMode::mono);
              log(LogLevel::info, "vr out mode: mono");
              break;
            }
            case '2': {
              render->setVrOutMode(VrOutMode::anaglyph);
              log(LogLevel::info, "vr out mode: anaglyph");
              break;
            }
            case '3': {
              render->setVrOutMode(VrOutMode::sbsPreview);
              log(LogLevel::info, "vr out mode: sbs preview");
              break;
            }
            case VK_OEM_MINUS: {  // '-' 键(VK非ASCII码)
              s_stereo = std::max(s_stereo - 0.25f, 0.0f);
              render->setVrStereoStrength(s_stereo);
              log(LogLevel::info, "vr stereo strength:", s_stereo, "deg");
              break;
            }
            case VK_OEM_PLUS: {  // '=' 键
              s_stereo = std::min(s_stereo + 0.25f, 5.0f);
              render->setVrStereoStrength(s_stereo);
              log(LogLevel::info, "vr stereo strength:", s_stereo, "deg");
              break;
            }
            case 'R': {
              render->resetView();
              logViewAngles();
              break;
            }
            case VK_ESCAPE: {
              m_running = false;
              break;
            }
            default:
              break;
          }
          break;
        }
        default:
          break;
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  mp->close();
  return 0;
}
