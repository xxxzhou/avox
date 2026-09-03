#pragma once

#include <mutex>
#include <vector>

#include "GeoShape.hpp"
#include "avox_vulkan/VkExport.h"

namespace avox {

class VkGeometryLayer;

// 几何渲染器 - 持有图元数据（用户线程写）+ 全局设置，render 线程按 dirty 取快照。
// 继承 IGeometryLayer，提供统一操作接口。由 VkVideoRender 持有。
class GeometryRender : public IGeometryLayer {
 public:
  GeometryRender();
  ~GeometryRender() override = default;

 private:
  // 绑定的 vulkan 层（graph 创建后注入），不持有
  VkGeometryLayer* layer = nullptr;
  // 当前帧图元（用户线程 clear+draw* 重写）
  std::vector<GeoShape> shapes;
  // 全局设置
  float curR = 1.0f;
  float curG = 1.0f;
  float curB = 1.0f;
  float curThreshold = 0.5f;
  // 默认 4 → canvas=帧/(4*dpiScale)，1080p 下 S≈4，阈值有 4 档线宽可调
  float curScale = 4.0f;
  bool bEnable = false;
  // rasterDirty: 形状/canvas 尺寸变化 → 需要 rasterize + upload（计算密集）
  //   clear/draw*/setScale/setLayer 置位
  // paramDirty: 颜色/阈值变化 → 只需 updateUBO（廉价，不重画 canvas）
  //   setColor/setThreshold/setLayer 置位
  // setLayer 同时置两者：新层 canvas 和 UBO 都要重新填充
  bool rasterDirty = true;
  bool paramDirty = true;
  std::mutex mtx;

 public:
  // 由 VkVideoRender 在 graph 创建时注入
  void setLayer(VkGeometryLayer* layer);
  void setEnable(bool enable) { bEnable = enable; }
  bool enabled() const { return bEnable; }
  // render 线程：rasterDirty 则拷贝图元并清位返回 true；否则 false（跳过 rasterize）
  bool consumeRasterDirty(GeoSnapshot& out);
  // render 线程：paramDirty 则拷贝颜色/阈值并清位返回 true；否则 false（跳过 updateUBO）
  bool consumeParamDirty(GeoSnapshot& out);

 public:
  void setColor(float r, float g, float b) override;
  void setScale(float scale) override;
  void setThreshold(float tau) override;
  void clear() override;
  void drawPoint(float x, float y, float radiusPx) override;
  void drawLine(float x0, float y0, float x1, float y1) override;
  void drawRect(float x0, float y0, float x1, float y1, bool fill) override;
  void drawCircle(float cx, float cy, float radiusPx, bool fill) override;
};

}
