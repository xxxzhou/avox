# Vulkan 字体渲染（avox_freetype）

## 整体思路

在视频帧上叠加文字，核心挑战是"既要清晰又要廉价"。方案是 **CPU 在小 canvas 上光栅化字形，GPU 一次 dispatch 混合到帧**——避免了逐字形 GPU draw call，也不需要维护 texture atlas。

canvas 取帧的 1/tscale 大小（默认 1/4），字形按原始 fontSize 渲染后 1:1 blit 到 canvas，GPU 采样时双线性插值自动放大到帧分辨率。canvas 越小越省内存和带宽，但字符越糊；scale 可调。

## 管线

```
FontCache(单例) → FontMap(CachedGlyph 按需缓存)
    → VkFontLayer[onPreFrame: CPU blit字形→canvas R8 → upload staging]
    → VkFontLayer[onCommand: GPU drawFontBlend.comp: sampler2D 双线性采样 + mix 混合]
    → 输出 RGBA
```

单次 CS dispatch 完成所有文本混合，无逐字形 draw call。canvas sampler **必须 LINEAR**（VkTexture::createSampler 的 bLinear 参数需真正生效，否则放大无插值，字符边缘锯齿严重）。

## 缩放与 DPI 适配

Canvas = 帧大小 / tscale，其中 `tscale = scale × dpiScale`，`dpiScale = 帧高 / 1080`。
dpiScale 的意义：**把任意分辨率统一到 1080p 基准**。720p 时 dpiScale=0.667，canvas 更小但字符相对帧的比例不变；4K 时 dpiScale=2，canvas 更大字符更精细。
用户调 `setScale` 影响 tscale → canvas 大小变化 → 触发 `resetGraph` 重建。

## GPU 混合（drawFontBlend.comp）

思路：canvas 上每个像素存字形 alpha（0~255），GPU 归一化 UV 采样时双线性插值自动产生边缘渐变，shader 用 `mix(帧色, 字色, α × (1-opacity))` 混合。

- α > 0 才混合（跳过无文字区域）
- opacity 控制整体透明度：0=完全不透明（文字全覆盖），1=全透明（文字不可见）
- 全局单 UBO（fontColor + opacity），所有文本块共享

## 多位置文本（TextBlock）

每个 index 对应一个 TextBlock：独立 FontLayout（锚点+对齐+宽高限制）+ 文本内容 + 字形缓存引用 + canvas 坐标。
所有 block 共享同一 canvas 和全局 color/scale。onPreFrame 遍历所有 block 一起 blit，一次 upload + dispatch。
`bNeedUpdate` 是 VkFontLayer 级别标记，任一 block 文本变化即置位，下次 onPreFrame 重画。

## 字形缓存：FontCache → FontMap → CachedGlyph

三层缓存，按需加载：

- **FontCache**（全局单例）：按 "字体名_字号" 缓存 FontMap，用过的永不释放。setFont 先查缓存，命中切、未命中建+缓存+切。构造时默认加载 simhei.ttf/32。loadText 加锁调用，线程安全。
- **FontMap**：按 wchar_t 缓存 CachedGlyph。不预加载任何字符，首现字符实时 FT_Load_Char 渲染并缓存。字幕场景缓存 <100KB（vs 旧 atlas 26MB 预加载方案）。
- **CachedGlyph**：单个字符的 width/height/baseline + R8 bitmap（alpha 行优先）。

## FontRender：IFontLayer 适配层

FontRender 继承 IFontLayer，由 VkVideoRender 持有。VkFontLayer 在 graph 重建时才创建，创建前用户调的 setFont/setColor/drawText 等暂存到 pending* 成员，setFontLayer 时 applyPendingSettings 一次性下发。

enableRenderFont / disableRenderFont 控制 VkFontLayer 是否接入 graph 执行链。对象常驻（由 render 持有），disable 不释放，enable 时 graph 重建自动恢复。

## IFontLayer 接口

- `setFont(name, size)` — 切字体，委托 FontCache 全局缓存
- `setColor(r, g, b, opacity)` — 全局颜色/透明度（0=不透明，1=全透明）
- `setScale(scale)` — 全局缩放，影响 canvas 分辨率
- `getLayout / updateLayout(index, layout)` — 多位置排版，index 超范围自动 resize
- `setTextLayout(index)` — 选当前 index
- `drawText(text)` — 在当前 index 位置绘制

FontLayout = { Alignment, x, y, width, height }，x/y/width/height 均归一化 [0,1]。
C 导出：`enableRenderFont(ISurfaceRender*)` / `disableRenderFont(ISurfaceRender*)`。
