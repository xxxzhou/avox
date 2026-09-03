#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

// 像素图效果,马赛克
class VkPixellateLayer : public VkLayer, public IParamet<PixellateParamet> {
  AVOX_LAYER_GETNAME(VkPixellateLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkPixellateLayer(/* args */);
  virtual ~VkPixellateLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override;
};

// 半色调效果,如新闻打印
class VkHalftoneLayer : public VkLayer, public IParamet<PixellateParamet> {
  AVOX_LAYER_GETNAME(VkHalftoneLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkHalftoneLayer(/* args */);
  virtual ~VkHalftoneLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override;
};

// 分解图像为常规网格中的彩色点
class VkPolkaDotLayer : public VkLayer, public IParamet<PolkaDotParamet> {
  AVOX_LAYER_GETNAME(VkPolkaDotLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkPolkaDotLayer(/* args */);
  ~VkPolkaDotLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override;
};

}