#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

// 膨胀与腐蚀(Dilation and Erosion)
// 膨胀和腐蚀被称为形态学操作.它们通常在二进制图像上执行,类似于轮廓检测.通过将像素添加到该图像中的对象的感知边界,扩张放大图像中的明亮白色区域.侵蚀恰恰相反:它沿着物体边界移除像素并缩小物体的大小.
// Closing 先dilation后erosion

class VkPreDilationLayer : public VkLayer, public IParamet<int32_t> {
  AVOX_LAYER_GETNAME(VkPreDilationLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
  ImageType imageType = ImageType::rgba8;

 public:
  VkPreDilationLayer(ImageType imageType = ImageType::rgba8);
  virtual ~VkPreDilationLayer();

 protected:
  virtual void onInitGraph() override;
};

// 扩张放大图像中的明亮白色区域
class VkDilationLayer : public VkGroupLayer, public IParamet<int32_t> {
  AVOX_LAYER_GETNAME(VkDilationLayer)
 private:
  /* data */
  ImageType imageType = ImageType::rgba8;

 public:
  VkDilationLayer(ImageType imageType = ImageType::rgba8);
  virtual ~VkDilationLayer();

 protected:
  VKTNodePtr<VkPreDilationLayer> preLayer = nullptr;

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
};

class VkPreErosionLayer : public VkLayer, public IParamet<int32_t> {
  AVOX_LAYER_GETNAME(VkPreErosionLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
  ImageType imageType = ImageType::rgba8;

 public:
  VkPreErosionLayer(ImageType imageType = ImageType::rgba8);
  virtual ~VkPreErosionLayer();

 protected:
  virtual void onInitGraph() override;
};

// 它沿着物体边界移除像素并缩小物体的明亮白色区域
class VkErosionLayer : public VkGroupLayer, public IParamet<int32_t> {
 private:
  /* data */
  ImageType imageType = ImageType::rgba8;

 public:
  VkErosionLayer(ImageType imageType = ImageType::rgba8);
  virtual ~VkErosionLayer();

 protected:
  VKTNodePtr<VkPreErosionLayer> preLayer = nullptr;

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
};

// Closing 先dilation后erosion
class VkClosingLayer : public VkGroupLayer, public IParamet<int32_t> {
 private:
  /* data */
  ImageType imageType = ImageType::rgba8;

 public:
  VkClosingLayer(ImageType imageType = ImageType::rgba8);
  virtual ~VkClosingLayer();

 protected:
  VKTNodePtr<VkDilationLayer> dilationLayer = nullptr;
  VKTNodePtr<VkErosionLayer> erosionLayer = nullptr;

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitNode() override;
};

// Opening 先erosion后dilation
class VkOpeningLayer : public VkGroupLayer, public IParamet<int32_t> {
 private:
  /* data */
  ImageType imageType = ImageType::rgba8;

 public:
  VkOpeningLayer(ImageType imageType = ImageType::rgba8);
  virtual ~VkOpeningLayer();

 protected:
  VKTNodePtr<VkDilationLayer> dilationLayer = nullptr;
  VKTNodePtr<VkErosionLayer> erosionLayer = nullptr;

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitNode() override;
};

}