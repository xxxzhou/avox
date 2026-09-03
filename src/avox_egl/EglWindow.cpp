#include "EglWindow.hpp"

#include <GLES3/gl3.h>
#ifdef __ANDROID__
#include "avox_android/AndVDecoder.hpp"
#endif

namespace avox {

EglWindow::EglWindow() { renderType = RenderType::OpenGLES; }

void EglWindow::onChangeSize() {
  if (wdWidth == 0 || wdHeight == 0) {
    LOGFLF(LogLevel::info, "invalid window size");
    return;
  }  
}

}
