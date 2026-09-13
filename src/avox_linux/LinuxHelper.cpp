#include "LinuxHelper.h"

#include <cstring>
#include <cstdlib>

#include "X11Surface.hpp"
#ifdef AVOX_ENABLE_WAYLAND
#include "WaylandSurface.hpp"
#endif
#include "avox/module/AvoxManager.hpp"

namespace avox {

__attribute__((constructor)) void on_library_load() {
  avox::AvoxManager::Get().init();
}

__attribute__((destructor)) void on_library_unload() {
  // avox::AvoxManager::Get().uninit();
}

ILinuxSurface* createLinuxSurface(int width, int height, const char* title) {
  // 宿主线程建窗口/SDK渲染线程建VkSurface是跨线程Xlib访问, 先启用线程安全
  static bool xThreads = []() { return XInitThreads() != 0; }();
  (void)xThreads;
#ifdef AVOX_ENABLE_WAYLAND
  // 选择显示后端: AVOX_LINUX_WM 显式指定; 否则纯 Wayland 会话(无 DISPLAY)自动走
  // Wayland, 其余(XWayland/纯X11)维持已验证的 X11 通道
  const char* wm = getenv("AVOX_LINUX_WM");
  bool bWayland = false;
  if (wm) {
    bWayland = (strcasecmp(wm, "wayland") == 0);
  } else {
    bWayland = getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY");
  }
  if (bWayland) {
    WaylandSurface* waylandSurface = new WaylandSurface();
    if (waylandSurface->create(width, height, title)) {
      return waylandSurface;
    }
    delete waylandSurface;
    log(LogLevel::warn, "wayland surface create failed, fallback to x11");
  }
#endif
  X11Surface* surface = new X11Surface();
  if (surface->create(width, height, title)) {
    return surface;
  }
  delete surface;
  return nullptr;
}

}