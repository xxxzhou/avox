#pragma once

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <string>

#include "LinuxHelper.h"
#include "avox/module/LogHelper.hpp"

namespace avox {

class X11Surface : public ILinuxSurface {
 public:
  X11Surface();
  virtual ~X11Surface();

  bool create(int width, int height, const char* title);

 public:
  virtual void* getDisplay() override { return display; }
  virtual void* getWindow() override { return (void*)(uintptr_t)window; }
  virtual int32_t getWidth() override { return width; }
  virtual int32_t getHeight() override { return height; }
  virtual void pollEvents() override;
  virtual bool shouldClose() override { return closeRequested; }

 private:
  Display* display = nullptr;
  Window window = 0;
  Atom wmDeleteMessage = 0;
  int width = 0;
  int height = 0;
  bool closeRequested = false;
  std::string windowTitle;

  void cleanup();
};

}
