#pragma once

#include <GLES2/gl2.h>

#include "avox/subtitle/CanvasState.hpp"
#include "avox/subtitle/SubtitleCanvas.hpp"

namespace avox {

// GLES 字幕画布层(字幕画布多后端渲染计划 §5.3): 数据面在 CanvasState(多后端
// 共享), 本层做 GL 纹理上传。混合由 EglVideoRender 的第二 draw 完成
// (glBlendFunc GL_ONE/GL_ONE_MINUS_SRC_ALPHA, premultiplied source-over)。
// GLES 腿无 HDR 呈现面(EglVideoRender fetchFrame 注), 恒 SDR gamma 域。
class GlesCanvasLayer : public ICanvasLayer {
 public:
  // ICanvasLayer(数据面全委托 CanvasState)
  void updateCanvas(const AssCanvas& canvas) override;
  void clearCanvas() override;
  void setCanvasTransform(float scale, float offsetX, float offsetY,
                          float opacity) override;

  // 渲染线程(context current): 按合成画布尺寸建纹理, 建图前来件回灌
  bool ensureTexture(int32_t frameW, int32_t frameH);
  // 内容变化时整画布上传(texSubImage2D, 对白节奏数秒一次)
  void uploadIfNeeded();

  bool visible() const { return state.hasContent(); }
  CanvasBlendParamet computeParamet() { return state.computeParamet(); }
  GLuint texture() const { return tex; }
  // closeProgram 时随 context 释放(context current 下调)
  void releaseGL();

 private:
  CanvasState state;
  GLuint tex = 0;
};

}
