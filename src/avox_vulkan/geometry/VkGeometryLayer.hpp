#pragma once

#include "GeoShape.hpp"
#include "avox/video/ImageBuffer.hpp"
#include "avox_vulkan/layer/VkLayer.hpp"
#include "avox_vulkan/vulkan/VkTexture.hpp"

namespace avox {

class GeometryRender;

// 几何混合参数（UBO）
struct GeoBlendParamet {
  vec3f color = {1.0f, 1.0f, 1.0f};
  // 宽度阈值 [0,1]：α>=阈值 全合并，<阈值 丢弃。低=宽，高=细
  float threshold = 0.5f;
};

// 几何叠加层：CPU 在小 canvas(R8) 上距离场光栅化线/矩形/点/圆，
// upload 后 GPU 用 drawShapeBlend.comp 双线性放大 + 阈值硬切混合到视频帧。
class VkGeometryLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkGeometryLayer)

 public:
  VkGeometryLayer();
  virtual ~VkGeometryLayer();

 private:
  // 数据源（图元 + 设置），由 VkVideoRender 注入，不持有
  GeometryRender* source = nullptr;
  // canvas 缩放：canvas=帧/(scale*dpiScale)，tscale=scale*dpiScale=S(帧/canvas)
  float scale = 1.0f;
  float dpiScale = 1.0f;
  float tscale = 1.0f;
  // CPU blit 资源
  std::unique_ptr<ImageBuffer> cpuCanvas;
  std::unique_ptr<VkWrapBuffer> cpuBuffer;
  // 内部 canvas image，drawShapeBlend.comp 的 sampler 输入
  VulkanTexturePtr canvasImage;
  int32_t canvasWidth = 0;
  int32_t canvasHeight = 0;
  GeoBlendParamet gParamet = {};

 public:
  void setSource(GeometryRender* s) { source = s; }
  void setScale(float scale);

 protected:
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
  virtual void onInitPipe() override;
  virtual void onPreFrame() override;
  virtual void onCommand() override;

 private:
  // 把一帧快照光栅化到 cpuCanvas（只写 0/255，双线性放大自然产生 0~1 渐变）
  void rasterize(const GeoSnapshot& snap);
  // 图元光栅化：canvas 上只写 255（内部）或 0（外部），不写中间值
  void rasterDisc(uint8_t* data, float cx, float cy, float r);
  void rasterRing(uint8_t* data, float cx, float cy, float r);
  void rasterLine(uint8_t* data, float x0, float y0, float x1, float y1);
  void rasterRectFill(uint8_t* data, float x0, float y0, float x1, float y1);
  // 点到线段距离
  static float distPointSegment(float px, float py, float x0, float y0,
                                float x1, float y1);
  // 写一个 canvas 像素为 255（max 混合）
  inline void putPixel(uint8_t* data, int x, int y);
  // 写一个 canvas 像素为 alpha 值（max 混合，用于圆边缘 AA）
  inline void putAlpha(uint8_t* data, int x, int y, float a);
};

}
