#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

// 边框默认使用REPLICATE模式
class VkLinearFilterLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkLinearFilterLayer)
  // AVOX_LAYER_GETNAME(VkLinearFilterLayer)
 protected:
  std::unique_ptr<VkWrapBuffer> kernelBuffer;
  // bool bOneChannel = false;
  ImageType imageType = ImageType::rgba8;

 public:
  VkLinearFilterLayer(ImageType imageType = ImageType::rgba8);
  virtual ~VkLinearFilterLayer();

 protected:
  virtual void onInitGraph() override;
  // virtual void onInitLayer() override;
  // virtual void onInitVkBuffer() override;
  virtual void onInitPipe() override;
};

class VkBoxBlurLayer : public VkLinearFilterLayer,
                       public IParamet<KernelSizeParamet> {
  AVOX_LAYER_GETNAME(VkBoxBlurLayer)

 public:
  VkBoxBlurLayer(ImageType imageType = ImageType::rgba8);
  virtual ~VkBoxBlurLayer();

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitVkBuffer() override;
};

class VkGaussianBlurLayer : public VkLinearFilterLayer,
                            public IParamet<GaussianBlurParamet> {
  AVOX_LAYER_GETNAME(VkGaussianBlurLayer)

 public:
  VkGaussianBlurLayer(ImageType imageType = ImageType::rgba8);
  virtual ~VkGaussianBlurLayer();

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitVkBuffer() override;
};

}
