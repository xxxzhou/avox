# 当前计划

本文档记录当前正在进行的开发计划。

## 2026.04 计划

### WebAssembly 完善

- [ ] WebGL 渲染支持
- [ ] 音频播放支持
- [ ] 性能优化

### 文档维护

- [x] README.md 重写
- [x] CLAUDE.md 更新
- [x] AI 模块文档
- [x] 构建文档更新

## 已归档计划

### 字体渲染接口 (已完成)

```cpp
class IFontLayer {
 public:
  virtual ~IFontLayer() {}

 public:
  // 设置字体信息 fontName 对应 asset/fonts/ 下面的字体文件名
  virtual bool loadFont(const char* fontName, const FontInfo& fontInfo) = 0;
  // 更新字体排版
  virtual void setFontLayout(const FontLayout& fontLayout) = 0;
  // opacity 不透明度，0 完全不透明，1 完全透明
  virtual void setFontColor(float r, float g, float b, float opacity = 0) = 0;
  // 绘制文本
  virtual void drawText(const char* text) = 0;
};

extern "C" {
// 通过 ISurfaceRender 获取 IFontLayer
// 只在 Vulkan 后端可用，其他平台返回 nullptr
AVOX_EXPORT IFontLayer* enableRenderFont(ISurfaceRender* render);
AVOX_EXPORT void disableRenderFont(ISurfaceRender* render);
}
```

此接口已实现，位于 `avox_freetype` 模块。
