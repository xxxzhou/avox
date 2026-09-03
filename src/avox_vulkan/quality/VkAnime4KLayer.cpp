#include "VkAnime4KLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

namespace avox {

// --- Sub-layer implementations ---

VkAnime4KConv0Layer::VkAnime4KConv0Layer(const std::string& shaderPath) {
  glslPath = shaderPath;
  inCount = 1;
  outCount = 1;
  setUBOSize(sizeof(int32_t) * 2);
}
VkAnime4KConv0Layer::~VkAnime4KConv0Layer() {}
void VkAnime4KConv0Layer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba8;
  outFormats[0].imageType = ImageType::rgba32f;
  VkLayer::onInitGraph();
}
void VkAnime4KConv0Layer::onInitVkBuffer() {
  int32_t w = inFormats[0].width;
  int32_t h = inFormats[0].height;
  sizeX = divUp(w, groupX);
  sizeY = divUp(h, groupY);
  int32_t ubo[] = {w, h};
  updateUBO(ubo);
}

VkAnime4KConvLayer::VkAnime4KConvLayer(const std::string& shaderPath) {
  glslPath = shaderPath;
  inCount = 1;
  outCount = 1;
  setUBOSize(sizeof(int32_t) * 2);
}
VkAnime4KConvLayer::~VkAnime4KConvLayer() {}
void VkAnime4KConvLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba32f;
  outFormats[0].imageType = ImageType::rgba32f;
  VkLayer::onInitGraph();
}
void VkAnime4KConvLayer::onInitVkBuffer() {
  int32_t w = inFormats[0].width;
  int32_t h = inFormats[0].height;
  sizeX = divUp(w, groupX);
  sizeY = divUp(h, groupY);
  int32_t ubo[] = {w, h};
  updateUBO(ubo);
}

VkAnime4KRestoreOutputLayer::VkAnime4KRestoreOutputLayer(
    const std::string& shaderPath) {
  glslPath = shaderPath;
  // 8 inputs: MAIN (rgba8) + 7 conv textures (rgba32f)
  inCount = 8;
  outCount = 1;
  setUBOSize(sizeof(int32_t) * 2);
}
VkAnime4KRestoreOutputLayer::~VkAnime4KRestoreOutputLayer() {}
void VkAnime4KRestoreOutputLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba8;
  for (int32_t i = 1; i < inCount; i++) {
    inFormats[i].imageType = ImageType::rgba32f;
  }
  outFormats[0].imageType = ImageType::rgba8;
  VkLayer::onInitGraph();
}
void VkAnime4KRestoreOutputLayer::onInitVkBuffer() {
  int32_t w = inFormats[0].width;
  int32_t h = inFormats[0].height;
  sizeX = divUp(w, groupX);
  sizeY = divUp(h, groupY);
  int32_t ubo[] = {w, h};
  updateUBO(ubo);
}

VkAnime4KUpscaleOutputLayer::VkAnime4KUpscaleOutputLayer(
    const std::string& shaderPath) {
  glslPath = shaderPath;
  // 7 inputs: conv0-conv6 textures (all rgba32f, no MAIN)
  inCount = 7;
  outCount = 1;
  setUBOSize(sizeof(int32_t) * 2);
}
VkAnime4KUpscaleOutputLayer::~VkAnime4KUpscaleOutputLayer() {}
void VkAnime4KUpscaleOutputLayer::onInitGraph() {
  for (int32_t i = 0; i < inCount; i++) {
    inFormats[i].imageType = ImageType::rgba32f;
  }
  outFormats[0].imageType = ImageType::rgba32f;
  VkLayer::onInitGraph();
}
void VkAnime4KUpscaleOutputLayer::onInitVkBuffer() {
  int32_t w = inFormats[0].width;
  int32_t h = inFormats[0].height;
  sizeX = divUp(w, groupX);
  sizeY = divUp(h, groupY);
  int32_t ubo[] = {w, h};
  updateUBO(ubo);
}

VkAnime4KD2SLayer::VkAnime4KD2SLayer(const std::string& shaderPath) {
  glslPath = shaderPath;
  // 2 inputs: MAIN (rgba8) + convLast (rgba32f)
  inCount = 2;
  outCount = 1;
  setUBOSize(sizeof(int32_t) * 4);
}
VkAnime4KD2SLayer::~VkAnime4KD2SLayer() {}
void VkAnime4KD2SLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba8;
  inFormats[1].imageType = ImageType::rgba32f;
  outFormats[0].imageType = ImageType::rgba8;
  VkLayer::onInitGraph();
}
void VkAnime4KD2SLayer::onInitLayer() {
  // D2S output is 2x resolution
  sizeX = divUp(inFormats[0].width * 2, groupX);
  sizeY = divUp(inFormats[0].height * 2, groupY);
  outFormats[0].width = inFormats[0].width * 2;
  outFormats[0].height = inFormats[0].height * 2;
}

void VkAnime4KD2SLayer::onInitVkBuffer() {
  int32_t srcW = inFormats[0].width;
  int32_t srcH = inFormats[0].height;
  int32_t dstW = srcW * 2;
  int32_t dstH = srcH * 2;
  int32_t ubo[] = {srcW, srcH, dstW, dstH};
  updateUBO(ubo);
}

VkAnime4KClampHPass::VkAnime4KClampHPass(const std::string& shaderPath) {
  glslPath = shaderPath;
  inCount = 1;
  outCount = 1;
  setUBOSize(sizeof(int32_t) * 2);
}
VkAnime4KClampHPass::~VkAnime4KClampHPass() {}
void VkAnime4KClampHPass::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba8;
  outFormats[0].imageType = ImageType::r32f;
  VkLayer::onInitGraph();
}
void VkAnime4KClampHPass::onInitVkBuffer() {
  int32_t w = inFormats[0].width;
  int32_t h = inFormats[0].height;
  sizeX = divUp(w, groupX);
  sizeY = divUp(h, groupY);
  int32_t ubo[] = {w, h};
  updateUBO(ubo);
}

VkAnime4KClampVPass::VkAnime4KClampVPass(const std::string& shaderPath) {
  glslPath = shaderPath;
  inCount = 1;
  outCount = 1;
  setUBOSize(sizeof(int32_t) * 2);
}
VkAnime4KClampVPass::~VkAnime4KClampVPass() {}
void VkAnime4KClampVPass::onInitGraph() {
  inFormats[0].imageType = ImageType::r32f;
  outFormats[0].imageType = ImageType::r32f;
  VkLayer::onInitGraph();
}
void VkAnime4KClampVPass::onInitVkBuffer() {
  int32_t w = inFormats[0].width;
  int32_t h = inFormats[0].height;
  sizeX = divUp(w, groupX);
  sizeY = divUp(h, groupY);
  int32_t ubo[] = {w, h};
  updateUBO(ubo);
}

VkAnime4KClampApplyPass::VkAnime4KClampApplyPass(const std::string& shaderPath) {
  glslPath = shaderPath;
  // 2 inputs: srcTex (rgba8) + statsMaxTex (r32f)
  inCount = 2;
  outCount = 1;
  setUBOSize(sizeof(int32_t) * 2);
}
VkAnime4KClampApplyPass::~VkAnime4KClampApplyPass() {}
void VkAnime4KClampApplyPass::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba8;
  inFormats[1].imageType = ImageType::r32f;
  outFormats[0].imageType = ImageType::rgba8;
  VkLayer::onInitGraph();
}
void VkAnime4KClampApplyPass::onInitVkBuffer() {
  int32_t w = inFormats[0].width;
  int32_t h = inFormats[0].height;
  sizeX = divUp(w, groupX);
  sizeY = divUp(h, groupY);
  int32_t ubo[] = {w, h};
  updateUBO(ubo);
}

// --- VkAnime4KLayer (VkGroupLayer) implementation ---

VkAnime4KLayer::VkAnime4KLayer() {}
VkAnime4KLayer::~VkAnime4KLayer() {}

void VkAnime4KLayer::onUpdateParamet() {
  if (paramet == oldParamet) {
    return;
  }
  resetGraph();
}

void VkAnime4KLayer::onInitLayer() {
  // Upscale outputs 2x resolution, group outFormats must match
  if (paramet.mode == Anime4KMode::ModeA ||
      paramet.mode == Anime4KMode::ModeB || paramet.mode == Anime4KMode::ModeC) {
    outFormats[0].width = inFormats[0].width * 2;
    outFormats[0].height = inFormats[0].height * 2;
  }
}

void VkAnime4KLayer::onInitGroup() {
  inFormats[0].imageType = ImageType::rgba8;
  outFormats[0].imageType = ImageType::rgba8;
  bool useRestore = (paramet.mode == Anime4KMode::ModeA ||
                     paramet.mode == Anime4KMode::ModeB);
  bool useUpscale = true;
  bool useClamp = paramet.enableClampHighlights;
  int32_t convPasses = 6;  // M variant has 6 middle conv passes
  std::string restorePrefix = "glsl/anime4k_restore_m_";
  std::string upscalePrefix = "glsl/anime4k_upscale_m_";
  // Create Clamp Highlights sub-layers (stats computation runs first)
  if (useClamp) {
    clampHPass = vkPipeGraph->addNode<VkAnime4KClampHPass>(
        "glsl/anime4k_clamp_h.comp.spv");
    clampVPass = vkPipeGraph->addNode<VkAnime4KClampVPass>(
        "glsl/anime4k_clamp_v.comp.spv");
    clampHPass->addLine(clampVPass);
  }
  // Create Restore CNN sub-layers
  if (useRestore) {
    restoreConv0Layer = vkPipeGraph->addNode<VkAnime4KConv0Layer>(
        restorePrefix + "conv0.comp.spv");
    restoreConvLayers.resize(convPasses);
    for (int32_t i = 0; i < convPasses; i++) {
      restoreConvLayers[i] = vkPipeGraph->addNode<VkAnime4KConvLayer>(
          restorePrefix + "conv" + std::to_string(i + 1) + ".comp.spv");
    }
    restoreOutputLayer = vkPipeGraph->addNode<VkAnime4KRestoreOutputLayer>(
        restorePrefix + "output.comp.spv");
    // Wire Restore conv chain: conv0 -> conv1 -> ... -> conv6
    restoreConv0Layer->addLine(restoreConvLayers[0]);
    for (int32_t i = 1; i < convPasses; i++) {
      restoreConvLayers[i - 1]->addLine(restoreConvLayers[i]);
    }
  }
  // Create Upscale CNN sub-layers
  if (useUpscale) {
    upscaleConv0Layer = vkPipeGraph->addNode<VkAnime4KConv0Layer>(
        upscalePrefix + "conv0.comp.spv");
    upscaleConvLayers.resize(convPasses);
    for (int32_t i = 0; i < convPasses; i++) {
      upscaleConvLayers[i] = vkPipeGraph->addNode<VkAnime4KConvLayer>(
          upscalePrefix + "conv" + std::to_string(i + 1) + ".comp.spv");
    }
    upscaleOutputLayer = vkPipeGraph->addNode<VkAnime4KUpscaleOutputLayer>(
        upscalePrefix + "output.comp.spv");
    d2sLayer = vkPipeGraph->addNode<VkAnime4KD2SLayer>(
        upscalePrefix + "d2s.comp.spv");
    // Wire Upscale conv chain: conv0 -> conv1 -> ... -> conv6
    upscaleConv0Layer->addLine(upscaleConvLayers[0]);
    for (int32_t i = 1; i < convPasses; i++) {
      upscaleConvLayers[i - 1]->addLine(upscaleConvLayers[i]);
    }
  }
  if (useClamp) {
    clampApplyPass = vkPipeGraph->addNode<VkAnime4KClampApplyPass>(
        "glsl/anime4k_clamp_apply.comp.spv");
  }
}

void VkAnime4KLayer::onInitNode() {
  bool useRestore = (paramet.mode == Anime4KMode::ModeA ||
                     paramet.mode == Anime4KMode::ModeB);
  bool useUpscale = true;
  bool useClamp = paramet.enableClampHighlights;
  // Wire Restore output layer (8 inputs):
  //   input 0 (MAIN): original rgba8 input (from external, via setStartNode)
  //   inputs 1-7: conv0..conv6 outputs (from conv chain, via addLine)
  if (useRestore) {
    restoreConv0Layer->addLine(restoreOutputLayer, 0, 1);
    for (int32_t i = 0; i < (int32_t)restoreConvLayers.size(); i++) {
      restoreConvLayers[i]->addLine(restoreOutputLayer, 0, i + 2);
    }
  }
  // Wire Upscale output layer (7 inputs):
  //   inputs 0-6: conv0..conv6 outputs (from conv chain, via addLine)
  if (useUpscale) {
    upscaleConv0Layer->addLine(upscaleOutputLayer, 0, 0);
    for (int32_t i = 0; i < (int32_t)upscaleConvLayers.size(); i++) {
      upscaleConvLayers[i]->addLine(upscaleOutputLayer, 0, i + 1);
    }
    // Wire D2S layer (2 inputs):
    //   input 0 (MAIN): original rgba8 input (from external, via setStartNode)
    //   input 1 (convLast): upscale output (via addLine)
    upscaleOutputLayer->addLine(d2sLayer, 0, 1);
  }
  // Wire Clamp apply (2 inputs):
  //   input 0 (srcTex): final processing output (via addLine from last processing)
  //   input 1 (statsMaxTex): clampVPass output (via addLine)
  if (useClamp) {
    clampVPass->addLine(clampApplyPass, 0, 1);
  }
  // Set endNode to the actual final processing sub-layer
  // (GroupLayer's output must come from a sub-layer that writes compute output,
  //  not from the group node itself which has no shader)
  if (useUpscale && useClamp) {
    d2sLayer->addLine(clampApplyPass, 0, 0);
    setEndNode(clampApplyPass);
  } else if (useUpscale) {
    setEndNode(d2sLayer);
  } else if (useRestore && useClamp) {
    restoreOutputLayer->addLine(clampApplyPass, 0, 0);
    setEndNode(clampApplyPass);
  } else if (useRestore) {
    setEndNode(restoreOutputLayer);
  }
  // Connect Restore output -> Upscale conv0 (when both are active)
  if (useRestore && useUpscale) {
    restoreOutputLayer->addLine(upscaleConv0Layer);
  }
  // Set start nodes: all layers that need the external rgba8 input
  // Multiple setStartNode calls register multiple start nodes for the same
  // group input slot — PipeGraph connects external input to all of them.
  if (useRestore) {
    setStartNode(restoreConv0Layer, 0, 0);
  } else {
    setStartNode(upscaleConv0Layer, 0, 0);
  }
  // Restore output layer MAIN input (for residual)
  if (useRestore) {
    setStartNode(restoreOutputLayer, 0, 0);
  }
  // D2S MAIN input (for residual, always gets original rgba8)
  if (useUpscale) {
    setStartNode(d2sLayer, 0, 0);
  }
  // Clamp stats computation reads original input
  if (useClamp) {
    setStartNode(clampHPass, 0, 0);
  }
}

}