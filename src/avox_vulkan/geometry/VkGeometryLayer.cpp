#include "VkGeometryLayer.hpp"

#include <cmath>

#include "GeometryRender.hpp"
#include "avox_vulkan/VkHelper.hpp"
#include "avox_vulkan/layer/VkPipeGraph.hpp"

namespace avox {

VkGeometryLayer::VkGeometryLayer() {
  inCount = 1;
  glslPath = "glsl/drawShapeBlend.comp.spv";
  setUBOSize(sizeof(GeoBlendParamet));
  gParamet.color = {1.0f, 1.0f, 1.0f};
  gParamet.threshold = 0.5f;
  updateUBO(&gParamet);
  bParametChange = true;
  cpuCanvas = std::make_unique<ImageBuffer>();
}

VkGeometryLayer::~VkGeometryLayer() {}

void VkGeometryLayer::setScale(float scale_) {
  if (scale_ <= 0.0f) {
    scale_ = 1.0f;
  }
  scale = scale_;
  float newTscale = scale * dpiScale;
  if (newTscale != tscale) {
    tscale = newTscale;
    resetGraph();
  }
}

void VkGeometryLayer::onInitGraph() {
  // descriptor set layout: inTexs[0](视频帧) + canvasImage(sampler) +
  // outTexs[0] + UBO
  std::vector<UBOLayoutItem> items = {
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
       VK_SHADER_STAGE_COMPUTE_BIT},  // 0: inTexs[0]
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       VK_SHADER_STAGE_COMPUTE_BIT},  // 1: canvasImage + sampler
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
       VK_SHADER_STAGE_COMPUTE_BIT},  // 2: outTexs[0]
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
       VK_SHADER_STAGE_COMPUTE_BIT}};  // 3: UBO
  layout->addSetLayout(items);
  outFormats[0].imageType = ImageType::rgba8;
  VkLayer::onInitGraph();  // 加载 shader + generateLayout
}

void VkGeometryLayer::onInitLayer() {
  VkLayer::onInitLayer();
  if (inFormats.size() > 0) {
    // DPI 缩放：以 1080p 为参考
    constexpr int32_t kReferenceHeight = 1080;
    dpiScale = (float)inFormats[0].height / kReferenceHeight;
    tscale = scale * dpiScale;
    // canvas 大小 = 帧大小 / tscale（tscale 即 S=帧/canvas）
    canvasWidth = (int32_t)(inFormats[0].width / tscale);
    canvasHeight = (int32_t)(inFormats[0].height / tscale);
    if (canvasWidth <= 0) canvasWidth = 1;
    if (canvasHeight <= 0) canvasHeight = 1;
    // 创建 cpuCanvas（R8）
    ImageFormat canvasFormat = {};
    canvasFormat.width = canvasWidth;
    canvasFormat.height = canvasHeight;
    canvasFormat.imageType = ImageType::r8;
    cpuCanvas->setImageFormat(canvasFormat);
    // 先清零，避免首帧未光栅化时读到脏数据
    memset(cpuCanvas->getPointer(), 0, canvasWidth * canvasHeight);
    // staging buffer
    int32_t canvasSize = canvasWidth * canvasHeight;
    cpuBuffer = std::make_unique<VkWrapBuffer>();
    cpuBuffer->setVkContext(vkPipeGraph);
    cpuBuffer->initResoure(BufferUsage::store, canvasSize,
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  }
}

void VkGeometryLayer::onInitPipe() {
  // 内部 canvas image（SAMPLED_BIT 用于 sampler 采样）
  canvasImage = VulkanTexturePtr(new VkTexture());
  canvasImage->setVkContext(vkPipeGraph);
  VkFormat canvasFormat = getVkFormat(ImageType::r8);
  canvasImage->InitResource(
      canvasWidth, canvasHeight, canvasFormat,
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  canvasImage->createSampler(true);  // 线性采样：放大产生 0~1 渐变
  canvasImage->descInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  // 更新 descriptor set
  inTexs[0]->descInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  outTexs[0]->descInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  layout->updateSetLayout(0, 0, &inTexs[0]->descInfo, &canvasImage->descInfo,
                          &outTexs[0]->descInfo, &constBuf->descInfo);
  auto computePipelineInfo =
      createComputePipelineInfo(layout->pipelineLayout, shader->shaderStage);
  vkCreateComputePipelines(vkDevice, vkPipeGraph->pipelineCache, 1,
                           &computePipelineInfo, nullptr, &computerPipeline);
}

void VkGeometryLayer::onPreFrame() {
  VkLayer::onPreFrame();
  if (!cpuCanvas) {
    return;
  }
  // rasterDirty（形状/canvas 尺寸变化）才 rasterize + upload；paramDirty 仅 updateUBO
  GeoSnapshot snap;
  bool needRaster = source && source->consumeRasterDirty(snap);
  bool needParam = source && source->consumeParamDirty(snap);
  if (!needRaster && !needParam) {
    return;
  }
  if (needParam) {
    gParamet.color = {snap.r, snap.g, snap.b};
    gParamet.threshold = snap.threshold;
    updateUBO(&gParamet);
    bParametChange = true;
  }
  if (needRaster) {
    rasterize(snap);
    cpuBuffer->upload(cpuCanvas->getPointer(), canvasWidth * canvasHeight);
  }
}

void VkGeometryLayer::onCommand() {
  if (!cpuBuffer || outTexs.empty() || !canvasImage) {
    return;
  }
  VkCommandBuffer cmd = getCurrentCmdBuffer();
  // 1. bufferToImage: cpuBuffer -> canvasImage
  canvasImage->addBarrier(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_ACCESS_TRANSFER_WRITE_BIT);
  bufferToImage(cmd, cpuBuffer.get(), canvasImage.get());
  // 2. drawShapeBlend.comp: inTexs[0] + canvasImage -> outTexs[0]
  canvasImage->addBarrier(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_ACCESS_SHADER_READ_BIT);
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

float VkGeometryLayer::distPointSegment(float px, float py, float x0, float y0,
                                        float x1, float y1) {
  float dx = x1 - x0;
  float dy = y1 - y0;
  float len2 = dx * dx + dy * dy;
  float t = 0.0f;
  if (len2 > 0.0f) {
    t = ((px - x0) * dx + (py - y0) * dy) / len2;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
  }
  float projX = x0 + t * dx;
  float projY = y0 + t * dy;
  float ex = px - projX;
  float ey = py - projY;
  return sqrtf(ex * ex + ey * ey);
}

inline void VkGeometryLayer::putPixel(uint8_t* data, int x, int y) {
  if (x < 0 || x >= canvasWidth || y < 0 || y >= canvasHeight) {
    return;
  }
  data[y * canvasWidth + x] = 255;
}

inline void VkGeometryLayer::putAlpha(uint8_t* data, int x, int y, float a) {
  if (x < 0 || x >= canvasWidth || y < 0 || y >= canvasHeight) {
    return;
  }
  if (a <= 0.0f) {
    return;
  }
  if (a > 1.0f) {
    a = 1.0f;
  }
  int idx = y * canvasWidth + x;
  uint8_t v = (uint8_t)(a * 255.0f + 0.5f);
  if (v > data[idx]) {
    data[idx] = v;
  }
}

void VkGeometryLayer::rasterDisc(uint8_t* data, float cx, float cy, float r) {
  // 边缘 1px AA 带：内部写 255，边缘按距圆周距离写中间值
  int x0 = (int)floorf(cx - r - 1.0f);
  int x1 = (int)ceilf(cx + r + 1.0f);
  int y0 = (int)floorf(cy - r - 1.0f);
  int y1 = (int)ceilf(cy + r + 1.0f);
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      float dx = (x + 0.5f) - cx;
      float dy = (y + 0.5f) - cy;
      float dist = sqrtf(dx * dx + dy * dy);
      float sdf = dist - r;  // <0 内部, >0 外部
      if (sdf < -1.0f) {
        putPixel(data, x, y);  // 内部，写 255
      } else if (sdf < 1.0f) {
        // 边缘 1px AA：sdf 从 -1→1 映射 alpha 从 255→0
        float a = 1.0f - (sdf + 1.0f) * 0.5f;  // sdf=-1→1.0, sdf=1→0.0
        putAlpha(data, x, y, a);
      }
    }
  }
}

void VkGeometryLayer::rasterRing(uint8_t* data, float cx, float cy, float r) {
  // 环：边缘 1px AA，按到圆周距离写渐变 alpha
  int x0 = (int)floorf(cx - r - 2.0f);
  int x1 = (int)ceilf(cx + r + 2.0f);
  int y0 = (int)floorf(cy - r - 2.0f);
  int y1 = (int)ceilf(cy + r + 2.0f);
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      float dx = (x + 0.5f) - cx;
      float dy = (y + 0.5f) - cy;
      float dist = sqrtf(dx * dx + dy * dy);
      float sdf = fabsf(dist - r);  // 到圆周距离
      if (sdf < 1.0f) {
        // 环带内 1px AA：sdf 0→1 映射 alpha 1.0→0.0
        float a = 1.0f - sdf;
        putAlpha(data, x, y, a);
      }
    }
  }
}

void VkGeometryLayer::rasterLine(uint8_t* data, float x0, float y0, float x1,
                                 float y1) {
  // 2px core(dist<=1) 写 255 + 两侧 1px AA 渐变(1<dist<2)，消除斜线阶梯锯齿
  float minX = fminf(x0, x1) - 2.5f;
  float maxX = fmaxf(x0, x1) + 2.5f;
  float minY = fminf(y0, y1) - 2.5f;
  float maxY = fmaxf(y0, y1) + 2.5f;
  int ix0 = (int)floorf(minX);
  int ix1 = (int)ceilf(maxX);
  int iy0 = (int)floorf(minY);
  int iy1 = (int)ceilf(maxY);
  for (int y = iy0; y <= iy1; ++y) {
    for (int x = ix0; x <= ix1; ++x) {
      float px = x + 0.5f;
      float py = y + 0.5f;
      float dist = distPointSegment(px, py, x0, y0, x1, y1);
      if (dist <= 1.0f) {
        putPixel(data, x, y);  // core, α=255
      } else if (dist < 2.0f) {
        // 边缘 AA：dist 1→2 映射 α 1→0，填平斜线阶梯角落
        putAlpha(data, x, y, 2.0f - dist);
      }
    }
  }
}

void VkGeometryLayer::rasterRectFill(uint8_t* data, float x0, float y0,
                                     float x1, float y1) {
  int ix0 = (int)ceilf(fminf(x0, x1));
  int ix1 = (int)floorf(fmaxf(x0, x1));
  int iy0 = (int)ceilf(fminf(y0, y1));
  int iy1 = (int)floorf(fmaxf(y0, y1));
  for (int y = iy0; y <= iy1; ++y) {
    for (int x = ix0; x <= ix1; ++x) {
      putPixel(data, x, y);
    }
  }
}

void VkGeometryLayer::rasterize(const GeoSnapshot& snap) {
  uint8_t* data = cpuCanvas->getPointer();
  memset(data, 0, canvasWidth * canvasHeight);
  for (const auto& s : snap.shapes) {
    switch (s.type) {
      case GeoType::point: {
        float cx = s.a.x * canvasWidth;
        float cy = s.a.y * canvasHeight;
        // radius 以 1080p 帧像素为基准，/scale 转 canvas px（dpiScale 自然消掉）
        float r = s.radius / scale;
        if (r < 0.5f) {
          r = 0.5f;
        }
        rasterDisc(data, cx, cy, r);
        break;
      }
      case GeoType::line: {
        float x0 = s.a.x * canvasWidth;
        float y0 = s.a.y * canvasHeight;
        float x1 = s.b.x * canvasWidth;
        float y1 = s.b.y * canvasHeight;
        rasterLine(data, x0, y0, x1, y1);
        break;
      }
      case GeoType::rect: {
        float x0 = s.a.x * canvasWidth;
        float y0 = s.a.y * canvasHeight;
        float x1 = s.b.x * canvasWidth;
        float y1 = s.b.y * canvasHeight;
        if (s.fill) {
          rasterRectFill(data, x0, y0, x1, y1);
        } else {
          // 四条边
          rasterLine(data, x0, y0, x1, y0);
          rasterLine(data, x0, y1, x1, y1);
          rasterLine(data, x0, y0, x0, y1);
          rasterLine(data, x1, y0, x1, y1);
        }
        break;
      }
      case GeoType::circle: {
        float cx = s.a.x * canvasWidth;
        float cy = s.a.y * canvasHeight;
        float r = s.radius / scale;  // 1080p 帧px → canvas px
        if (r < 0.5f) {
          r = 0.5f;
        }
        if (s.fill) {
          rasterDisc(data, cx, cy, r);
        } else {
          rasterRing(data, cx, cy, r);
        }
        break;
      }
    }
  }
}

}
