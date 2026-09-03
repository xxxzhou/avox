#include "MetalWindow.hpp"
#include <iostream>

namespace avox {

MetalWindow::MetalWindow() { renderType = RenderType::Metal; }

MetalWindow::~MetalWindow() {}

void MetalWindow::onChangeSize() {
  if (wdWidth == 0 || wdHeight == 0) {
    LOGFLF(LogLevel::info, "invalid window size");
  }
}

}
