#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "VkBlendingModeLayer.hpp"

namespace avox {

// 主要有二种需要显示。
// 1. 显示时间戳，大小相对固定，左上角对齐，渲染大张图浪费
// 2. 显示字幕，大小不固定，中心对齐更好，渲染图需要大张的

class VkDrawPointsPreLayer : public VkLayer, public IParamet<PointsParamet> {
  AVOX_LAYER_GETNAME(VkDrawPointsPreLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
  std::unique_ptr<VkWrapBuffer> inBuffer = nullptr;
  // std::unique_ptr<VkWrapBuffer> inBufferX = nullptr;
  int32_t maxPoint = 4000;

 public:
  VkDrawPointsPreLayer(/* args */);
  virtual ~VkDrawPointsPreLayer();

 public:
  void setImageFormat(const ImageFormat& imageFormat);
  void drawPoints(const vec2f* points, int32_t size, vec4f color,
                  int32_t raduis);

 protected:
  virtual void onInitGraph() override;
  virtual void onInitVkBuffer() override;
  virtual void onInitPipe() override;
  virtual void onCommand() override;
};

class VkDrawPointsLayer : public VkGroupLayer, public IDrawPointsLayer {
  AVOX_LAYER_GETNAME(VkDrawPointsLayer)
 private:
  /* data */
  VKTNodePtr<VkDrawPointsPreLayer> preLayer = nullptr;

 public:
  VkDrawPointsLayer(/* args */);
  virtual ~VkDrawPointsLayer();

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitNode() override;
  virtual void onInitLayer() override;
  // virtual void onCommand() override;

 public:
  virtual void drawPoints(const vec2f* points, int32_t size, vec4f color,
                          int32_t raduis) override;
};

class VkDrawRectLayer : public VkLayer, public IDrawRectLayer {
  AVOX_LAYER_GETNAME(VkDrawRectLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkDrawRectLayer(/* args */);
  virtual ~VkDrawRectLayer();
};

}