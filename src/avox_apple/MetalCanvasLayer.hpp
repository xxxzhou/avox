#pragma once

#include <Metal/Metal.h>

#include <vector>

#include "avox/subtitle/CanvasState.hpp"
#include "avox/subtitle/SubtitleCanvas.hpp"

namespace avox {

// Metal 字幕画布层(字幕画布多后端渲染计划 §5.2): 数据面在 CanvasState(多后端
// 共享), 本层做 MTLTexture 上传(replaceRegion 整画布, 内容变化时)。混合由
// MetalRender 的第二 draw 完成(OM blend one/oneMinusSourceAlpha, premultiplied
// source-over), 无内容整跳(零字幕零影响)。画布恒 rgba8 SDR gamma 域, f16 直通
// 域的线性化在 shader 内做(linearScale), 域翻转无需重传内容。
class MetalCanvasLayer : public ICanvasLayer {
 public:
  // ICanvasLayer(数据面全委托 CanvasState)
  void updateCanvas(const AssCanvas& canvas) override;
  void clearCanvas() override;
  void setCanvasTransform(float scale, float offsetX, float offsetY,
                          float opacity) override;

  // 渲染线程: 按合成画布尺寸(subtitleCanvasSize)建画布与纹理, 建图前来件回灌
  bool ensureTexture(id<MTLDevice> d, int32_t frameW, int32_t frameH);
  // 内容变化时整画布上传(对白节奏数秒一次, 与 VK staging 同口径)
  void uploadIfNeeded();

  bool visible() const { return state.hasContent(); }
  CanvasBlendParamet computeParamet() { return state.computeParamet(); }
  id<MTLTexture> texture() const { return canvasTexture; }

 private:
  CanvasState state;
  id<MTLTexture> canvasTexture = nil;
};

// canvasFragmentShader 的参数区(C++ 侧镜像, 布局严格同序, 全 float)
struct MetalCanvasParams {
  // 变换后门控矩形中心与宽高(帧归一化坐标)
  float centerX = 0.f;
  float centerY = 0.f;
  float width = 0.f;
  float height = 0.f;
  // 整层不透明度(0=直通/无字幕)
  float opacity = 0.f;
  // 采样反算仿射原点与 1/scale
  float originX = 0.f;
  float originY = 0.f;
  float invScale = 1.f;
  // >0 = f16 直通(1.0=100nit): canvas 线性化×该值(203/100); 0 = SDR gamma 直混
  float linearScale = 0.f;
};

}
