#pragma once

#include <cstdint>

#include "IAssOverlay.hpp"

namespace avox {

class ISurfaceRender;

// 字幕画布混合层(计划 doc/plan/player/ASS字幕渲染计划.md §3.4)。
// 画布为视频分辨率坐标系的 RGBA8 bbox 裁剪区(由 IAssOverlay::render /
// PGS / TextRasterizer 产出, premultiplied alpha), 变化时上传, 静止段零上传。
//
// 内部接口, 不进公开头/绑定: 层归字幕引擎独占(SubtitlesView 挂摘与清屏),
// 数据由引擎内部算好直接画 — 宿主不可也不需要调用(IFontLayer/IGeometryLayer
// 才是给上层的叠加出口, 背后是宿主专属实例)。
class ICanvasLayer {
 public:
  virtual ~ICanvasLayer() {}

 public:
  // 上传画布(bbox 裁剪, 整帧坐标系): canvas 为 RGBA8 子图, x/y 为其相对视频帧的偏移
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
