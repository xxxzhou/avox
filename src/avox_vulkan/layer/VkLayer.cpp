#include "VkLayer.hpp"

#include "../vulkan/VkLayout.hpp"
#include "VkPipeGraph.hpp"

#ifdef _WIN32
#include <windows.h>
#elif __ANDROID__
#include <android/asset_manager.h>
#endif

namespace avox {

VkLayer::VkLayer(/* args */) { gpu = GpuType::vulkan; }

VkLayer::~VkLayer() { constBufCpu.clear(); }

void VkLayer::setUBOSize(int size, bool bMatchParamet) {
  conBufSize = size;
  constBufCpu.resize(conBufSize);
  bParametMatch = bMatchParamet;
}

void VkLayer::generateLayout() {
  if (layout->pipelineLayout != VK_NULL_HANDLE) {
    return;
  }
  std::vector<UBOLayoutItem> items;
  for (int i = 0; i < inCount; i++) {
    VkDescriptorType vdt = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    if (getSampled(i)) {
      vdt = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    items.push_back({vdt, VK_SHADER_STAGE_COMPUTE_BIT});
  }
  for (int i = 0; i < outCount; i++) {
    items.push_back(
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT});
  }
  if (constBuf) {
    items.push_back(
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
  }
  layout->addSetLayout(items);
  layout->generateLayout();
}

void VkLayer::updateUBO(void* data) {
  memcpy(constBufCpu.data(), data, conBufSize);
}

void VkLayer::submitUBO() {
  if (constBuf) {
    constBuf->upload(constBufCpu.data());
    constBuf->submit();
  }
}

void VkLayer::onInit() {
  // dynamic_cast android open rtti
  vkPipeGraph = static_cast<VkPipeGraph*>(pipeGraph);
  setVkContext(vkPipeGraph);
  if (!bInput) {
    inTexs.resize(inCount);
  }
  outTexs.resize(outCount);
  layout = std::make_unique<UBOLayout>();
  layout->setVkContext(vkPipeGraph);
  shader = std::make_unique<VkShader>();
  shader->setVkContext(vkPipeGraph);
  // 是否需要UBO
  if (conBufSize > 0) {
    constBuf = std::make_unique<VkWrapBuffer>();
    constBuf->setVkContext(vkPipeGraph);
    constBuf->initResoure(BufferUsage::store, conBufSize,
                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          constBufCpu.data());
  }
  onInitGraph();
}

void VkLayer::onUnInit() {
  // 从pipegraph拿下时重置所有
  layout.reset();
  shader.reset();
  constBuf.reset();
  for (int i = 0; i < outCount; i++) {
    outTexs[i].reset();
  }
  outTexs.clear();
}

void VkLayer::onInitLayer() {
  if (inCount > 0) {
    sizeX = divUp(inFormats[0].width, groupX);
    sizeY = divUp(inFormats[0].height, groupY);
  }
}

NodePtr<VkLayer> VkLayer::getNode() {
  return vkPipeGraph->getTNode(graphIndex);
}

VkCommandBuffer VkLayer::getCurrentCmdBuffer() {
  if (vkPipeGraph) {
    // 为双缓冲做准备
    return vkPipeGraph->getCurrentCmdBuffer();
  }
  return VK_NULL_HANDLE;
}

void VkLayer::createOutTexs() {
  outTexs.clear();
  for (int32_t i = 0; i < outCount; i++) {
    const ImageFormat& format = outFormats[i];
    if (!format.bVailid()) {
      log(LogLevel::warn, "layer:", getName(), " format incorrect");
    }
    VkFormat vkft = getVkFormat(format.imageType);
    VulkanTexturePtr texPtr(new VkTexture());
    // VkMemoryPropertyFlags
    VkMemoryPropertyFlags texFlags = VK_IMAGE_USAGE_STORAGE_BIT;
    auto& outLayers = vkPipeGraph->getTNode(graphIndex)->outNodes[i];
    // 需要检测当前层的输出层是否需要当前层的纹理需要采样
    bool bMustSampled = false;
    // 是否需要输出
    bool bMustOutput = false;
    for (int32_t i = 0; i < outLayers.size(); i++) {
      if (vkPipeGraph->getMustSampled(outLayers[i])) {
        bMustSampled = true;
      }
      if (vkPipeGraph->bOutLayer(outLayers[i].index)) {
        bMustOutput = true;
      };
    }
    VkMemoryPropertyFlags memoryFlag = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    // 查看对应输出层是否需要采样功能
    if (bMustSampled) {
      texFlags = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    // 输入与输出都给传输位
    if (bInput | bMustOutput | bMustClear) {
      texFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      texFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    texPtr->setVkContext(vkPipeGraph);
    texPtr->InitResource(format.width, format.height, vkft, texFlags,
                         memoryFlag);
    outTexs.push_back(texPtr);
  }
}

void VkLayer::clearColor(vec4f color) {
  VkCommandBuffer cmd = getCurrentCmdBuffer();
  for (int i = 0; i < outCount; i++) {
    // vkCmdClearColorImage 要求布局是 GENERAL 或 TRANSFER_DST_OPTIMAL
    // 如果当前是 UNDEFINED，使用 TRANSFER_DST_OPTIMAL（更高效）
    VkImageLayout targetLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if (outTexs[i]->layout != targetLayout) {
      // TRANSFER_DST_OPTIMAL 需要 TRANSFER_WRITE 访问权限
      outTexs[i]->addBarrier(cmd, targetLayout, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_ACCESS_TRANSFER_WRITE_BIT);
    }
    VkImageSubresourceRange subResourceRange = {};
    subResourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subResourceRange.baseMipLevel = 0;
    subResourceRange.levelCount = 1;
    subResourceRange.baseArrayLayer = 0;
    subResourceRange.layerCount = 1;

    VkClearColorValue clearColor = {color.x, color.y, color.z, color.w};
    vkCmdClearColorImage(cmd, outTexs[i]->image, targetLayout, &clearColor, 1,
                         &subResourceRange);
  }
}

void VkLayer::onInitBuffer() {
  if (!bInput) {
    inTexs.clear();
    for (int32_t i = 0; i < inCount; i++) {
      auto& inNode = vkPipeGraph->getTNode(graphIndex)->inNodes[i];
      inTexs.push_back(vkPipeGraph->getOutTex(inNode));
    }
  }
  if (!bOutput) {
    createOutTexs();
  }
  onInitVkBuffer();
  onInitPipe();
  // 默认更新一次UBO
  submitUBO();
}

bool VkLayer::onFrame() { return true; }

void VkLayer::onInitGraph() {
  if (!glslPath.empty()) {
    shader->loadShaderModule(glslPath);
  }
  generateLayout();
}

void VkLayer::onInitPipe() {
  std::vector<void*> bufferInfos;
  for (int i = 0; i < inCount; i++) {
    inTexs[i]->descInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    // 需要采样就提供,否则不提供
    if (getSampled(i)) {
      VkSampler sampler = vkPipeGraph->linearSampler;
      if (sampledNearest(i)) {
        sampler = vkPipeGraph->nearestSampler;
      }
      inTexs[i]->descInfo.sampler = sampler;
    }
    bufferInfos.push_back(&inTexs[i]->descInfo);
  }
  for (int i = 0; i < outCount; i++) {
    outTexs[i]->descInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    bufferInfos.push_back(&outTexs[i]->descInfo);
  }
  if (constBuf) {
    bufferInfos.push_back(&constBuf->descInfo);
  }
  layout->updateSetLayout(0, 0, bufferInfos);
  auto computePipelineInfo =
      createComputePipelineInfo(layout->pipelineLayout, shader->shaderStage);
  vkCreateComputePipelines(vkDevice, vkPipeGraph->pipelineCache, 1,
                           &computePipelineInfo, nullptr, &computerPipeline);
  if (!computerPipeline) {
    LOGFLF(LogLevel::warn, "not create computepipe");
  }
}

void VkLayer::onPreFrame() {
  if (bParametChange) {
    submitUBO();
    bParametChange = false;
  }
}

void VkLayer::onCommand() {
  assert(computerPipeline);
  VkCommandBuffer cmd = getCurrentCmdBuffer();
  for (int i = 0; i < inCount; i++) {
    inTexs[i]->addBarrier(cmd, VK_IMAGE_LAYOUT_GENERAL,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_ACCESS_SHADER_READ_BIT);
  }
  for (int i = 0; i < outCount; i++) {
    outTexs[i]->addBarrier(cmd, VK_IMAGE_LAYOUT_GENERAL,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_WRITE_BIT);
  }
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computerPipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          layout->pipelineLayout, 0, 1,
                          layout->descSets[0].data(), 0, 0);
  vkCmdDispatch(cmd, sizeX, sizeY, 1);
}

void VkGroupLayer::onInitGraph() {
  setInCount(inCount);
  // 默认当前节点为结束节点
  setEndNode(getNode());
  onInitGroup();
  InitNode();
  // 加载shader并生成pipelineLayout
  VkLayer::onInitGraph();
}

void VkGroupLayer::onInitPipe() {
  if (glslPath.empty()) {
    return;
  }
  VkLayer::onInitPipe();
}

void VkGroupLayer::onCommand() {
  if (glslPath.empty()) {
    return;
  }
  VkLayer::onCommand();
}

void VkGroupLayer::onVisible(bool visible) {
  if (endIndex >= 0) {
    NodePtr<VkLayer> cnode = vkPipeGraph->getTNode(endIndex);
    cnode->setVisible(visible);
  }
}

}
