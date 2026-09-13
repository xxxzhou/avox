#include "WaylandSurface.hpp"

#ifdef __ONLY_LINUX__

#ifdef AVOX_ENABLE_WAYLAND

#include <fcntl.h>
#include <poll.h>
#include <cstring>

#include <wayland-client.h>
// 配置期由 wayland-scanner 生成到 CMAKE_BINARY_DIR (见 AVOXOptions.cmake)
#include "xdg-shell-client-protocol.h"
// 协议接口符号 (scanner private-code), 全局作用域, 纯 C 与 C++ 兼容
#include "xdg-shell-client-code.c"

#include "avox/module/AvoxManager.hpp"

namespace avox {

void WaylandSurface::onRegistry(void* data, wl_registry* reg, uint32_t name,
                                const char* iface, uint32_t version) {
  auto* self = static_cast<WaylandSurface*>(data);
  if (strcmp(iface, wl_compositor_interface.name) == 0) {
    self->compositor = static_cast<wl_compositor*>(wl_registry_bind(
        reg, name, &wl_compositor_interface, version > 5 ? 5 : version));
  } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
    self->wmBase = static_cast<xdg_wm_base*>(wl_registry_bind(
        reg, name, &xdg_wm_base_interface, version > 2 ? 2 : version));
  }
}

void WaylandSurface::onWmBasePing(void* data, xdg_wm_base* b, uint32_t serial) {
  // 不应答 ping 会被合成器判为无响应杀掉窗口
  xdg_wm_base_pong(b, serial);
}

void WaylandSurface::onXdgConfigure(void* data, xdg_surface* s,
                                    uint32_t serial) {
  auto* self = static_cast<WaylandSurface*>(data);
  xdg_surface_ack_configure(s, serial);
  self->configured = true;
}

void WaylandSurface::onToplevelConfigure(void* data, xdg_toplevel* t,
                                         int32_t w, int32_t h, wl_array*) {
  auto* self = static_cast<WaylandSurface*>(data);
  // 0 表示合成器不指定尺寸 (由客户端决定), 保留当前值
  if (w > 0 && h > 0) {
    self->width = w;
    self->height = h;
  }
}

void WaylandSurface::onToplevelClose(void* data, xdg_toplevel* t) {
  auto* self = static_cast<WaylandSurface*>(data);
  self->closeRequested = true;
}

WaylandSurface::~WaylandSurface() {
  if (toplevel) {
    xdg_toplevel_destroy(toplevel);
  }
  if (xdgSurface) {
    xdg_surface_destroy(xdgSurface);
  }
  if (surface) {
    wl_surface_destroy(surface);
  }
  if (wmBase) {
    xdg_wm_base_destroy(wmBase);
  }
  if (registry) {
    wl_registry_destroy(registry);
  }
  if (display) {
    wl_display_disconnect(display);
  }
}

bool WaylandSurface::create(int width_, int height_, const char* title) {
  width = width_;
  height = height_;

  display = wl_display_connect(nullptr);
  if (!display) {
    LOGFLF(LogLevel::warn, "failed to connect wayland display");
    return false;
  }
  registry = wl_display_get_registry(display);
  static const wl_registry_listener kRegListener = {onRegistry, nullptr};
  wl_registry_add_listener(registry, &kRegListener, this);
  // 第一轮取全局对象, 第二轮完成绑定对象的初始事件
  wl_display_roundtrip(display);
  wl_display_roundtrip(display);
  if (!compositor || !wmBase) {
    LOGFLF(LogLevel::warn, "wayland missing compositor/xdg_wm_base");
    return false;
  }
  static const xdg_wm_base_listener kWmListener = {onWmBasePing};
  xdg_wm_base_add_listener(wmBase, &kWmListener, this);

  surface = wl_compositor_create_surface(compositor);
  xdgSurface = xdg_wm_base_get_xdg_surface(wmBase, surface);
  toplevel = xdg_surface_get_toplevel(xdgSurface);
  static const xdg_surface_listener kXdgListener = {onXdgConfigure};
  xdg_surface_add_listener(xdgSurface, &kXdgListener, this);
  static const xdg_toplevel_listener kTopListener = {
      onToplevelConfigure, onToplevelClose, nullptr, nullptr};
  xdg_toplevel_add_listener(toplevel, &kTopListener, this);
  xdg_toplevel_set_title(toplevel, title ? title : "avox");
  xdg_toplevel_set_app_id(toplevel, "avox");
  wl_surface_commit(surface);

  // 等首轮 configure (合成器布局/装饰协商)
  for (int i = 0; i < 100 && !configured && !closeRequested; ++i) {
    wl_display_roundtrip(display);
  }
  if (!configured) {
    LOGFLF(LogLevel::warn, "wayland xdg configure timeout");
    return false;
  }
  log(LogLevel::info, "Wayland window created: ", width, "x", height);
  return true;
}

void WaylandSurface::pollEvents() {
  if (!display) {
    return;
  }
  // 先处理已读入队列的事件
  wl_display_dispatch_pending(display);
  // 非阻塞读新事件: prepare_read 成功后轮询 fd, 无数据则取消读
  while (wl_display_prepare_read(display) != 0) {
    wl_display_dispatch_pending(display);
  }
  pollfd pfd = {wl_display_get_fd(display), POLLIN, 0};
  if (::poll(&pfd, 1, 0) > 0) {
    wl_display_read_events(display);
    wl_display_dispatch_pending(display);
  } else {
    wl_display_cancel_read(display);
  }
  wl_display_flush(display);
}

}

#endif  // AVOX_ENABLE_WAYLAND
#endif  // __ONLY_LINUX__
