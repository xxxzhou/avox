#include "VkFSRLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

namespace avox {

VkFSRLayer::VkFSRLayer() {
  paramet.deblockMode = DeblockMode::Bilateral;
  paramet.rcasSharpness = 0.0f;  // 0=最大锐化 (exp2(0)=1.0)
  paramet.scale = FSRScale::Restore;
}

VkFSRLayer::~VkFSRLayer() {}

void VkFSRLayer::onUpdateParamet() {
  if (paramet == oldParamet) {
    return;
  }
  // 去块在 sRGB 空间 (rgba8) 做: sigma_color/eps 参数匹配 sRGB 值域,
  // 线性空间暗部值极小(0.01级), sigma_color=0.3 远大于暗部差异→全权重≈1→blur
  if (bilateralLayer) {
    BilateralParamet bp = {};
    bp.kernelSize = 5;
    bp.sigma_spatial = 3.0f;
    if (paramet.deblockStrength <= 0.0f) {
      bp.sigma_color = 0.1f;
    } else {
      // sRGB 空间: sigma_color 0.3~2.3, 块边界差异 ~0.05-0.15
      bp.sigma_color = 0.3f + paramet.deblockStrength * 2.0f;
    }
    bilateralLayer->get()->updateParamet(bp);
  }
  if (guidedDeblockLayer) {
    GuidedParamet gp = {};
    gp.boxSize = 5;
    // sRGB 空间: eps 0.001~0.05, 方差量级 ~0.01-0.1
    gp.eps = 0.001f + paramet.deblockStrength * 0.05f;
    guidedDeblockLayer->get()->updateParamet(gp);
  }
  if (rcasLayer) {
    rcasLayer->get()->setSharpness(paramet.rcasSharpness);
  }
  // 仅结构性变化才重建: scale/enableRCAS/deblockMode 变→子层组合变。
  // deblockStrength/rcasSharpness 只改 UBO/参数, 不重建。
  // enableLinear 不再重建: decode/encode 始终存在, sRGB↔linear 不影响子层结构。
  if (paramet.scale != oldParamet.scale ||
      paramet.enableRCAS != oldParamet.enableRCAS ||
      paramet.deblockMode != oldParamet.deblockMode) {
    resetGraph();
  }
}

void VkFSRLayer::onInitGroup() {
  inFormats[0].imageType = ImageType::rgba8;
  outFormats[0].imageType = ImageType::rgba8;
  int32_t scale = 1;
  if (paramet.scale == FSRScale::Upscale2x) {
    scale = 2;
  } else if (paramet.scale == FSRScale::Upscale4x) {
    scale = 4;
  }  // Restore: scale=1, EASU走1x不放大
  bool useRCAS = paramet.enableRCAS;
  // 管线: deblock(sRGB rgba8) → decode(rgba8→rgba16f linear) → EASU? → RCAS → encode(rgba16f→rgba8)
  // 去块在 sRGB 空间做: sigma_color/eps 参数匹配 sRGB 值域(0~1 均匀分布),
  // 线性空间暗部值极小(0.01级), sigma_color 远大于暗部差异→全权重≈1→blur
  // EASU/RCAS 在线性空间做: FSR 官方要求, 放大+锐化在 linear light 下正确
  // ① 去块: sRGB 空间 (rgba8), bilateral 或 guided
  bool useGuided = paramet.deblockMode == DeblockMode::Guided;
  if (useGuided) {
    guidedDeblockLayer = vkPipeGraph->addNode<VkGuidedDeblockLayer>();
    GuidedParamet guidedParamet = {};
    guidedParamet.boxSize = 5;
    guidedParamet.eps = 0.001f + paramet.deblockStrength * 0.05f;
    guidedDeblockLayer->get()->updateParamet(guidedParamet);
  } else {
    bilateralLayer = vkPipeGraph->addNode<VkBilateralLayer>(ImageType::rgba8);
    BilateralParamet bilateralParamet = {};
    bilateralParamet.kernelSize = 5;
    bilateralParamet.sigma_spatial = 3.0f;
    if (paramet.deblockStrength <= 0.0f) {
      bilateralParamet.sigma_color = 0.1f;
    } else {
      bilateralParamet.sigma_color = 0.3f + paramet.deblockStrength * 2.0f;
    }
    bilateralLayer->get()->updateParamet(bilateralParamet);
  }
  // ② sRGB→线性光: 去块后转线性, EASU/RCAS 在线性空间
  decodeLayer = vkPipeGraph->addNode<VkFSRDecodeLayer>();
  encodeLayer = vkPipeGraph->addNode<VkFSREncodeLayer>();
  // ③ EASU 保边放大: Restore(scale=1) 不放大, 跳过 EASU 省 12-tap 采样开销;
  //   2x/4x 放大才创建 EASU, dispatch 在输出分辨率。
  bool needEasu = scale > 1;
  if (needEasu) {
    easuLayer = vkPipeGraph->addNode<VkFSREASULayer>();
    easuLayer->get()->setScale(scale);
  }
  // ④ 可选锐化: RCAS 或普通 Sharpen
  // RCAS 原生支持 rgba16f; Sharpen 只支持 rgba8, 需前后加格式转换
  if (useRCAS) {
    rcasLayer = vkPipeGraph->addNode<VkFSRRCASLayer>();
    rcasLayer->get()->setSharpness(paramet.rcasSharpness);
  } else {
    sharpenPreConvert =
        vkPipeGraph->addNode<VkConvertImageLayer>(ConvertType::rgba16f2rgba32f);
    sharpenMidConvert =
        vkPipeGraph->addNode<VkConvertImageLayer>(ConvertType::rgba32f2rgba8);
    sharpenLayer = vkPipeGraph->addNode<VkSharpenLayer>();
    sharpenPostConvert =
        vkPipeGraph->addNode<VkConvertImageLayer>(ConvertType::rgba82rgba32f);
    sharpenEndConvert =
        vkPipeGraph->addNode<VkConvertImageLayer>(ConvertType::rgba32f2rgba16f);
    SharpenParamet sharpenParamet = {};
    sharpenParamet.offset = 1;
    sharpenParamet.sharpness = 0.3f;
    sharpenLayer->get()->updateParamet(sharpenParamet);
  }
  // 串联: deblock(sRGB) → decode(sRGB→linear) → EASU? → RCAS/Sharpen → encode(linear→sRGB)
  // 注: VKTNodePtr 类型不同, 不能用三元运算符, 必须用 if/else
  // deblock → decode
  if (useGuided) {
    guidedDeblockLayer->addLine(decodeLayer);
  } else {
    bilateralLayer->addLine(decodeLayer);
  }
  // decode → (easu?) → sharpen/RCAS → encode
  if (needEasu) {
    decodeLayer->addLine(easuLayer);
    if (useRCAS) {
      easuLayer->addLine(rcasLayer);
      rcasLayer->addLine(encodeLayer);
    } else {
      easuLayer->addLine(sharpenPreConvert);
      sharpenPreConvert->addLine(sharpenMidConvert);
      sharpenMidConvert->addLine(sharpenLayer);
      sharpenLayer->addLine(sharpenPostConvert);
      sharpenPostConvert->addLine(sharpenEndConvert);
      sharpenEndConvert->addLine(encodeLayer);
    }
  } else {
    if (useRCAS) {
      decodeLayer->addLine(rcasLayer);
      rcasLayer->addLine(encodeLayer);
    } else {
      decodeLayer->addLine(sharpenPreConvert);
      sharpenPreConvert->addLine(sharpenMidConvert);
      sharpenMidConvert->addLine(sharpenLayer);
      sharpenLayer->addLine(sharpenPostConvert);
      sharpenPostConvert->addLine(sharpenEndConvert);
      sharpenEndConvert->addLine(encodeLayer);
    }
  }
}

void VkFSRLayer::onInitNode() {
  // 从 deblock 开始 (sRGB rgba8 空间)
  bool useGuided = paramet.deblockMode == DeblockMode::Guided;
  if (useGuided) {
    setStartNode(guidedDeblockLayer, 0, 0);
  } else {
    setStartNode(bilateralLayer, 0, 0);
  }
  // 到 encode 结束 (rgba16f→rgba8)
  setEndNode(encodeLayer);
}

void VkFSRLayer::onInitLayer() {
  int32_t scale = 1;
  if (paramet.scale == FSRScale::Upscale2x) {
    scale = 2;
  } else if (paramet.scale == FSRScale::Upscale4x) {
    scale = 4;
  }
  outFormats[0].width = inFormats[0].width * scale;
  outFormats[0].height = inFormats[0].height * scale;
}

}
