#pragma once

#include "FontCache.hpp"
#include "avox/video/ImageBuffer.hpp"
#include "avox_vulkan/layer/VkLayer.hpp"
#include "avox_vulkan/vulkan/VkTexture.hpp"

namespace avox {

// 字体纹理混合参数
struct FontBlendParamet {
  vec3f fontColor = {1.0f, 1.0f, 1.0f};
  // 不透明度,0完全不透明，1完全透明
  float opacity = 0.f;
};

// 文本块：每个 index 对应一组独立的排版+文本
struct TextBlock {
  FontLayout layout = {};
  std::string text;
  std::vector<CachedGlyph> glyphs;
  std::vector<vec2i> uvs;               // canvas 坐标
};

class VkFontLayer : public VkLayer, public IFontLayer {
  AVOX_LAYER_GETNAME(VkFontLayer)
 public:
  VkFontLayer();
  virtual ~VkFontLayer();

 private:
  // 字体缩放比例
  float scale = 1.0f;
  // DPI缩放比例，根据帧高度与参考高度(1080)的比值计算
  float dpiScale = 1.0f;
  // 合并缩放 = scale * dpiScale
  float tscale = 1.0f;
  // 多位置文本块
  std::vector<TextBlock> blocks;
  // 当前操作的 block index
  int32_t currentIndex = 0;
  // 水平间隔
  int32_t hSpace = 3;
  // 垂直间隔
  int32_t vSpace = 5;
  // 需要更新的 block 标记
  bool bNeedUpdate = false;
  // CPU blit 资源
  std::unique_ptr<ImageBuffer> cpuCanvas;
  std::unique_ptr<VkWrapBuffer> cpuBuffer;
  // 内部 canvas image，作为 drawFontBlend.comp 的输入
  VulkanTexturePtr canvasImage;
  // canvas 尺寸（= 帧尺寸 / tscale）
  int32_t canvasWidth = 0;
  int32_t canvasHeight = 0;
  FontBlendParamet fParamet = {};

 public:
  // IFontLayer 接口实现
  virtual bool setFont(const char* fontName, int32_t fontSize) override;
  virtual FontLayout getLayout(int32_t index) override;
  virtual void updateLayout(int32_t index, const FontLayout& fontLayout) override;
  virtual void setColor(float r, float g, float b,
                        float opacity = 0) override;
  virtual void setScale(float scale) override;
  virtual void setTextLayout(int32_t index) override;
  virtual void drawText(const char* text) override;

 protected:
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
  virtual void onInitPipe() override;
  virtual void onPreFrame() override;
  virtual void onCommand() override;

 protected:
  virtual void onVisible(bool visible) override;

 private:
  void computeLayout(TextBlock& block);
  void drawTextInternal();
};

}
