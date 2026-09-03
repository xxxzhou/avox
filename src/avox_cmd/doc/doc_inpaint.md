# inpaint — AI 去水印

Phase 2 — AI 图像修复，检测和去除水印。

## 用法

```bash
# 检测水印
avox_cli inpaint -i image.png -detect

# 去水印
avox_cli inpaint -i image.png -o output.png

# 仅检测并保存 mask
avox_cli inpaint -i image.png -mask mask.png

# 指定区域去水印
avox_cli inpaint -i image.png -o output.png -region 100,100,200,50

# 使用指定 mask
avox_cli inpaint -i image.png -o output.png -mask custom_mask.png

# GPU 加速
avox_cli inpaint -i image.png -o output.png -gpu
```

## 选项

| 选项 | 长选项 | 类型 | 必填 | 说明 | 默认值 |
|------|--------|------|------|------|--------|
| `-i` | `--input` | String | ✅ | 输入图片 | - |
| `-o` | `--output` | String | | 输出图片 | - |
| `-detect` | | Bool | | 仅检测 (不修复) | 关闭 |
| `-mask` | | String | | 保存/加载 mask 文件 | - |
| `-region` | | String | | 指定水印区域 (x,y,w,h) | 自动检测 |
| `-model` | | String | | 模型路径 | 内置 |
| `-gpu` | | Bool | | GPU 加速 | 关闭 |

## 对应 SDK API

- `AvoxManager::watermarkRemovalHub.create("inpaint")` → `IWatermarkRemoval*`
- `IWatermarkRemoval::load()`: 初始化模型
- `IWatermarkRemoval::process()`: 检测+修复
- `IWatermarkRemoval::detect()`: 仅检测
- `IWatermarkRemoval::inpaint()`: 使用指定 mask 修复
- `createImageBuffer()`, `loadImagePath()`, `saveImagePath()`: 图片 I/O

## 实现要点

1. 加载模型 (`IWatermarkRemoval::load()`)
2. 根据参数决定流程: 仅检测 / 检测+保存mask / 完整修复
3. 如果指定 `-region`，手动构建 mask 后调用 `IWatermarkRemoval::inpaint()`
4. 如果指定 `-mask` (已存在的文件)，加载 mask 后调用 `IWatermarkRemoval::inpaint()`
5. 否则自动检测水印位置 (`IWatermarkRemoval::process()`)
6. 参考: `samples/functest/inpainttest.cpp`
