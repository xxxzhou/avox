#include "VkCanvasLayer.hpp"

#include <cstring>

#include "avox_vulkan/VkHelper.hpp"
#include "avox_vulkan/layer/VkPipeGraph.hpp"

namespace avox {

VkCanvasLayer::VkCanvasLayer() {
  inCount = 1;
  glslPath = "glsl/canvasBlend.comp.spv";
  setUBOSize(sizeof(CanvasBlendParamet));
  updateUBO(&vkParamet);
  bParametChange = true;
}

VkCanvasLayer::~VkCanvasLayer() {}

void VkCanvasLayer::updateCanvas(const uint8_t* rgba, int32_t w, int32_t h,
                                 int32_t stride, int32_t x, int32_t y) {
  if (!rgba || w <= 0 || h <= 0) {
    return;
  }
  std::lock_guard<std::mutex> lock(mtx);
  if (frameW <= 0 || frameH <= 0) {
    // 图未建(首帧尺寸未知): 暂存, onInitLayer 后应用
    pendingCanvas.assign((size_t)h * stride, 0);
    memcpy(pendingCanvas.data(), rgba, (size_t)h * stride);
    pendingW = w;
    pendingH = h;
    pendingStride = stride;
    pendingX = x;
    pendingY = y;
    hasPending = true;
    return;
  }
  if (hasContent) {
    // 清上一帧 bbox(CPU 画布侧); GPU 侧旧区域在本帧已不在采样矩形内
    for (int32_t row = 0; row < rectH; ++row) {
      memset(canvasData.data() + (size_t)(rectY + row) * frameW * 4 +
                 (size_t)rectX * 4,
             0, (size_t)rectW * 4);
    }
  }
  // blit 新 bbox(裁到帧内)
  const int32_t cx0 = x < 0 ? 0 : x;
  const int32_t cy0 = y < 0 ? 0 : y;
  int32_t cx1 = x + w, cy1 = y + h;
  if (cx1 > frameW) cx1 = frameW;
  if (cy1 > frameH) cy1 = frameH;
  for (int32_t row = cy0; row < cy1; ++row) {
    const uint8_t* src = rgba + (size_t)(row - y) * stride + (size_t)(cx0 - x) * 4;
    uint8_t* dst = canvasData.data() + (size_t)row * frameW * 4 + (size_t)cx0 * 4;
    memcpy(dst, src, (size_t)(cx1 - cx0) * 4);
  }
  rectX = cx0;
  rectY = cy0;
  rectW = cx1 - cx0;
  rectH = cy1 - cy0;
  hasContent = rectW > 0 && rectH > 0;
  bNeedUpdate = true;
}

void VkCanvasLayer::clearCanvas() {
  std::lock_guard<std::mutex> lock(mtx);
  if (!hasContent) {
    return;
  }
  hasContent = false;
  bNeedUpdate = true;
}

void VkCanvasLayer::onInitGraph() {
  // descriptor set layout: inTexs[0](视频帧) + canvasImage(sampler) +
  // outTexs[0] + UBO — 与 VkFontLayer/VkGeometryLayer 同构
  std::vector<UBOLayoutItem> items = {
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT}};
  layout->addSetLayout(items);
  outFormats[0].imageType = ImageType::rgba8;
  VkLayer::onInitGraph();
}

void VkCanvasLayer::onInitLayer() {
  VkLayer::onInitLayer();
  if (inFormats.empty()) {
    return;
  }
  // 画布纹理 = 视频帧尺寸(ASS 排版坐标系), 不做缩放
  frameW = inFormats[0].width;
  frameH = inFormats[0].height;
  if (frameW <= 0 || frameH <= 0) {
    frameW = frameH = 0;
    return;
  }
  canvasData.assign((size_t)frameW * frameH * 4, 0);
  // staging(TRANSFER_SRC): 一次分配帧尺寸, 每次只上传当前 bbox 紧凑行
  cpuBuffer = std::make_unique<VkWrapBuffer>();
  cpuBuffer->setVkContext(vkPipeGraph);
  cpuBuffer->initResoure(BufferUsage::store, frameW * frameH * 4,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  applyPending();
}

void VkCanvasLayer::onInitPipe() {
  canvasImage = VulkanTexturePtr(new VkTexture());
  canvasImage->setVkContext(vkPipeGraph);
  canvasImage->InitResource(frameW, frameH, getVkFormat(ImageType::rgba8),
                            VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  canvasImage->createSampler(true);  // 线性采样(与 blend.comp 水印一致)
  canvasImage->descInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  inTexs[0]->descInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  outTexs[0]->descInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  layout->updateSetLayout(0, 0, &inTexs[0]->descInfo, &canvasImage->descInfo,
                          &outTexs[0]->descInfo, &constBuf->descInfo);
  auto computePipelineInfo =
      createComputePipelineInfo(layout->pipelineLayout, shader->shaderStage);
  vkCreateComputePipelines(vkDevice, vkPipeGraph->pipelineCache, 1,
                           &computePipelineInfo, nullptr, &computerPipeline);
}

void VkCanvasLayer::applyPending() {
  if (!hasPending) {
    return;
  }
  // 暂存件按当前帧参数走通用 blit(一次性; 调用方 onInitLayer 不持锁)
  std::vector<uint8_t> data;
  data.swap(pendingCanvas);
  const int32_t w = pendingW, h = pendingH, stride = pendingStride;
  const int32_t x = pendingX, y = pendingY;
  hasPending = false;
  pendingW = pendingH = pendingStride = pendingX = pendingY = 0;
  updateCanvas(data.data(), w, h, stride, x, y);
}

void VkCanvasLayer::onPreFrame() {
  VkLayer::onPreFrame();
  std::lock_guard<std::mutex> lock(mtx);
  if (!bNeedUpdate || !cpuBuffer) {
    return;
  }
  bNeedUpdate = false;
  if (!hasContent) {
    // 无字幕: 整层直通, 不上传
    vkParamet.opacity = 0.f;
    updateUBO(&vkParamet);
    bParametChange = true;
    return;
  }
  // 紧凑行拷入 staging: bufferRowLength=rectW, imageOffset=(rectX,rectY)
  uploadRows.resize((size_t)rectW * rectH * 4);
  for (int32_t row = 0; row < rectH; ++row) {
    memcpy(uploadRows.data() + (size_t)row * rectW * 4,
           canvasData.data() + (size_t)(rectY + row) * frameW * 4 +
               (size_t)rectX * 4,
           (size_t)rectW * 4);
  }
  cpuBuffer->upload(uploadRows.data(), (int32_t)uploadRows.size());
  vkParamet.centerX = (float)(rectX + rectW / 2) / frameW;
  vkParamet.centerY = (float)(rectY + rectH / 2) / frameH;
  vkParamet.width = (float)rectW / frameW;
  vkParamet.height = (float)rectH / frameH;
  vkParamet.opacity = 1.f;
  updateUBO(&vkParamet);
  bParametChange = true;
}

void VkCanvasLayer::onCommand() {
  if (!cpuBuffer || !canvasImage || outTexs.empty()) {
    return;
  }
  VkCommandBuffer cmd = getCurrentCmdBuffer();
  if (hasContent) {
    // 1. 子矩形拷贝: staging(紧凑 bbox 行) → canvasImage(rectX,rectY)
    canvasImage->addBarrier(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = rectW;  // 紧凑排列
    region.bufferImageHeight = rectH;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = rectX;
    region.imageOffset.y = rectY;
    region.imageExtent.width = rectW;
    region.imageExtent.height = rectH;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cmd, cpuBuffer->buffer, canvasImage->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    canvasImage->addBarrier(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_READ_BIT);
  }
  // 2. canvasBlend.comp: 无内容时 opacity=0, shader 直通把 base 写进输出 —
  //    层在图内就必须 dispatch, 否则输出纹理是未定义内存(灰带伪影)
  inTexs[0]->addBarrier(cmd, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT);
  outTexs[0]->addBarrier(cmd, VK_IMAGE_LAYOUT_GENERAL,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_ACCESS_SHADER_WRITE_BIT);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computerPipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          layout->pipelineLayout, 0, 1,
                          layout->descSets[0].data(), 0, 0);
  vkCmdDispatch(cmd, sizeX, sizeY, 1);
}

}
