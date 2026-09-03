#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "VkSeparableLinearLayer.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

// 按GPUImageColourFASTFeatureDetector里来说,应该先box blur一次.
// 然后把box blur纹理给VkColourFASTFeatureDetector第二个输入.
// 但是实际上在fastFeatureDetector中第二纹理并没参与任何事情.

class VkColourFASTFeatureDetector : public VkGroupLayer,
                                    public IParamet<FASTFeatureParamet> {
  AVOX_LAYER_GETNAME(VkColourFASTFeatureDetector)
 private:
  /* data */
  VKTNodePtr<VkBoxBlurSLayer> boxBlur = nullptr;

 public:
  VkColourFASTFeatureDetector(/* args */);
  virtual ~VkColourFASTFeatureDetector();

 protected:
  virtual void onUpdateParamet() override;
  virtual bool getSampled(int inIndex) override;
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
};

}