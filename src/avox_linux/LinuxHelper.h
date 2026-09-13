#pragma once

#include "avox/AvoxDef.h"

namespace avox {

// Linux 窗口接口 (X11 / Wayland)
class ILinuxSurface {
 public:
  ILinuxSurface() = default;
  virtual ~ILinuxSurface() = default;

 public:
  virtual void* getDisplay() = 0;  // Display* for X11, wl_display* for Wayland
  virtual void* getWindow() = 0;   // Window for X11, wl_surface* for Wayland
  virtual int32_t getWidth() = 0;
  virtual int32_t getHeight() = 0;
  virtual void pollEvents() = 0;
  virtual bool shouldClose() = 0;
  // 显示后端标识: "x11" / "wayland"; 带默认实现兼容旧实现 (仅 X11)
  virtual const char* getDisplayType() const { return "x11"; }
};

extern "C" {
ILinuxSurface* createLinuxSurface(int width, int height, const char* title);
}

}