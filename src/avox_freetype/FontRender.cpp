#include "FontRender.hpp"

#ifdef AVOX_ENABLE_FREETYPE
#include "avox_vulkan/vulkan/VkVideoRender.hpp"
#endif

namespace avox {

FontRender::FontRender() {
  pendingLayouts.resize(1);
  pendingLayouts[0].alignment.horizontal = HAlignType::mid;
  pendingLayouts[0].alignment.vertical = VAlignType::bottom;
  pendingLayouts[0].x = 0.5f;
  pendingLayouts[0].y = 0.8f;
  pendingLayouts[0].width = 0.8f;
  pendingLayouts[0].height = 0.4f;
}

void FontRender::setFontLayer(VkFontLayer* layer) {
  std::lock_guard<std::mutex> lock(mtx);
  fontLayer = layer;
  applyPendingSettings();
}

bool FontRender::setFont(const char* fontName, int32_t fontSize) {
  std::lock_guard<std::mutex> lock(mtx);
  return FontCache::instance().setFont(fontName, fontSize);
}

FontLayout FontRender::getLayout(int32_t index) {
  std::lock_guard<std::mutex> lock(mtx);
  if (!fontLayer) {
    if (index >= (int32_t)pendingLayouts.size()) {
      pendingLayouts.resize(index + 1);
    }
    return pendingLayouts[index];
  }
  return fontLayer->getLayout(index);
}

void FontRender::updateLayout(int32_t index, const FontLayout& fontLayout_) {
  std::lock_guard<std::mutex> lock(mtx);
  if (index >= (int32_t)pendingLayouts.size()) {
    pendingLayouts.resize(index + 1);
  }
  pendingLayouts[index] = fontLayout_;
  if (!fontLayer) {
    return;
  }
  fontLayer->updateLayout(index, fontLayout_);
}

void FontRender::setColor(float r, float g, float b, float opacity) {
  std::lock_guard<std::mutex> lock(mtx);
  pendingTextColor = {r, g, b, opacity};
  if (!fontLayer) {
    return;
  }
  fontLayer->setColor(r, g, b, opacity);
}

void FontRender::setScale(float scale) {
  std::lock_guard<std::mutex> lock(mtx);
  pendingScale = scale;
  if (!fontLayer) {
    return;
  }
  fontLayer->setScale(scale);
}

void FontRender::setTextLayout(int32_t index) {
  std::lock_guard<std::mutex> lock(mtx);
  pendingIndex = index;
  if (!fontLayer) {
    return;
  }
  fontLayer->setTextLayout(index);
}

void FontRender::drawText(const char* text) {
  std::lock_guard<std::mutex> lock(mtx);
  if (!text) return;
  pendingText = text;
  if (!fontLayer) {
    return;
  }
  fontLayer->drawText(text);
}

void FontRender::applyPendingSettings() {
  if (!fontLayer) {
    return;
  }
  // 应用所有 pending layouts
  for (size_t i = 0; i < pendingLayouts.size(); ++i) {
    fontLayer->updateLayout((int32_t)i, pendingLayouts[i]);
  }
  // 应用颜色
  fontLayer->setColor(pendingTextColor.x, pendingTextColor.y,
                      pendingTextColor.z, pendingTextColor.w);
  // 应用缩放
  fontLayer->setScale(pendingScale);
  // 应用当前 index
  fontLayer->setTextLayout(pendingIndex);
  // 应用文本
  if (!pendingText.empty()) {
    fontLayer->drawText(pendingText.c_str());
  }
}

}
