#pragma once

#include <memory>

#include "FontCache.hpp"
#include "FreetypeExport.h"
#include "VkFontLayer.hpp"

namespace avox {

// 字体渲染器 - 管理 VkFontLayer 的创建和配置
// 继承 IFontLayer，提供统一的字体操作接口
class FontRender : public IFontLayer {
 public:
  FontRender();
  ~FontRender() override = default;

 private:
  VkFontLayer* fontLayer = nullptr;
  // 缓存的配置（在 fontLayer 未创建时保存）
  std::vector<FontLayout> pendingLayouts;
  vec4f pendingTextColor = {1.0f, 1.0f, 1.0f, 0.0f};
  std::string pendingText = "";
  float pendingScale = 1.0f;
  int32_t pendingIndex = 0;

  bool bEnableText = false;
  std::mutex mtx;

 public:
  void setFontLayer(VkFontLayer* layer);

  // IFontLayer 接口实现
  bool setFont(const char* fontName, int32_t fontSize) override;
  FontLayout getLayout(int32_t index) override;
  void updateLayout(int32_t index, const FontLayout& fontLayout) override;
  void setColor(float r, float g, float b, float opacity = 0) override;
  void setScale(float scale) override;
  void setTextLayout(int32_t index) override;
  void drawText(const char* text) override;

  void setEnable(bool bEnable) { bEnableText = bEnable; };
  bool enabled() const { return bEnableText; }

 private:
  void applyPendingSettings();
};

}
