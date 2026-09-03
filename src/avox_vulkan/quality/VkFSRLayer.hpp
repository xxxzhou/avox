#pragma once

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"
#include "../extra/VkBilateralLayer.hpp"
#include "../extra/VkConvertImageLayer.hpp"
#include "../extra/VkImageProcessing.hpp"
#include "VkFSREASULayer.hpp"
#include "VkFSRRCASLayer.hpp"
#include "VkFSRDecodeLayer.hpp"
#include "VkFSREncodeLayer.hpp"
#include "VkGuidedDeblockLayer.hpp"
#include "avox/AvoxLayer.h"

namespace avox {

// FSR实时增强组合层 (VkGroupLayer)
// 管线: deblock(sRGB rgba8) → decode(sRGB→linear rgba16f) → EASU保边放大 → RCAS/Sharpen锐化 → encode(linear→sRGB rgba8)
// 去块在 sRGB 空间做: sigma_color/eps 参数匹配 sRGB 值域(0~1 均匀分布),
//   线性空间暗部值极小(0.01级), sigma_color 远大于暗部差异→全权重≈1→blur
// EASU/RCAS 在线性空间做: FSR 官方要求, 放大+锐化在 linear light 下正确
// 去块模式由 deblockMode 控制: Bilateral=双边滤波(推荐), Guided=导向滤波自引导
class VkFSRLayer : public VkGroupLayer, public IParamet<FSRParamet> {
  AVOX_LAYER_GETNAME(VkFSRLayer)

 private:
  // 子层
  VKTNodePtr<VkFSRDecodeLayer> decodeLayer = nullptr;
  VKTNodePtr<VkBilateralLayer> bilateralLayer = nullptr;
  VKTNodePtr<VkGuidedDeblockLayer> guidedDeblockLayer = nullptr;
  VKTNodePtr<VkFSREASULayer> easuLayer = nullptr;
  VKTNodePtr<VkFSRRCASLayer> rcasLayer = nullptr;
  VKTNodePtr<VkSharpenLayer> sharpenLayer = nullptr;
  VKTNodePtr<VkFSREncodeLayer> encodeLayer = nullptr;
  // Sharpen 格式转换 (Sharpen 只支持 rgba8, 需 rgba16f→rgba8→Sharpen→rgba8→rgba16f)
  VKTNodePtr<VkConvertImageLayer> sharpenPreConvert = nullptr;
  VKTNodePtr<VkConvertImageLayer> sharpenMidConvert = nullptr;
  VKTNodePtr<VkConvertImageLayer> sharpenPostConvert = nullptr;
  VKTNodePtr<VkConvertImageLayer> sharpenEndConvert = nullptr;

 public:
  VkFSRLayer();
  virtual ~VkFSRLayer();

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
  virtual void onInitLayer() override;
};

}
