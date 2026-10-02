#include "GlesCanvasLayer.hpp"

#include <vector>

namespace avox {

void GlesCanvasLayer::updateCanvas(const AssCanvas& canvas) {
  state.updateCanvas(canvas);
}

void GlesCanvasLayer::clearCanvas() { state.clearCanvas(); }

void GlesCanvasLayer::setCanvasTransform(float scale, float offsetX,
                                         float offsetY, float opacity) {
  state.setCanvasTransform(scale, offsetX, offsetY, opacity);
}

bool GlesCanvasLayer::ensureTexture(int32_t frameW, int32_t frameH) {
  if (frameW <= 0 || frameH <= 0) {
    return false;
  }
  // 画布 = 合成画布尺寸(subtitleCanvasSize, 与 VK/Metal 同机制)
  int32_t cw = 0;
  int32_t ch = 0;
  subtitleCanvasSize(frameW, frameH, &cw, &ch);
  if (cw <= 0 || ch <= 0) {
    return false;
  }
  if (tex) {
    return true;
  }
  if (!state.reset(cw, ch)) {
    return false;
  }
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cw, ch, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  // 建图前来件暂存回灌(避免丢第一屏字幕, 与 VK applyPending 同流程)
  if (tex && state.hasPending()) {
    std::vector<uint8_t> buf;
    int32_t w = 0, h = 0, stride = 0, x = 0, y = 0;
    state.takePending(buf, w, h, stride, x, y);
    state.updateCanvas(AssCanvas{buf.data(), w, h, stride, x, y});
  }
  return tex != 0;
}

void GlesCanvasLayer::uploadIfNeeded() {
  if (!tex || !state.takeUploadDue()) {
    return;
  }
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, state.width(), state.height(),
                  GL_RGBA, GL_UNSIGNED_BYTE, state.data());
  glBindTexture(GL_TEXTURE_2D, 0);
}

void GlesCanvasLayer::releaseGL() {
  if (tex) {
    glDeleteTextures(1, &tex);
    tex = 0;
  }
}

}
