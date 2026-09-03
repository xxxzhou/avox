#pragma once

// ============================================================================
// vision-toolkit: 本地图像处理。
//
// 对齐 dsh-vision-toolkit 本地脚本 (pixel_diff.py / crop) 的确定性算法, 底层复用 avox
// 的 AvoxImage.h (stb_image 解码 + 手写双线性缩放)。全 C++, 不引 Python / OpenCV。
//
// 统一语义: 任何图像先 decode 成 3 通道 RGB (带 alpha 的合成到白底, 透明 UI 才不会被
// 当成幽灵差异), 之后裁剪/缩放/取主色/像素diff 全在这份 RGB 上做, 坐标即分析后图像坐标。
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

#include "avox/AvoxDef.h"  // namespace avox { / }

namespace avox {

// 像素框。工具 schema 里的 region 用 "x1,y1,x2,y2" 文本承载, 本处结构化为整数。
struct VisionBox {
  int x1 = 0;
  int y1 = 0;
  int x2 = 0;
  int y2 = 0;
};

// 解析 "x1,y1,x2,y2" 文本 → 框; 非法返回 false。
bool parseVisionBox(const std::string& text, VisionBox& box);

// 解码到 3 通道 RGB (alpha 已合成到白底)。返回 RGB 字节数组 (w*h*3)。
struct DecodedImage {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgb;
  bool ok = false;
  std::string error;
};
DecodedImage decodeImageToRgb(const std::string& path);

// 对 RGB 图做双线性缩放。dstW/dstH >0。
void resizeRgbBilinear(const std::vector<uint8_t>& src, int srcW, int srcH,
                       std::vector<uint8_t>& dst, int dstW, int dstH);

// 把 3 通道 RGB 存成 PNG (内部构造 IImageBuffer 走 saveImagePath)。
bool savePngFromRgb(const std::string& path, int width, int height,
                    const std::vector<uint8_t>& rgb, std::string& error);

// 把 3 通道 RGB 写成文件, 按扩展名选格式 (.jpg/.png/.bmp/.tga, 默认 png)。
// 视觉请求自动降采样重编码走 JPEG (节省体积/积分)。
bool saveRgbImageFile(const std::string& path, int width, int height,
                      const std::vector<uint8_t>& rgb, std::string& error);

// 把 4 通道 RGBA 存成带透明度的 PNG (extract-foreground 用)。
bool saveRgbaPng(const std::string& path, int width, int height,
                 const std::vector<uint8_t>& rgba, std::string& error);

// 裁剪 srcPath 的 [x1,y1,x2,y2) 区域 (越界自动收敛到图内), 可选 scale 放大(>=1),
// 存到 outputPath (按扩展名 .png/.jpg)。返回产物尺寸。
struct CropResult {
  bool ok = false;
  std::string error;
  int width = 0;
  int height = 0;
};
CropResult cropImageFile(const std::string& srcPath, const VisionBox& requested,
                         int scale, const std::string& outputPath);

// 单区块差异分 (0-100) 与框。
struct DiffRegion {
  int index = 0;
  double differencePct = 0.0;
  VisionBox box;
};

// pixel_diff 核心: 两图逐像素对比, 最差区块按格子均值排序。
// heatmap == 热力图 RGB (w*h*3, 低蓝高红), 供调用方落盘。width/height 为参考图(original)
// 分析坐标; rebuilt 尺寸不同时缩放到 original 并记 scaled。
struct PixelDiffResult {
  bool ok = false;
  std::string error;
  int width = 0;      // 参考图分析宽
  int height = 0;     // 参考图分析高
  int rebuiltWidth = 0;
  int rebuiltHeight = 0;
  bool scaled = false;
  double overallDifferencePct = 0.0;
  std::vector<DiffRegion> worst;      // 降序, 最多 top 个
  std::vector<uint8_t> heatmap;       // w*h*3 热力图
};
PixelDiffResult computePixelDiff(const std::string& originalPath,
                                 const std::string& rebuiltPath, int grid, int top);

}