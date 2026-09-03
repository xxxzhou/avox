#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

// 边框默认使用REPLICATE模式
class VkSeparableLayer : public VkGroupLayer {
  AVOX_LAYER_GETNAME(VkSeparableLayer)
 protected:
  /* data */
  ImageType imageType = ImageType::rgba8;
  std::unique_ptr<VkWrapBuffer> kernelBuffer = nullptr;

 public:
  VkSeparableLayer(ImageType imageType = ImageType::rgba8);
  virtual ~VkSeparableLayer();

  void updateBuffer(std::vector<float> data);

 protected:
  virtual void onInitGroup() override;
  virtual void onInitLayer() override;
  virtual void onInitPipe() override;
};

class VkSeparableLinearLayer : public VkSeparableLayer {
  AVOX_LAYER_GETNAME(VkSeparableLinearLayer)
 protected:
  VKTNodePtr<VkSeparableLayer> rowLayer = nullptr;

 public:
  VkSeparableLinearLayer(ImageType imageType = ImageType::rgba8);
  virtual ~VkSeparableLinearLayer();

 protected:
  virtual void onInitNode() override;
  virtual void onInitLayer() override;
};

class VkBoxBlurSLayer : public VkSeparableLinearLayer,
                        public IParamet<KernelSizeParamet> {
  AVOX_LAYER_GETNAME(VkBoxBlurSLayer)

 public:
  VkBoxBlurSLayer(ImageType imageType = ImageType::rgba8);
  virtual ~VkBoxBlurSLayer();

 public:
  void getKernel(int size, std::vector<float>& kernels);

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitLayer() override;
};

class VkGaussianBlurSLayer : public VkSeparableLinearLayer,
                             public IParamet<GaussianBlurParamet> {
  AVOX_LAYER_GETNAME(VkGaussianBlurSLayer)

 public:
  VkGaussianBlurSLayer(ImageType imageType = ImageType::rgba8);
  virtual ~VkGaussianBlurSLayer();

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitLayer() override;
};

}
