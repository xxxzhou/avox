#include "VkGuidedDeblockLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

namespace avox {

VkToMatDeblockLayer::VkToMatDeblockLayer() {
  glslPath = "glsl/guidedFilter1Deblock.comp.spv";
  inCount = 1;
  outCount = 1;
}

VkToMatDeblockLayer::~VkToMatDeblockLayer() {}

void VkToMatDeblockLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba32f;
  outFormats[0].imageType = ImageType::rgba32f;
  VkLayer::onInitGraph();
}

VkGuidedSolveDeblockLayer::VkGuidedSolveDeblockLayer() {
  setUBOSize(sizeof(paramet), true);
  glslPath = "glsl/guidedFilter2Deblock.comp.spv";
  inCount = 2;
  outCount = 1;
  paramet = 0.000001f;
  updateUBO(&paramet);
}

VkGuidedSolveDeblockLayer::~VkGuidedSolveDeblockLayer() {}

void VkGuidedSolveDeblockLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba32f;
  inFormats[1].imageType = ImageType::rgba32f;
  outFormats[0].imageType = ImageType::rgba32f;
  VkLayer::onInitGraph();
}

VkGuidedDeblockLayer::VkGuidedDeblockLayer() {
  // group 自身持输出 shader (逐通道自引导 q = mean_c + a·(I-mean_c))
  glslPath = "glsl/guidedDeblock.comp.spv";
  inCount = 3;   // [0]=I(rgba32f), [1]=mean_c(rgba32f), [2]=a(rgba32f)
  outCount = 1;  // rgba16f
}

VkGuidedDeblockLayer::~VkGuidedDeblockLayer() {}

void VkGuidedDeblockLayer::onUpdateParamet() {
  if (paramet.boxSize != oldParamet.boxSize) {
    box1Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
    box2Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
    if (box5Layer) {
      box5Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
    }
  }
  if (paramet.eps != oldParamet.eps) {
    solveDeblockLayer->get()->updateParamet(paramet.eps);
  }
}

void VkGuidedDeblockLayer::onInitGroup() {
  // group 自身作为输出节点的输入输出格式
  // in[0]=I(rgba32f, from convert rgba8→rgba32f): 原始帧的 float 表示, 值域 0~1
  // in[1]=mean_c(rgba32f), in[2]=a(rgba32f)
  // out=rgba8 (sRGB 空间, 去块在 sRGB 做, eps 匹配 sRGB 值域)
  inFormats[0].imageType = ImageType::rgba32f;  // 原始 I (from convert)
  inFormats[1].imageType = ImageType::rgba32f;  // mean_c (from box1 or resize1m)
  inFormats[2].imageType = ImageType::rgba32f;  // a (from solve or resize1)
  outFormats[0].imageType = ImageType::rgba8;

  convertLayer = vkPipeGraph->addNode<VkConvertImageLayer>(ConvertType::rgba82rgba32f);
  toMatLayer = vkPipeGraph->addNode<VkToMatDeblockLayer>();
  box1Layer = vkPipeGraph->addNode<VkBoxBlurSLayer>(ImageType::rgba32f);
  box2Layer = vkPipeGraph->addNode<VkBoxBlurSLayer>(ImageType::rgba32f);
  solveDeblockLayer = vkPipeGraph->addNode<VkGuidedSolveDeblockLayer>();
  box1Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
  box2Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
  solveDeblockLayer->get()->updateParamet(paramet.eps);
  if (zoom > 1) {
    // zoom>1: 需要下采样→计算→上采样, box5 平滑 a 避免上采样插值伪影
    resizeLayer = vkPipeGraph->addNode<VkSizeScaleLayer>(ImageType::rgba32f);
    box5Layer = vkPipeGraph->addNode<VkBoxBlurSLayer>(ImageType::rgba32f);
    resize1Layer = vkPipeGraph->addNode<VkSizeScaleLayer>(ImageType::rgba32f);
    resize1mLayer = vkPipeGraph->addNode<VkSizeScaleLayer>(ImageType::rgba32f);
    resizeLayer->get()->updateParamet({1, 1.0f / zoom, 1.0f / zoom});
    resize1Layer->get()->updateParamet({1, (float)zoom, (float)zoom});
    resize1mLayer->get()->updateParamet({1, (float)zoom, (float)zoom});
    box5Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
    // 子链: convert → resize(1/zoom) → toMat(c²)
    convertLayer->addLine(resizeLayer)->addLine(toMatLayer);
  } else {
    // zoom=1: 不需要 resize(恒等拷贝)和 box5(a 不需要再平滑)
    // 子链: convert → toMat(c²)  (box1/box2 直接在原图分辨率计算)
    convertLayer->addLine(toMatLayer);
  }
}

void VkGuidedDeblockLayer::onInitNode() {
  if (zoom > 1) {
    resizeLayer->addLine(box1Layer, 0, 0);         // box1 <- resize (mean_c)
    toMatLayer->addLine(box2Layer, 0, 0);          // box2 <- toMat (mean_cc)
    box1Layer->addLine(solveDeblockLayer, 0, 0);   // solve in0 <- box1 (mean_c)
    box2Layer->addLine(solveDeblockLayer, 0, 1);   // solve in1 <- box2 (mean_cc)
    solveDeblockLayer->addLine(box5Layer);         // box5 <- solve (a)
    box5Layer->addLine(resize1Layer);              // resize1 <- box5 (a 上采样)
    box1Layer->addLine(resize1mLayer);             // resize1m <- box1 (mean_c 上采样)
  } else {
    // zoom=1: convert 直接给 box1, box1/box2 直接给 solve, solve/box1 直接送 output
    convertLayer->addLine(box1Layer, 0, 0);         // box1 <- convert (mean_c)
    toMatLayer->addLine(box2Layer, 0, 0);          // box2 <- toMat (mean_cc)
    box1Layer->addLine(solveDeblockLayer, 0, 0);   // solve in0 <- box1 (mean_c)
    box2Layer->addLine(solveDeblockLayer, 0, 1);   // solve in1 <- box2 (mean_cc)
    // solve 直接送 output (a 不需要 box5 再平滑)
    solveDeblockLayer->addLine(getNode(), 0, 2);    // group in2 <- solve (a)
    // box1 直接送 output (mean_c 不需要 resize 上采样)
    box1Layer->addLine(getNode(), 0, 1);            // group in1 <- box1 (mean_c)
  }
  // group 自身作为输出节点: in0=I(convert)
  convertLayer->addLine(getNode(), 0, 0);
  setStartNode(convertLayer);
}

void VkGuidedDeblockLayer::onInitLayer() {
  VkLayer::onInitLayer();
  if (zoom > 1) {
    // VkSizeScaleLayer.onInitLayer 用 fx 算好 outFormats 后, 会把 fx 取反写入 UBO,
    // 取反的 fx 留在 paramet 里, 下次重建 onInitLayer 会用错值。
    // 这里在子层 onInitLayer 之后把 fx 重置回原值供下次重建。
    resizeLayer->get()->updateParamet({1, 1.0f / zoom, 1.0f / zoom});
    resize1Layer->get()->updateParamet({1, (float)zoom, (float)zoom});
    resize1mLayer->get()->updateParamet({1, (float)zoom, (float)zoom});
  }
}

}
