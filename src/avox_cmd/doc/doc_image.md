# image — 图片处理

Phase 3 — 图片加载、保存、缩放、格式转换。

## 用法

```bash
# 图片信息
avox_cli image info -i photo.jpg

# 缩放
avox_cli image resize -i input.png -o output.png -w 800 -h 600

# 格式转换
avox_cli image convert -i input.bmp -o output.png

# Base64 编码
avox_cli image base64 -i photo.jpg
```

## 选项

| 选项 | 长选项 | 类型 | 必填 | 说明 | 默认值 |
|------|--------|------|------|------|--------|
| 子操作 | | | ✅ | info/resize/convert/base64 | - |
| `-i` | `--input` | String | ✅ | 输入图片 | - |
| `-o` | `--output` | String | | 输出图片 | - |
| `-w` | `--width` | Int | | 目标宽度 | 原始 |
| `-h` | `--height` | Int | | 目标高度 | 原始 |

## 对应 SDK API

- `createImageBuffer()`: 创建图片缓冲
- `loadImagePath()`: 加载图片
- `saveImagePath()`: 保存图片
- `resizeImage()`: 缩放图片
- `getImageBase64()`: Base64 编码
- `getImageSize()`: 获取图片尺寸

## 实现要点

1. `info`: 加载图片，输出尺寸、格式信息
2. `resize`: 加载 → `resizeImage()` → 保存
3. `convert`: 加载 → 保存 (自动根据输出后缀选格式)
4. `base64`: 加载 → `getImageBase64()` → 输出到 stdout
