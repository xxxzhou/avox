#pragma once

#include <cstdint>

#include "IAssOverlay.hpp"

namespace avox {

class ISurfaceRender;

// 字幕合成画布尺寸(帧 → 画布): >1080p 恒为 1920x1080 基准(内存/逐帧拷贝常数,
// 铺满全帧的拉伸由层内 sampler 完成, 与旧 VkFontLayer 同机制); ≤1080p 与帧 1:1
// (尺寸本就不大, 保持原生清晰度)。文本/ASS/PGS 三路内容统一按此坐标系产出。
inline constexpr int32_t kSubtitleCanvasReferenceHeight = 1080;

// 由帧尺寸算合成画布尺寸; 帧尺寸未知(≤0)时出 0, 调用方自行兜底
inline void subtitleCanvasSize(int32_t frameW, int32_t frameH,
                               int32_t* canvasW, int32_t* canvasH) {
  if (frameW <= 0 || frameH <= 0) {
    *canvasW = 0;
    *canvasH = 0;
    return;
  }
  const float dpiScale =
      frameH > kSubtitleCanvasReferenceHeight
          ? (float)frameH / (float)kSubtitleCanvasReferenceHeight
          : 1.f;
  int32_t cw = (int32_t)((float)frameW / dpiScale + 0.5f);
  int32_t ch = (int32_t)((float)frameH / dpiScale + 0.5f);
  if (cw < 1) {
    cw = 1;
  }
  if (ch < 1) {
    ch = 1;
  }
  *canvasW = cw;
  *canvasH = ch;
}

// 字幕画布混合层(计划 doc/plan/player/ASS字幕渲染计划.md §3.4)。
// 画布为合成画布坐标系的 RGBA8 bbox 裁剪区(由 IAssOverlay::render /
// PGS / TextRasterizer 产出, premultiplied alpha), 变化时上传, 静止段零上传。
//
// 内部接口, 不进公开头/绑定: 层归字幕引擎独占(SubtitlesView 挂摘与清屏),
// 数据由引擎内部算好直接画 — 宿主不可也不需要调用(IFontLayer/IGeometryLayer
// 才是给上层的叠加出口, 背后是宿主专属实例)。
class ICanvasLayer {
 public:
  virtual ~ICanvasLayer() {}

 public:
  // 上传画布(bbox 裁剪, 合成画布坐标系): canvas 为 RGBA8 子图, x/y 为其相对
  // 合成画布的偏移(上屏拉伸由层内 sampler 归一化映射完成)
  virtual void updateCanvas(const AssCanvas& canvas) = 0;
  // 清空(无字幕帧): 层走直通, 不采样画布
  virtual void clearCanvas() = 0;

  // ---- 全局变换通道(字幕轨槽) ----
  // 帧归一化 scale/offset + opacity: 文本槽位由 CPU 侧重栅格化消费(下发
  // 单位值), ASS/PGS 轨槽位经 canvas UV 反算消费(下发用户值)。实现方
  // (常驻前端)存权威副本并转发当前层, 图重建后自动补发, 不触发图重建
  virtual void setCanvasTransform(float scale, float offsetX, float offsetY,
                                  float opacity) {}
  // 消费"画布层内容因图重建丢失"信号(层随图销毁, 内容不随层迁移):
  // 返回 true 时调用方须强制重传当次内容; 取走即清
  virtual bool takeContentStale() { return false; }
};

// core ↔ avox_vulkan 通道: 获取/归还字幕画布层的常驻前端。
// 无 Vulkan 图(纯 CPU 渲染)时返回 nullptr, 调用方降级为不渲染字幕轨。
// 变换与内容信号经返回的前端(ICanvasLayer)直接下发, 不再另开通道函数。
ICanvasLayer* enableRenderCanvas(ISurfaceRender* surfaceRender);
void disableRenderCanvas(ISurfaceRender* surfaceRender);

}
