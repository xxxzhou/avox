#pragma once

#include "avox/AvoxDef.h"

namespace avox {

// Linux 窗口接口 (X11)
class ILinuxSurface {
 public:
  ILinuxSurface() = default;
  virtual ~ILinuxSurface() = default;

 public:
  virtual void* getDisplay() = 0;  // Display* for X11
  virtual void* getWindow() = 0;   // Window for X11
  virtual int32_t getWidth() = 0;
  virtual int32_t getHeight() = 0;
  virtual void pollEvents() = 0;
  virtual bool shouldClose() = 0;
};

extern "C" {
ILinuxSurface* createLinuxSurface(int width, int height, const char* title);
}

}