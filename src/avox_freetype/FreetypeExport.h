#pragma once

#include "avox/AvoxLayer.h"

namespace avox {

// 显示字体画框定位
struct FontLayout {
  // 定位方式
  Alignment alignment = {};
  // 根据定位方式对应位置，范围 0-1
  float x = 0;
  float y = 0;
  // 最大宽度比例，超过后自动换行，范围 0-1
  float width = 0;
  // 最大高度比例
  float height = 0;
};

class IFontLayer {
 public:
  virtual ~IFontLayer() {}

 public:
  // 切换字体,默认会加载simhei.ttf/32
  virtual bool setFont(const char* fontName, int32_t fontSize) = 0;
  // opacity 不透明度,0完全不透明，1完全透明，颜色范围0-1（所有文本块共享）
  virtual void setColor(float r, float g, float b, float opacity = 0) = 0;
  // 设置字体缩放比例（全局）
  virtual void setScale(float scale) = 0;
  // 得到index的字体排版，index超范围自动resize
  virtual FontLayout getLayout(int32_t index) = 0;
  // 更新index的字体排版
  virtual void updateLayout(int32_t index, const FontLayout& fontLayout) = 0;
  // 选中当前排版index，后续drawText使用该index的FontLayout
  virtual void setTextLayout(int32_t index) = 0;
  // 在当前选中的index位置绘制文本
  virtual void drawText(const char* text) = 0;
};

extern "C" {
// 得到并开启字体渲染管线
AVOX_EXPORT IFontLayer* enableRenderFont(ISurfaceRender* render);
// 关闭字体渲染管线
AVOX_EXPORT void disableRenderFont(ISurfaceRender* render);
}

}
