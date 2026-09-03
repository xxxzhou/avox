#pragma once
#include "VkLayer.hpp"

namespace avox {

class VkResizeLayer : public VkLayer, public IReSizeLayer {
  AVOX_LAYER_GETNAME(VkResizeLayer)
 private:
  /* data */
  ImageType imageType = ImageType::rgba8;

 public:
  VkResizeLayer();
  VkResizeLayer(ImageType imageType);
  virtual ~VkResizeLayer();

 protected:
  virtual bool getSampled(int inIndex) override;
  virtual bool sampledNearest(int32_t inIndex) override;
  virtual void onUpdateParamet() override;
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
};

class VkSizeScaleLayer : public VkLayer, public IParamet<SizeScaleParamet> {
  AVOX_LAYER_GETNAME(VkSizeScaleLayer)
 private:
  /* data */
  ImageType imageType = ImageType::rgba8;

 public:
  VkSizeScaleLayer();
  VkSizeScaleLayer(ImageType imageType);
  ~VkSizeScaleLayer();

 protected:
  virtual bool getSampled(int inIndex) override;
  virtual bool sampledNearest(int32_t inIndex) override;
  virtual void onUpdateParamet() override;
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
};

// Gamma ranges from 0.0 to 3.0, with 1.0 as the normal level
class CVkGammaLayer : public VkLayer, public IGammaLayer {
  AVOX_LAYER_GETNAME(CVkGammaLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  CVkGammaLayer(/* args */);
  virtual ~CVkGammaLayer();
};

}