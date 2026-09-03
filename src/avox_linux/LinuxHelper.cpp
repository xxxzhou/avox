#include "LinuxHelper.h"

#include "X11Surface.hpp"
#include "avox/module/AvoxManager.hpp"

namespace avox {

__attribute__((constructor)) void on_library_load() {
  avox::AvoxManager::Get().init();
}

__attribute__((destructor)) void on_library_unload() {
  // avox::AvoxManager::Get().uninit();
}

ILinuxSurface* createLinuxSurface(int width, int height, const char* title) {
  X11Surface* surface = new X11Surface();
  if (surface->create(width, height, title)) {
    return surface;
  }
  delete surface;
  return nullptr;
}

}