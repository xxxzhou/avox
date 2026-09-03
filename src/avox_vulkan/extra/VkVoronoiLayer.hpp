#pragma once

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkVoronoiConsumerLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkVoronoiConsumerLayer)
 private:
  /* data */
 public:
  VkVoronoiConsumerLayer(/* args */);
  virtual ~VkVoronoiConsumerLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override;
  virtual void onInitLayer() override;
};

}