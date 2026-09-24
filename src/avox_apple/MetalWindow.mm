#include "MetalWindow.hpp"
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
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
  if (edrHeadroom(hostObject) <= 1.0f) {
    return false;  // SDR 屏: 层保持 SDR, 色彩正确性由 tone map 承担
  }
  metalHdrPassthrough = true;
  return true;
}
#else
// iOS 腿 EDR 未做(无 NSScreen, 探测口径另批): 恒不受理
bool MetalWindow::setHdrPassthrough(bool bPassthrough) {
  if (!bPassthrough) metalHdrPassthrough = false;
  return false;
}
#endif

}
