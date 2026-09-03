# Vulkan 几何叠加层（线/矩形/点/圆）

复用 `VkFontLayer` 的 "CPU 光栅化到小 canvas(R8) → upload → GPU sampler 双线性混合" 管线，
在视频帧上叠加矢量图元。详细设计见 [doc/plan/几何层设计.md](../../../doc/plan/几何层设计.md)。

## 管线

```
GeometryRender(图元数据,mutex) --takeSnapshot--> VkGeometryLayer
  CPU: 距离场光栅化 → canvas R8（=帧/tscale） → VkWrapBuffer upload
  GPU: drawShapeBlend.comp: imageLoad(帧) + textureLod(canvas) → 阈值硬切 → outTex RGBA
```

- `VkGeometryLayer` 继承 `VkLayer`（非 Group），inCount=1，单 CS dispatch
- 与 `VkFontLayer` 独立 canvas + 独立 dispatch，可共存于同一 PipeGraph

## 宽度控制：阈值

canvas 上画 1px 图元 → 双线性放大 S 倍（S=帧/canvas=tscale）→ 边缘成 0~1 渐变带。
shader 用 `step(threshold, α)` 硬切：**α≥阈值全合并（不透明），α<阈值丢弃**。

| 阈值 τ | 含义 |
|--------|------|
| 低（0.1）| 通过像素多 → 线宽（最宽）|
| 高（0.7）| 只过核心 → 线细（最细）|
| 0.5 | 适中 |

- `setScale(scale)` 改 S（canvas 分辨率），档位数 ≈ S，默认 4 → 1080p 下 S≈4
- `drawFontBlend` 不能复用（线性半透明 vs 阈值硬切），故新建 `drawShapeBlend.comp`

## 图元光栅化（距离场，CPU 在 canvas 上）

`alpha = clamp(0.5 - sdf, 0, 1)`，max 混合写入 R8：

| 图元 | SDF | 备注 |
|------|-----|------|
| 点（disc）| `dist(center) - r` | radius 帧px→canvas px: `/tscale` |
| 线段（capsule）| `distToSegment - 0.5` | 端点自带圆头 |
| 圆环 | `\|dist(center)-r\| - 0.5` | outline 宽度由阈值控 |
| 矩形 outline | 4 条 capsule | 转角自带圆角 |
| 矩形/圆 fill | 内部 α≈1，阈值只影响外轮廓 | — |

## API

```cpp
// 声明在 avox_vulkan/VkExport.h
extern "C" {
IGeometryLayer* enableRenderGeometry(ISurfaceRender* render);  // 开启
void disableRenderGeometry(ISurfaceRender* render);            // 关闭（移出执行链，不释放对象）
}

class IGeometryLayer {
  void setColor(float r, float g, float b);           // 全局色 0-1
  void setScale(float scale);                         // canvas=帧/(scale*dpiScale)
  void setThreshold(float tau);                       // 宽度旋钮 [0,1]: 低=宽 高=细
  void clear();                                       // 每帧重画前清空
  void drawPoint(float x, float y, float radiusPx);           // 归一化坐标
  void drawLine(float x0, float y0, float x1, float y1);
  void drawRect(float x0, float y0, float x1, float y1, bool fill = false);
  void drawCircle(float cx, float cy, float radiusPx, bool fill = false);
};
```

坐标用帧归一化 [0,1]；半径用帧像素（内部 `/tscale` 转 canvas 像素）。
线宽由全局 `threshold` 控制，同一 dispatch 内所有线宽一致；多粗细需按阈值分组多次 dispatch。

## 用法

```cpp
auto* geo = enableRenderGeometry(surfaceRender);
geo->setColor(0.f, 1.f, 0.f);   // 绿色
geo->setThreshold(0.5f);        // 适中线宽

// 每帧重画（动态图元，如检测框）
geo->clear();
geo->drawRect(0.1f, 0.1f, 0.4f, 0.4f);          // ROI 框
geo->drawLine(0.f, 0.5f, 1.f, 0.5f);            // 中线
geo->drawPoint(0.5f, 0.5f, 6.f);                // 中心点（半径6px）
geo->drawCircle(0.8f, 0.2f, 20.f);              // 圆环
```

注意：`enableRenderGeometry` 返回的对象由 render 持有，调用方只借用，不要释放。
每帧的 `clear()+draw*()` 需在本帧渲染前完成；下一帧 graph 初始化后 layer 才接入管线。

## 文件

| 文件 | 职责 |
|------|------|
| `../VkExport.h` | `IGeometryLayer` 接口 + `enableRenderGeometry` C 导出 |
| `GeometryRender.hpp/.cpp` | 图元数据 owner（mutex）+ `takeSnapshot` |
| `GeoShape.hpp` | 图元结构 + 快照 |
| `VkGeometryLayer.hpp/.cpp` | Vulkan 层：canvas + 距离场光栅化 + pipeline |
| `glsl/source/drawShapeBlend.comp` | 阈值硬切混合 shader |
