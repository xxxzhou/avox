#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "VkLowPassLayer.hpp"
#include "VkReduceLayer.hpp"
#include "../layer/VkLayer.hpp"
#include "../layer/VkOutputLayer.hpp"

namespace avox {

class VkMotionBlurLayer : public VkLayer, public IParamet<MotionBlurParamet> {
  AVOX_LAYER_GETNAME(VkMotionBlurLayer)
 private:
  /* data */
 public:
  VkMotionBlurLayer(/* args */);
  virtual ~VkMotionBlurLayer();

 private:
  void transformParamet();

 protected:
  virtual bool getSampled(int32_t inIndex) override;
  virtual void onUpdateParamet() override;
  virtual void onInitLayer() override;
};

class VkZoomBlurLayer : public VkLayer, public IParamet<ZoomBlurParamet> {
  AVOX_LAYER_GETNAME(VkZoomBlurLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkZoomBlurLayer(/* args */);
  virtual ~VkZoomBlurLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override;
};

// class VkMotionDetectorLayer : public VkGroupLayer,
//                               public IMotionDetectorLayer,
//                               public IOutputLayerObserver {
//   AVOX_LAYER_GETNAME(VkMotionDetectorLayer)
//  private:
//   std::unique_ptr<VkLowPassLayer> lowLayer = nullptr;
//   std::unique_ptr<VkReduceLayer> avageLayer = nullptr;
//   std::unique_ptr<VkOutputLayer> outLayer = nullptr;
//   IMotionDetectorObserver* observer = nullptr;

//  public:
//   VkMotionDetectorLayer();
//   virtual ~VkMotionDetectorLayer();

//  public:
//   virtual void setObserver(IMotionDetectorObserver* observer) final;

//  public:
//   virtual void onImageProcess(uint8_t* data, const ImageFormat& format,
//                               int32_t outIndex) final;
//   virtual void onFormatChanged(const ImageFormat& imageFormat,
//                                int32_t outIndex) override{};

//  protected:
//   virtual void onUpdateParamet() override;
//   virtual void onInitGroup() override;
//   virtual void onInitNode() override;
// };

}