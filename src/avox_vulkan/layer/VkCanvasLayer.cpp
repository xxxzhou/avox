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

VkCanvasLayer::VkCanvasLayer(bool bLinearDomain) : VkCanvasLayer() {
  this->bLinearDomain = bLinearDomain;
}

VkCanvasLayer::~VkCanvasLayer() {}

void VkCanvasLayer::updateCanvas(const AssCanvas& canvas) {
  state.updateCanvas(canvas);
}

void VkCanvasLayer::clearCanvas() { state.clearCanvas(); }

void VkCanvasLayer::setCanvasTransform(float scale, float offsetX,
                                       float offsetY, float opacity) {
  state.setCanvasTransform(scale, offsetX, offsetY, opacity);
}

void VkCanvasLayer::onInitGraph() {
  // 双域变体(字幕画布多后端渲染计划 §5.4): forceHDR 拓扑帧在 16F 线性域
  // (1.0=80nit), canvas 采样在 shader 内线性化; SDR 拓扑照旧 rgba8 gamma 域
  if (bLinearDomain) {
    glslPath = "glsl/canvasBlendHDR.comp.spv";
    inFormats[0].imageType = ImageType::rgba16f;
  }
  // descriptor set layout: inTexs[0](视频帧) + canvasImage(sampler) +
  // outTexs[0] + UBO — 与 VkFontLayer/VkGeometryLayer 同构
  std::vector<UBOLayoutItem> items = {
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT}};
  layout->addSetLayout(items);
  outFormats[0].imageType =
      bLinearDomain ? ImageType::rgba16f : ImageType::rgba8;
  VkLayer::onInitGraph();
}

void VkCanvasLayer::onInitLayer() {
  VkLayer::onInitLayer();
  if (inFormats.empty()) {
    return;
  }
  // 画布纹理 = 合成画布尺寸(>1080p 恒 1920x1080 基准), 铺满全帧由 sampler
  // 归一化映射拉伸(与旧 VkFontLayer 同机制)
  int32_t cw = 0;
  int32_t ch = 0;
  subtitleCanvasSize(inFormats[0].width, inFormats[0].height, &cw, &ch);
  if (!state.reset(cw, ch)) {
    return;
  }
  // staging(TRANSFER_SRC): 一次分配画布尺寸, 每次只上传当前 bbox 紧凑行
  cpuBuffer = std::make_unique<VkWrapBuffer>();
  cpuBuffer->setVkContext(vkPipeGraph);
  cpuBuffer->initResoure(BufferUsage::store, cw * ch * 4,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  applyPending();
}

void VkCanvasLayer::onInitPipe() {
  canvasImage = VulkanTexturePtr(new VkTexture());
  canvasImage->setVkContext(vkPipeGraph);
  canvasImage->InitResource(state.width(), state.height(),
                            getVkFormat(ImageType::rgba8),
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
  if (!state.hasPending()) {
    return;
  }
  // 暂存件按当前帧参数走通用 blit(一次性; 调用方 onInitLayer 不持锁)
  std::vector<uint8_t> data;
  int32_t w = 0, h = 0, stride = 0, x = 0, y = 0;
  state.takePending(data, w, h, stride, x, y);
  updateCanvas(AssCanvas{data.data(), w, h, stride, x, y});
}

void VkCanvasLayer::onPreFrame() {
  VkLayer::onPreFrame();
  if (!cpuBuffer || state.width() <= 0) {
    return;
  }
  if (state.takeUploadDue()) {
    // 整画布上传(staging 常驻画布尺寸)。命令缓冲在 onInitBuffers 只录制
    // 一次并逐帧重提交, 拷贝区域必须是录制期确定的常量, 内容更新只能
    // 走 staging 数据; 带宽 ~8MB/次内容变化(对白节奏数秒一次), 可接受。
    cpuBuffer->upload(state.data(), state.width() * state.height() * 4);
  }
  // 门控矩形与采样映射每帧由 base bbox + 用户变换重算(不做增量累加,
  // 静止帧改变换也即时生效); 无内容时 opacity=0 强制直通(不能填用户值,
  // 否则去采样已清空的画布)。UBO 每帧提交: 内容首帧恰逢命令缓冲录制
  // 常量的时序下, 一次性提交会被吞掉; 常备提交成本可忽略(32 字节)
  vkParamet = state.computeParamet();
  updateUBO(&vkParamet);
  bParametChange = true;
}

void VkCanvasLayer::onCommand() {
  const int32_t cw = state.width();
  const int32_t ch = state.height();
  if (!cpuBuffer || !canvasImage || cw <= 0 || outTexs.empty()) {
    return;
  }
  VkCommandBuffer cmd = getCurrentCmdBuffer();
  // 1. staging(整画布) → canvasImage(整画布)。命令只在此处录制一次并逐帧重提交,
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
  region.imageExtent.width = (uint32_t)cw;
  region.imageExtent.height = (uint32_t)ch;
  region.imageExtent.depth = 1;
  vkCmdCopyBufferToImage(cmd, cpuBuffer->buffer, canvasImage->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  canvasImage->addBarrier(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_ACCESS_SHADER_READ_BIT);
  // 2. canvasBlend(.comp/HDR.comp): 无内容时 opacity=0, shader 直通把 base 写
  //    进输出 — 层在图内就必须 dispatch, 否则输出纹理是未定义内存(灰带伪影)
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
