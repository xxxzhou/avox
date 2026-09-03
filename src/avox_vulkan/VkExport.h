#pragma once

#include "avox/AvoxDef.h"
#include "avox/AvoxLayer.h"

namespace avox {

// 几何叠加层：在视频帧上绘制 线/矩形/点/圆
// 复用 VkFontLayer 的 "CPU 光栅化到小 canvas(R8) -> upload -> GPU 双线性混合"
// 管线， blend shader 用阈值硬切（α>=阈值
// 全合并，无透明感），阈值即线宽旋钮（低=宽，高=细）。 资源随 ISurfaceRender
// 释放自动回收。enable/disable 仅控制 VkGeometryLayer 进/出 vulkan 执行链，
// IGeometryLayer 对象常驻（由 render 持有），disable 不释放对象。
class IGeometryLayer {
 public:
  virtual ~IGeometryLayer() {}

 public:
  // 全局颜色（线/轮廓/填充），范围 0-1
  virtual void setColor(float r, float g, float b) = 0;
  // canvas 缩放：canvas=帧/(scale*dpiScale)，决定 S=帧/canvas
  // 与宽度档位数。默认 4
  virtual void setScale(float scale) = 0;
  // 宽度阈值 [0,1]：α>=阈值 全合并，<阈值 丢弃。低=宽，高=细，0.5 适中
  virtual void setThreshold(float tau) = 0;
  // 清空当前帧所有图元（动态图元每帧重画前调用）
  virtual void clear() = 0;
  // 画点（实心圆），x,y 归一化 [0,1]，radiusPx 帧像素
  virtual void drawPoint(float x, float y, float radiusPx) = 0;
  // 画线段，端点归一化 [0,1]，线宽由 threshold 控制
  virtual void drawLine(float x0, float y0, float x1, float y1) = 0;
  // 画矩形，两对角点归一化；fill=true 实心，否则仅轮廓
  virtual void drawRect(float x0, float y0, float x1, float y1,
                        bool fill = false) = 0;
  // 画圆，圆心归一化 [0,1]，radiusPx 帧像素；fill=true 实心圆盘，否则仅圆环轮廓
  virtual void drawCircle(float cx, float cy, float radiusPx,
                          bool fill = false) = 0;
};

extern "C" {
// 检查vulkan是否可用,是否能生成vulkan实例,是否有vulkan设备等
AVOX_EXPORT bool canVulkan();
// 得到并开启几何叠加管线（对象由 render 持有）
AVOX_EXPORT IGeometryLayer* enableRenderGeometry(ISurfaceRender* render);
// 关闭叠加
AVOX_EXPORT void disableRenderGeometry(ISurfaceRender* render);
}

}
