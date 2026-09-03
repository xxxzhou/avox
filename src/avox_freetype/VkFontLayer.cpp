#include "VkFontLayer.hpp"

#include "avox_vulkan/VkHelper.hpp"
#include "avox_vulkan/layer/VkPipeGraph.hpp"

namespace avox {

VkFontLayer::VkFontLayer() {
  inCount = 1;
  glslPath = "glsl/drawFontBlend.comp.spv";
  setUBOSize(sizeof(FontBlendParamet));
  fParamet.fontColor = {1.0f, 1.0f, 1.0f};
  fParamet.opacity = 0.f;
  updateUBO(&fParamet);
  bParametChange = true;
  // 默认有一个 text block (index 0)
  blocks.resize(1);
  blocks[0].layout.alignment.horizontal = HAlignType::mid;
  blocks[0].layout.alignment.vertical = VAlignType::bottom;
  blocks[0].layout.x = 0.5f;
  blocks[0].layout.y = 0.8f;
  blocks[0].layout.width = 0.8f;
  blocks[0].layout.height = 0.4f;
  cpuCanvas = std::make_unique<ImageBuffer>();
}

VkFontLayer::~VkFontLayer() {}

bool VkFontLayer::setFont(const char* fontName, int32_t fontSize) {
  bool bLoad = FontCache::instance().setFont(fontName, fontSize);
  if (bLoad) {
    resetGraph();
  }
  return bLoad;
}

FontLayout VkFontLayer::getLayout(int32_t index) {
  if (index >= (int32_t)blocks.size()) {
    blocks.resize(index + 1);
  }
  return blocks[index].layout;
}

void VkFontLayer::updateLayout(int32_t index, const FontLayout& fontLayout_) {
  if (fontLayout_.alignment.horizontal == HAlignType::none ||
      fontLayout_.alignment.vertical == VAlignType::none) {
    return;
  }
  if (index >= (int32_t)blocks.size()) {
    blocks.resize(index + 1);
  }
  blocks[index].layout = fontLayout_;
  resetGraph();
}

void VkFontLayer::setColor(float r, float g, float b, float opacity) {
  fParamet.fontColor = {r, g, b};
  fParamet.opacity = opacity;
  updateUBO(&fParamet);
  bParametChange = true;
}

void VkFontLayer::setScale(float scale_) {
  if (scale_ <= 0.0f) scale_ = 1.0f;
  scale = scale_;
  float newTscale = scale * dpiScale;
  if (newTscale != tscale) {
    tscale = newTscale;
    resetGraph();
  }
}

void VkFontLayer::setTextLayout(int32_t index) {
  if (index >= (int32_t)blocks.size()) {
    blocks.resize(index + 1);
  }
  currentIndex = index;
}

void VkFontLayer::drawText(const char* text) {
  if (!text) return;
  if (currentIndex >= (int32_t)blocks.size()) {
    blocks.resize(currentIndex + 1);
  }
  blocks[currentIndex].text = text;
  bNeedUpdate = true;
}

void VkFontLayer::onInitGraph() {
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

void VkFontLayer::onInitLayer() {
  VkLayer::onInitLayer();
  if (inFormats.size() > 0) {
    // DPI缩放：以1080p为参考
    constexpr int32_t kReferenceHeight = 1080;
    dpiScale = (float)inFormats[0].height / kReferenceHeight;
    tscale = scale * dpiScale;
    // canvas 大小 = 帧大小 / tscale
    canvasWidth = (int32_t)(inFormats[0].width / tscale);
    canvasHeight = (int32_t)(inFormats[0].height / tscale);
    if (canvasWidth <= 0) canvasWidth = 1;
    if (canvasHeight <= 0) canvasHeight = 1;
    // 创建 cpuCanvas
    ImageFormat canvasFormat = {};
    canvasFormat.width = canvasWidth;
    canvasFormat.height = canvasHeight;
    canvasFormat.imageType = ImageType::r8;
    cpuCanvas->setImageFormat(canvasFormat);
    // 创建 staging buffer
    int32_t canvasSize = canvasWidth * canvasHeight;
    cpuBuffer = std::make_unique<VkWrapBuffer>();
    cpuBuffer->setVkContext(vkPipeGraph);
    cpuBuffer->initResoure(BufferUsage::store, canvasSize,
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  }
}

void VkFontLayer::onInitPipe() {
  // 创建内部 canvas image（需要 SAMPLED_BIT 用于 sampler 采样）
  canvasImage = VulkanTexturePtr(new VkTexture());
  canvasImage->setVkContext(vkPipeGraph);
  VkFormat canvasFormat = getVkFormat(ImageType::r8);
  canvasImage->InitResource(
      canvasWidth, canvasHeight, canvasFormat,
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  canvasImage->createSampler(true);  // 线性采样
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

void VkFontLayer::onPreFrame() {
  VkLayer::onPreFrame();
  drawTextInternal();
  if (bNeedUpdate) {
    bNeedUpdate = false;
    if (!cpuCanvas) {
      return;
    }
    uint8_t* canvasData = cpuCanvas->getPointer();
    int32_t cW = canvasWidth;
    int32_t cH = canvasHeight;
    memset(canvasData, 0, cW * cH);
    // 遍历所有 text block，blit 到同一个 canvas
    for (auto& block : blocks) {
      if (block.text.empty()) {
        continue;
      }
      for (size_t i = 0; i < block.glyphs.size(); ++i) {
        auto& glyph = block.glyphs[i];
        auto& pos = block.uvs[i];
        const uint8_t* src = glyph.bitmap.data();
        if (glyph.width <= 0 || glyph.height <= 0) {
          continue;
        }
        for (int32_t y = 0; y < glyph.height; ++y) {
          int32_t cy = pos.y + y;
          if (cy < 0 || cy >= cH) continue;
          for (int32_t x = 0; x < glyph.width; ++x) {
            int32_t cx = pos.x + x;
            if (cx < 0 || cx >= cW) continue;
            uint8_t alpha = src[y * glyph.width + x];
            if (alpha > 0) {
              int32_t idx = cy * cW + cx;
              if (alpha > canvasData[idx]) canvasData[idx] = alpha;
            }
          }
        }
      }
    }
    cpuBuffer->upload(canvasData, cW * cH);
  }
}

void VkFontLayer::onCommand() {
  if (!cpuBuffer || outTexs.empty() || !canvasImage) {
    return;
  }
  VkCommandBuffer cmd = getCurrentCmdBuffer();
  // 1. bufferToImage: cpuBuffer → canvasImage
  canvasImage->addBarrier(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_ACCESS_TRANSFER_WRITE_BIT);
  bufferToImage(cmd, cpuBuffer.get(), canvasImage.get());
  // 2. drawFontBlend.comp: inTexs[0] + canvasImage → outTexs[0]
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

void VkFontLayer::onVisible(bool visible) {
  // 不再需要委托给 preFontLayer
}

void VkFontLayer::computeLayout(TextBlock& block) {
  block.uvs.resize(block.glyphs.size());
  if (block.glyphs.empty()) {
    return;
  }
  // 直接在 canvas 坐标系下排版
  int32_t canvasLayoutW = (int32_t)(canvasWidth * block.layout.width);
  // 1. 计算文本行信息
  std::vector<TextLine> lines;
  TextLine currentLine = {};
  int32_t currentWidth = 0;
  int32_t maxLineHeight = 0;
  size_t currentGlyphIndex = 0;
  for (size_t i = 0; i < block.glyphs.size(); ++i) {
    const auto& glyph = block.glyphs[i];
    if (currentWidth + glyph.width + (currentWidth > 0 ? hSpace : 0) >
            canvasLayoutW &&
        currentWidth > 0) {
      currentLine.width = currentWidth;
      currentLine.height = maxLineHeight;
      currentLine.glyphCount = currentGlyphIndex - currentLine.startGlyphIndex;
      lines.push_back(currentLine);
      currentLine.startGlyphIndex = currentGlyphIndex;
      currentLine.glyphCount = 0;
      currentWidth = 0;
      maxLineHeight = 0;
    }
    if (currentLine.glyphCount == 0) {
      currentLine.startGlyphIndex = i;
    }
    currentGlyphIndex = i + 1;
    currentLine.glyphCount = currentGlyphIndex - currentLine.startGlyphIndex;
    currentWidth += glyph.width + (currentWidth > 0 ? hSpace : 0);
    maxLineHeight = std::max(maxLineHeight, glyph.height);
  }
  if (currentLine.glyphCount > 0) {
    currentLine.width = currentWidth;
    currentLine.height = maxLineHeight;
    lines.push_back(currentLine);
  }
  // 2. 计算文本总高度和最大行宽
  int32_t totalHeight = 0;
  int32_t maxLineWidth = 0;
  for (auto& line : lines) {
    totalHeight += line.height;
    maxLineWidth = std::max(maxLineWidth, line.width);
  }
  totalHeight += lines.size() * vSpace;
  // 3. fontLayout.x/y 是锚点，alignment 决定文本相对锚点的位置
  int32_t anchorX = (int32_t)(canvasWidth * block.layout.x);
  int32_t anchorY = (int32_t)(canvasHeight * block.layout.y);
  int32_t startX = anchorX;
  switch (block.layout.alignment.horizontal) {
    case HAlignType::mid:
      startX = anchorX - maxLineWidth / 2;
      break;
    case HAlignType::right:
      startX = anchorX - maxLineWidth;
      break;
    case HAlignType::left:
    default:
      break;
  }
  int32_t startY = anchorY;
  switch (block.layout.alignment.vertical) {
    case VAlignType::top:
      break;
    case VAlignType::mid:
      startY = anchorY - totalHeight / 2;
      break;
    case VAlignType::bottom:
      startY = anchorY - totalHeight;
      break;
  }
  startX = std::max(0, std::min(startX, canvasWidth - 1));
  startY = std::max(0, std::min(startY, canvasHeight - 1));
  // 4. 计算每个字符在 canvas 上的位置
  int32_t gindex = 0;
  int32_t currentY = startY;
  for (auto& line : lines) {
    int32_t lineStartX = startX;
    switch (block.layout.alignment.horizontal) {
      case HAlignType::mid:
        lineStartX = startX + (maxLineWidth - line.width) / 2;
        break;
      case HAlignType::right:
        lineStartX = startX + maxLineWidth - line.width;
        break;
      case HAlignType::left:
      default:
        lineStartX = startX;
        break;
    }
    int32_t currentX = lineStartX;
    size_t lineEndIndex = line.startGlyphIndex + line.glyphCount;
    for (size_t i = line.startGlyphIndex; i < lineEndIndex; ++i) {
      const auto& glyph = block.glyphs[i];
      int32_t baseY = line.height - glyph.baseline;
      int32_t uvX = std::max(0, std::min(currentX, canvasWidth - 1));
      int32_t uvY = std::max(0, std::min(currentY + baseY, canvasHeight - 1));
      block.uvs[gindex] = vec2i(uvX, uvY);
      gindex++;
      currentX += glyph.width + hSpace;
    }
    if (currentY >= startY) {
      currentY += vSpace;
    }
    currentY = currentY + line.height;
  }
}

void VkFontLayer::drawTextInternal() {
  auto& fontCache = FontCache::instance();
  if (!fontCache.hasCurrent()) {
    return;
  }
  // 遍历所有 text block，加载字形并计算排版
  for (auto& block : blocks) {
    if (block.text.empty()) {
      block.glyphs.clear();
      block.uvs.clear();
      continue;
    }
    fontCache.loadText(block.text, block.glyphs);
    computeLayout(block);
  }
  updateUBO(&fParamet);
  bParametChange = true;
}

}
