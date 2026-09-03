#include <thread>

#include "avox/AvoxPlayer.h"
#include "avox_vulkan/VkExport.h"
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

// 基础图像调整(色调/亮度/对比度/饱和度/伽玛) + 锐度 参数
bool bBasicAdjustEnabled = false;
bool bSharpenEnabled = false;
float hueValue = 0.0f;           // -360 ~ 360
float brightnessValue = 0.0f;    // -1.0 ~ 1.0
float contrastValue = 1.0f;      // 0.0 ~ 2.0
float saturationValue = 1.0f;    // 0.0 ~ 2.0
float gammaValue = 1.0f;         // 0.0 ~ 3.0
SharpenVideo sharpenParamet = {};

// 几何叠加层(线/矩形/点/圆) - 阈值控宽: 低=宽, 高=细
IGeometryLayer* geoLayer = nullptr;
bool bGeometryEnabled = false;
float geoThreshold = 0.5f;

// 由当前各调整值组装 BasicAdjustParamet
BasicAdjustParamet makeBasicAdjustParamet() {
  BasicAdjustParamet p;
  p.hue = hueValue;
  p.brightness = brightnessValue;
  p.contrast = contrastValue;
  p.saturation = saturationValue;
  p.gamma = gammaValue;
  return p;
}

#ifdef AVOX_ENABLE_FREETYPE
IFontLayer* fontLayer = nullptr;

void updateFontTip() {
  if (!fontLayer) return;
  char tip[256];
  snprintf(tip, sizeof(tip),
           "BA[%c] H:%.0f B:%.2f C:%.2f S:%.2f G:%.2f Sh[%c]:%.2f Geo[%c] t:%.2f",
           bBasicAdjustEnabled ? 'Y' : 'N', hueValue, brightnessValue,
           contrastValue, saturationValue, gammaValue,
           bSharpenEnabled ? 'Y' : 'N', sharpenParamet.sharpness,
           bGeometryEnabled ? 'Y' : 'N', geoThreshold);
  fontLayer->drawText(tip);
}
#endif

// 画一组演示图元（外框 + 中心十字 + 中心点 + 中心圆环 + 右上实心圆）
void drawGeometryDemo() {
  if (!geoLayer) return;
  geoLayer->setColor(0.0f, 1.0f, 0.0f);            // 绿色
  geoLayer->setThreshold(geoThreshold);
  geoLayer->clear();
  geoLayer->drawRect(0.2f, 0.2f, 0.8f, 0.8f);      // 外框
  geoLayer->drawLine(0.0f, 0.5f, 1.0f, 0.5f);      // 水平中线
  geoLayer->drawLine(0.5f, 0.0f, 0.5f, 1.0f);      // 垂直中线
  geoLayer->drawPoint(0.5f, 0.5f, 10.0f);          // 中心点
  geoLayer->drawCircle(0.5f, 0.5f, 80.0f);         // 中心圆环
  geoLayer->drawCircle(0.85f, 0.15f, 50.0f, true); // 右上实心圆
}

int main() {
  mp = createMediaPlayer();
  wr = mp->getSurfaceRender();
  wr->setSurface(nullptr);
  mp->setHardDecode(false);
  mp->getOption()->setNumber("mp.lowlatency.speed", 1.2);
  mp->getOption()->setInt("mp.delay.ms", 2500);
  mp->getOption()->setInt("io.timeout.ms", 10000);
  mp->setIoPlan(IoPlan::ffmpeg);
  mp->open("D://Back//美好.mp4");

#ifdef AVOX_ENABLE_FREETYPE
  fontLayer = enableRenderFont(mp->getSurfaceRender());
  fontLayer->setFont("simhei.ttf", 16);
  fontLayer->setColor(1.0f, 0.0f, 0.0f, 0.5f);
  fontLayer->setScale(2.0f);
  updateFontTip();
#endif

  // 几何叠加层：开启并画演示图元（V 切换, N/M 调线宽）
  geoLayer = enableRenderGeometry(mp->getSurfaceRender());
  drawGeometryDemo();
  bGeometryEnabled = true;

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
          // G: toggle 整组基础调整(色调/亮度/对比度/饱和度/伽玛)
          case 'G': {
            if (bBasicAdjustEnabled) {
              wr->disableBasicAdjust();
              bBasicAdjustEnabled = false;
              log(LogLevel::info, "BasicAdjust disabled");
            } else {
              wr->enableBasicAdjust(makeBasicAdjustParamet());
              bBasicAdjustEnabled = true;
              log(LogLevel::info, "BasicAdjust enabled");
            }
            break;
          }
          // Q/W: 亮度 -/+
          case 'Q': {
            brightnessValue = std::max(brightnessValue - 0.1f, -1.0f);
            if (bBasicAdjustEnabled) { wr->enableBasicAdjust(makeBasicAdjustParamet()); }
            log(LogLevel::info, "Brightness:", brightnessValue);
            break;
          }
          case 'W': {
            brightnessValue = std::min(brightnessValue + 0.1f, 1.0f);
            if (bBasicAdjustEnabled) { wr->enableBasicAdjust(makeBasicAdjustParamet()); }
            log(LogLevel::info, "Brightness:", brightnessValue);
            break;
          }
          // A/S: 对比度 -/+
          case 'A': {
            contrastValue = std::max(contrastValue - 0.1f, 0.0f);
            if (bBasicAdjustEnabled) { wr->enableBasicAdjust(makeBasicAdjustParamet()); }
            log(LogLevel::info, "Contrast:", contrastValue);
            break;
          }
          case 'S': {
            contrastValue = std::min(contrastValue + 0.1f, 2.0f);
            if (bBasicAdjustEnabled) { wr->enableBasicAdjust(makeBasicAdjustParamet()); }
            log(LogLevel::info, "Contrast:", contrastValue);
            break;
          }
          // Z/X: 饱和度 -/+
          case 'Z': {
            saturationValue = std::max(saturationValue - 0.1f, 0.0f);
            if (bBasicAdjustEnabled) { wr->enableBasicAdjust(makeBasicAdjustParamet()); }
            log(LogLevel::info, "Saturation:", saturationValue);
            break;
          }
          case 'X': {
            saturationValue = std::min(saturationValue + 0.1f, 2.0f);
            if (bBasicAdjustEnabled) { wr->enableBasicAdjust(makeBasicAdjustParamet()); }
            log(LogLevel::info, "Saturation:", saturationValue);
            break;
          }
          // H/J: 色调 -/+
          case 'H': {
            hueValue = hueValue - 15.0f;
            if (bBasicAdjustEnabled) { wr->enableBasicAdjust(makeBasicAdjustParamet()); }
            log(LogLevel::info, "Hue:", hueValue);
            break;
          }
          case 'J': {
            hueValue = hueValue + 15.0f;
            if (bBasicAdjustEnabled) { wr->enableBasicAdjust(makeBasicAdjustParamet()); }
            log(LogLevel::info, "Hue:", hueValue);
            break;
          }
          // T/Y: 伽玛 -/+
          case 'T': {
            gammaValue = std::max(gammaValue - 0.1f, 0.0f);
            if (bBasicAdjustEnabled) { wr->enableBasicAdjust(makeBasicAdjustParamet()); }
            log(LogLevel::info, "Gamma:", gammaValue);
            break;
          }
          case 'Y': {
            gammaValue = std::min(gammaValue + 0.1f, 3.0f);
            if (bBasicAdjustEnabled) { wr->enableBasicAdjust(makeBasicAdjustParamet()); }
            log(LogLevel::info, "Gamma:", gammaValue);
            break;
          }
          // 4: toggle sharpen
          case '4': {
            if (bSharpenEnabled) {
              wr->disableSharpen();
              bSharpenEnabled = false;
              log(LogLevel::info, "Sharpen disabled");
            } else {
              wr->updateSharpen(sharpenParamet);
              bSharpenEnabled = true;
              log(LogLevel::info, "Sharpen enabled:", sharpenParamet.sharpness);
            }
            break;
          }
          // E/R: decrease/increase sharpen
          case 'E': {
            sharpenParamet.sharpness = std::max(sharpenParamet.sharpness - 0.5f, -4.0f);
            if (bSharpenEnabled) {
              wr->updateSharpen(sharpenParamet);
            }
            log(LogLevel::info, "Sharpen:", sharpenParamet.sharpness);
            break;
          }
          case 'R': {
            sharpenParamet.sharpness = std::min(sharpenParamet.sharpness + 0.5f, 4.0f);
            if (bSharpenEnabled) {
              wr->updateSharpen(sharpenParamet);
            }
            log(LogLevel::info, "Sharpen:", sharpenParamet.sharpness);
            break;
          }
          // D: toggle font layer
          case 'D': {
            fontLayer = enableRenderFont(mp->getSurfaceRender());
  fontLayer->setFont("simhei.ttf", 16);
            log(LogLevel::info, "Font layer enabled");
            break;
          }
          case 'F': {
            disableRenderFont(mp->getSurfaceRender());
            break;
          }
          // V: toggle 几何叠加层
          case 'V': {
            if (bGeometryEnabled) {
              disableRenderGeometry(mp->getSurfaceRender());
              bGeometryEnabled = false;
              log(LogLevel::info, "Geometry disabled");
            } else {
              geoLayer = enableRenderGeometry(mp->getSurfaceRender());
              drawGeometryDemo();
              bGeometryEnabled = true;
              log(LogLevel::info, "Geometry enabled, threshold:", geoThreshold);
            }
            break;
          }
          // N/M: 阈值 -/+（低=宽, 高=细）
          case 'N': {
            geoThreshold = std::max(geoThreshold - 0.05f, 0.05f);
            if (geoLayer) { geoLayer->setThreshold(geoThreshold); }
            log(LogLevel::info, "Geo threshold (wider):", geoThreshold);
            break;
          }
          case 'M': {
            geoThreshold = std::min(geoThreshold + 0.05f, 0.95f);
            if (geoLayer) { geoLayer->setThreshold(geoThreshold); }
            log(LogLevel::info, "Geo threshold (thinner):", geoThreshold);
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
