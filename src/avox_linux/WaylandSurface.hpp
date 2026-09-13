#pragma once

#ifdef __ONLY_LINUX__

#ifdef AVOX_ENABLE_WAYLAND

#include "LinuxHelper.h"

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_surface;
struct wl_array;
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;

namespace avox {

// Wayland 窗口 (xdg_shell): 纯 Wayland 会话用, XWayland 缺席时的原生通道
// 宿主/SDK 经 ILinuxSurface 消费, VkWindow 按 getDisplayType() 分流建 surface
class WaylandSurface : public ILinuxSurface {
 public:
  WaylandSurface() = default;
  virtual ~WaylandSurface();

 public:
  bool create(int width, int height, const char* title);

 public:
  virtual void* getDisplay() override { return display; }
  virtual void* getWindow() override { return surface; }
  virtual const char* getDisplayType() const override { return "wayland"; }
  virtual int32_t getWidth() override { return width; }
  virtual int32_t getHeight() override { return height; }
  virtual void pollEvents() override;
  virtual bool shouldClose() override { return closeRequested; }

 private:
  // 注册表/协议事件入口 (user data 均为 this)
  static void onRegistry(void* data, wl_registry* reg, uint32_t name,
                         const char* iface, uint32_t version);
  static void onXdgConfigure(void* data, xdg_surface* s, uint32_t serial);
  static void onToplevelConfigure(void* data, xdg_toplevel* t, int32_t w,
                                  int32_t h, wl_array* states);
  static void onToplevelClose(void* data, xdg_toplevel* t);
  static void onWmBasePing(void* data, xdg_wm_base* b, uint32_t serial);

 private:
  wl_display* display = nullptr;
  wl_registry* registry = nullptr;
  wl_compositor* compositor = nullptr;
  xdg_wm_base* wmBase = nullptr;
  wl_surface* surface = nullptr;
  xdg_surface* xdgSurface = nullptr;
  xdg_toplevel* toplevel = nullptr;
  int32_t width = 0;
  int32_t height = 0;
  bool configured = false;
  bool closeRequested = false;
};

}

#endif  // AVOX_ENABLE_WAYLAND
#endif  // __ONLY_LINUX__
