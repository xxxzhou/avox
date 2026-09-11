#include "Window.hpp"

#include <algorithm>

#ifdef __ANDROID__
#include <android/native_window.h>
#endif
#ifdef __APPLE__
// CAMetalLayer/CALayer(contentsScale/bounds) 两平台都在 QuartzCore
#import <QuartzCore/QuartzCore.h>
#endif
#if _WIN32
#include <windows.h>
#endif

namespace avox {

Window::Window() {
#ifdef WIN32
  renderType = RenderType::D3D11;
#endif
#ifdef __ANDROID__
  renderType = RenderType::OpenGLES;
#endif
#ifdef __APPLE__
  renderType = RenderType::Metal;
#endif
}

Window::~Window() { close(); }

IRenderContext* Window::getRenderContext() { return nullptr; }

void Window::initSurface(void* surface_) {
  if (surface != surface_) {
    close();
  }
#if WIN32
  // 传入窗口句柄，由onInitWin生成surface
  hwnd = (AvoxSurfaceType)surface_;
  if (hwnd) {
    RECT rect;
    if (GetClientRect(hwnd, &rect)) {
      wdWidth = rect.right - rect.left;
      wdHeight = rect.bottom - rect.top;
    }
  }
  if (wdWidth == 0 || wdHeight == 0) {
    wdWidth = 1280;
    wdHeight = 720;
  }
#else
  surface = getNativeSurface(surface_);
#ifdef __ANDROID__
  // 宿主传入的是借出的 ANativeWindow (fromSurface), close() 会 release;
  // 这里 acquire 配平, 否则 close 后引用计数归零解绑窗口, 下一个持有同指针的
  // 播放器 initVkSurface 时 loader 空指针崩溃 (矩阵 APK 连续用例实测)
  if (surface) {
    ANativeWindow_acquire(surface);
  }
#endif
#endif
  // windows平台会由hwnd创建surface
  // 后面渲染统一使用surface
  onInitWin();
}

bool Window::updateSize() {
  // 检查suface有效性，并根据suface得到最新
  if (!surface) {
    wdHeight = 0;
    wdWidth = 0;
    return false;
  }
  int32_t preWidth = wdWidth;
  int32_t preHeight = wdHeight;
  getSurfaceSize(surface, wdWidth, wdHeight);
  if (wdWidth < 0 || wdHeight < 0) {
    wdWidth = 0;
    wdHeight = 0;
  }
  if (preWidth != wdWidth || preHeight != wdHeight) {
    log(LogLevel::info, "Window size changed, old size:(", preWidth, "-",
        preHeight, "), new size:(", wdWidth, "-", wdHeight, ")");

    // 更新窗口大小
    onChangeSize();
    return true;
  }
  return false;
}

RenderType Window::getRenderType() {
  if (renderType != RenderType::other) {
    return renderType;
  }
  IRenderContext* rcontext = getRenderContext();
  if (!rcontext) {
    return RenderType::other;
  }
  return rcontext->getRenderType();
}

bool Window::onPreTick() { return wdWidth > 0 && wdHeight > 0; }

bool Window::preTick() {
  // 窗口渲染环境是否有效
  if (!onValidWin()) {
    // LOGFLF(LogLevel::info, wdTitle, " is invalid");
    // vaildWdLog.olog(LogLevel::info, wdTitle, " is invalid");
    TLOGFLF(vaildWdLog, LogLevel::info, wdTitle, " is invalid");
    return false;
  }
  vaildWdLog.reset();
  // 窗口是否已经退出等
  if (!onPreTick()) {
    return false;
  }
  return true;
}

void Window::tick() {
  // 交由子类具体实现
  onTickWin();
}

void Window::close() {
  // 同步的交给子类窗口自己处理
  onCloseWin();
#ifdef WIN32
  if (surface) {
    DestroyWindow(surface);
    surface = nullptr;
    LOGFLF(LogLevel::info, "surface release");
  }
#endif
#ifdef __ANDROID__
  if (surface) {
    // 释放ANativeWindow引用
    ANativeWindow_release(surface);
    surface = nullptr;
    LOGFLF(LogLevel::info, "surface release");
  }
#endif
}

AvoxSurfaceType getNativeSurface(void* surface_) {
  AvoxSurfaceType surface = nullptr;
#ifdef __APPLE__
  surface = (__bridge AvoxSurfaceType)surface_;
#else
  // 传入窗口句柄，由onInitWin生成surface
  surface = (AvoxSurfaceType)surface_;
#endif
  return surface;
}

void getSurfaceSize(AvoxSurfaceType surface, int32_t& width, int32_t& height) {
#ifdef __ANDROID__
  width = ANativeWindow_getWidth(surface);
  height = ANativeWindow_getHeight(surface);
#endif
#ifdef __APPLE__
  // iOS 使用 CAMetalLayer
  if (surface != nil) {
    // 2. 获取屏幕缩放因子 (Retina 屏通常是 2.0 或 3.0)
    CGFloat scale = surface.contentsScale;
    // 3. 计算物理像素尺寸 (Physical Pixels)
    width = (int32_t)(surface.bounds.size.width * scale);
    height = (int32_t)(surface.bounds.size.height * scale);
  } else {
    width = 0;
    height = 0;
  }
#endif
#ifdef WIN32
  RECT rect;
  if (GetClientRect(surface, &rect)) {
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
  } else {
    width = 0;
    height = 0;
  }
#endif
}

}
