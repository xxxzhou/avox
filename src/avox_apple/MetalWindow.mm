#include "MetalWindow.hpp"
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#else
#import <UIKit/UIKit.h>
#endif
#include <iostream>

namespace avox {

std::atomic<bool> metalHdrPassthrough{false};

MetalWindow::MetalWindow() { renderType = RenderType::Metal; }

MetalWindow::~MetalWindow() {}

void MetalWindow::initSurface(void* surface) {
  hostObject = surface;
  Window::initSurface(surface);
}

void MetalWindow::onChangeSize() {
  if (wdWidth == 0 || wdHeight == 0) {
    LOGFLF(LogLevel::info, "invalid window size");
  }
}

#if TARGET_OS_OSX
// 潜在 EDR 头距: 优先宿主视图所在屏, 回落主屏; <=1 按 SDR
static float edrHeadroom(void* hostObject) {
  if (@available(macOS 10.15, *)) {
    NSScreen* screen = nil;
    id obj = (__bridge id)hostObject;
    if ([obj isKindOfClass:[NSView class]]) {
      screen = ((NSView*)obj).window.screen;
    }
    if (!screen) screen = NSScreen.mainScreen;
    if (screen) {
      return (float)screen.maximumPotentialExtendedDynamicRangeColorComponentValue;
    }
  }
  return 1.0f;
}

bool MetalWindow::setHdrPassthrough(bool bPassthrough) {
  if (!bPassthrough) {
    metalHdrPassthrough = false;
    return true;
  }
  const float headroom = edrHeadroom(hostObject);
  LOGFLF(LogLevel::info, "setHdrPassthrough on, edr headroom:", headroom);
  if (headroom <= 1.0f) {
    return false;  // SDR 屏: 层保持 SDR, 色彩正确性由 tone map 承担
  }
  metalHdrPassthrough = true;
  return true;
}
#else
// iOS 腿 EDR(10/3 打通 HDR 原生播放): iOS 16+ UIScreen.potentialEDRHeadroom
// 探测, 与 mac NSScreen 同口径(headroom>1 受理直通); iOS 15 及以下恒不受理
// (SDR 层, 正确性由 tone map 承担)。
bool MetalWindow::setHdrPassthrough(bool bPassthrough) {
  if (!bPassthrough) {
    metalHdrPassthrough = false;
    return true;
  }
  float headroom = 1.0f;
  if (@available(iOS 16.0, *)) {
    headroom = (float)UIScreen.mainScreen.potentialEDRHeadroom;
  }
  LOGFLF(LogLevel::info, "setHdrPassthrough on, edr headroom:", headroom);
  if (headroom <= 1.0f) {
    return false;  // SDR 屏: 层保持 SDR, 色彩正确性由 tone map 承担
  }
  metalHdrPassthrough = true;
  return true;
}
#endif

}
