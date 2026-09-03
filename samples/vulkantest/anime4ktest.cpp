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
ISurfaceRender* wr = nullptr;
bool bAnime4KEnabled = false;
Anime4KParamet animePar = {};
#ifdef AVOX_ENABLE_FREETYPE
IFontLayer* fontLayer = nullptr;
#endif

#ifdef AVOX_ENABLE_FREETYPE
void updateFontTip() {
  if (!fontLayer) return;
  if (!bAnime4KEnabled) {
    fontLayer->drawText("Anime4K OFF  [S]ModeA [D]ModeC [W]ModeB [Q]A+L");
  } else {
    const char* modeStr = "A";
    if (animePar.mode == Anime4KMode::ModeB) modeStr = "B";
    if (animePar.mode == Anime4KMode::ModeC) modeStr = "C";
    const char* variantStr = "M";
    if (animePar.variant == Anime4KVariant::S) variantStr = "S";
    if (animePar.variant == Anime4KVariant::L) variantStr = "L";
    char tip[128];
    snprintf(tip, sizeof(tip), "Anime4K Mode%c+%c str=%.1f clamp=%d  [A]OFF",
             modeStr[0], variantStr[0], animePar.strength,
             animePar.enableClampHighlights);
    fontLayer->drawText(tip);
  }
}
#endif

int main() {
  mp = createMediaPlayer();
  wr = mp->getSurfaceRender();
  wr->setSurface(nullptr);
  mp->setHardDecode(false);
  mp->getOption()->setNumber("mp.lowlatency.speed", 1.2);
  mp->getOption()->setInt("mp.delay.ms", 2500);
  mp->getOption()->setInt("io.timeout.ms", 10000);
  mp->setIoPlan(IoPlan::ffmpeg);
  // wr->enableSizeScale(0.5);
  // 0101.mkv 美好.mp4
  mp->open("D://Back//美好.mp4");
#ifdef AVOX_ENABLE_FREETYPE
  fontLayer = enableRenderFont(mp->getSurfaceRender());
  fontLayer->setColor(1.0f, 0.0f, 0.0f, 0.5f); 
  updateFontTip();
#endif

  bool m_running = true;
  MSG msg;
  while (m_running) {
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        m_running = false;
        break;
      }
      if (msg.message == WM_KEYDOWN) {
        int virtualKey = (int)msg.wParam;
        log(LogLevel::info, "virtualKey:", (char)virtualKey);
        switch (virtualKey) {
          // A: disable Anime4K
          case 'A': {
            if (bAnime4KEnabled) {
              wr->disableAnime4K();
              bAnime4KEnabled = false;
              log(LogLevel::info, "Anime4K disabled");
            }
            break;
          }
          // S: enable Anime4K Mode A (Restore + Upscale + Clamp)
          case 'S': {
            animePar = {};
            animePar.mode = Anime4KMode::ModeA;
            animePar.enableClampHighlights = true;
            wr->enableAnime4K(animePar);
            bAnime4KEnabled = true;
            log(LogLevel::info, "Anime4K ModeA enabled");
            break;
          }
          // D: enable Anime4K Mode C (Upscale only)
          case 'D': {
            animePar = {};
            animePar.mode = Anime4KMode::ModeC;
            animePar.enableClampHighlights = false;
            wr->enableAnime4K(animePar);
            bAnime4KEnabled = true;
            log(LogLevel::info, "Anime4K ModeC enabled");
            break;
          }
          // Q: enable Anime4K Mode A + Variant L (high quality)
          case 'Q': {
            animePar = {};
            animePar.mode = Anime4KMode::ModeA;
            animePar.variant = Anime4KVariant::L;
            animePar.enableClampHighlights = true;
            wr->enableAnime4K(animePar);
            bAnime4KEnabled = true;
            log(LogLevel::info, "Anime4K ModeA+L enabled");
            break;
          }
          // W: enable Anime4K Mode B (Restore_Soft + Upscale)
          case 'W': {
            animePar = {};
            animePar.mode = Anime4KMode::ModeB;
            animePar.enableClampHighlights = true;
            wr->enableAnime4K(animePar);
            bAnime4KEnabled = true;
            log(LogLevel::info, "Anime4K ModeB enabled");
            break;
          }
          // F: toggle Clamp Highlights
          case 'F': {
            if (bAnime4KEnabled) {
              animePar.enableClampHighlights = !animePar.enableClampHighlights;
              wr->enableAnime4K(animePar);
              log(LogLevel::info,
                  "Anime4K clampHighlights:", animePar.enableClampHighlights);
            }
            break;
          }
          // G: increase Restore strength
          case 'G': {
            if (bAnime4KEnabled) {
              animePar.strength = std::min(animePar.strength + 0.1f, 2.0f);
              wr->enableAnime4K(animePar);
              log(LogLevel::info, "Anime4K strength:", animePar.strength);
            }
            break;
          }
          // H: decrease Restore strength
          case 'H': {
            if (bAnime4KEnabled) {
              animePar.strength = std::max(animePar.strength - 0.1f, 0.0f);
              wr->enableAnime4K(animePar);
              log(LogLevel::info, "Anime4K strength:", animePar.strength);
            }
            break;
          }
          case 'Z': {
            fontLayer = enableRenderFont(mp->getSurfaceRender());
  fontLayer->setFont("simhei.ttf", 16);
            log(LogLevel::info, "Font layer enabled");
            break;
          }
          case 'X': {
            disableRenderFont(mp->getSurfaceRender());
            break;
          }
          default:
            break;
        }
#ifdef AVOX_ENABLE_FREETYPE
        updateFontTip();
#endif
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  mp->close();
  return 0;
}
