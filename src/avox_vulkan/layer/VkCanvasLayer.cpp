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

void VkCanvasLayer::updateCanvas(const AssCanvas& canvas) {
  const uint8_t* rgba = canvas.rgba;
  const int32_t w = canvas.width;
  const int32_t h = canvas.height;
  const int32_t stride = canvas.stride;
  const int32_t x = canvas.x;
  const int32_t y = canvas.y;
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

void VkCanvasLayer::setCanvasTransform(float scale, float offsetX,
                                       float offsetY, float opacity) {
  std::lock_guard<std::mutex> lock(mtx);
  userScale = scale > 0.f ? scale : 1.f;
  userOffsetX = offsetX;
  userOffsetY = offsetY;
  userOpacity = std::min(std::max(opacity, 0.f), 1.f);
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
  updateCanvas(AssCanvas{data.data(), w, h, stride, x, y});
}

void VkCanvasLayer::onPreFrame() {
  VkLayer::onPreFrame();
  std::lock_guard<std::mutex> lock(mtx);
  if (!cpuBuffer || frameW <= 0) {
    return;
  }
  if (bNeedUpdate) {
    bNeedUpdate = false;
    if (hasContent) {
      // 整帧上传(staging 常驻全帧尺寸)。命令缓冲在 onInitBuffers 只录制
      // 一次并逐帧重提交, 拷贝区域必须是录制期确定的常量, 内容更新只能
      // 走 staging 数据; 带宽 ~8MB/次内容变化(对白节奏数秒一次), 可接受。
      cpuBuffer->upload(canvasData.data(), frameW * frameH * 4);
    }
  }
  // 门控矩形与采样映射每帧由 base bbox + 用户变换重算(不做增量累加,
  // 静止帧改变换也即时生效); 无内容时 opacity=0 强制直通(不能填用户值,
  // 否则去采样已清空的画布)
  if (!hasContent) {
    vkParamet.opacity = 0.f;
  } else {
    const float s = userScale;
    const float inv = 1.f / s;
    // 轴心 = 帧中心(ASS/PGS 锚点归片源不可知); offset 叠加在轴心上:
    // 正向 screen = P + (c - P) * s + o, 反算 suv = origin + uv * invScale
    const float qx = 0.5f + userOffsetX;
    const float qy = 0.5f + userOffsetY;
    const float baseCX = (float)(rectX + rectW / 2) / frameW;
    const float baseCY = (float)(rectY + rectH / 2) / frameH;
    vkParamet.centerX = qx + (baseCX - 0.5f) * s;
    vkParamet.centerY = qy + (baseCY - 0.5f) * s;
    vkParamet.width = (float)rectW / frameW * s;
    vkParamet.height = (float)rectH / frameH * s;
    vkParamet.originX = 0.5f - qx * inv;
    vkParamet.originY = 0.5f - qy * inv;
    vkParamet.invScale = inv;
    vkParamet.opacity = userOpacity;
  }
  // UBO 每帧提交: 内容首帧恰逢命令缓冲录制常量的时序下, 一次性提交会
  // 被吞掉; 常备提交成本可忽略(32 字节)
  updateUBO(&vkParamet);
  bParametChange = true;
}

void VkCanvasLayer::onCommand() {
  if (!cpuBuffer || !canvasImage || outTexs.empty()) {
    return;
  }
  VkCommandBuffer cmd = getCurrentCmdBuffer();
  // 1. staging(全帧) → canvasImage(全帧)。命令只在此处录制一次并逐帧重提交,
  //    因此拷贝区域必须是编译期(录制期)确定的常量; 内容更新走 staging 数据。
  canvasImage->addBarrier(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_ACCESS_TRANSFER_WRITE_BIT);
  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;  // 紧凑排列
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset.x = 0;
  region.imageOffset.y = 0;
  region.imageExtent.width = (uint32_t)frameW;
  region.imageExtent.height = (uint32_t)frameH;
  region.imageExtent.depth = 1;
  vkCmdCopyBufferToImage(cmd, cpuBuffer->buffer, canvasImage->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  canvasImage->addBarrier(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_ACCESS_SHADER_READ_BIT);
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
