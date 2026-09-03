#include "VisionImage.hpp"

#include <algorithm>
#include <cmath>  // std::round
#include <cstring>
#include <memory>

#include "avox/AvoxImage.h"  // createImageBuffer / loadImagePath / resizeImage / saveImagePath
#include "avox/AvoxVideo.h"  // IImageBuffer / ImageFormat / ImageType

namespace avox {

// ---------------- 框解析 ----------------

bool parseVisionBox(const std::string& text, VisionBox& box) {
  std::vector<int> values;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t comma = text.find(',', start);
    const std::string token =
        comma == std::string::npos ? text.substr(start) : text.substr(start, comma - start);
    if (!token.empty()) {
      try {
        values.push_back(std::stoi(token));
      } catch (...) {
        return false;
      }
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  if (values.size() != 4) return false;
  box.x1 = values[0];
  box.y1 = values[1];
  box.x2 = values[2];
  box.y2 = values[3];
  return true;
}

// ---------------- 解码 ----------------

int channelsForImageType(ImageType type) {
  switch (type) {
    case ImageType::r8:       return 1;
    case ImageType::rgb8:
    case ImageType::bgr8:     return 3;
    case ImageType::rgba8:
    case ImageType::bgra8:
    case ImageType::argb8:    return 4;
    default:                  return 0;
  }
}

DecodedImage decodeImageToRgb(const std::string& path) {
  DecodedImage out;
  std::unique_ptr<IImageBuffer> buf(createImageBuffer());
  if (!buf || !loadImagePath(path.c_str(), buf.get())) {
    out.error = "无法解码图像: " + path + " (仅支持 PNG/JPEG/BMP/TGA 等 stb 格式)";
    return out;
  }
  const ImageFormat format = buf->getImageFormat();
  if (!format.bVailid()) {
    out.error = "图像格式信息缺失: " + path;
    return out;
  }
  const int w = format.width;
  const int h = format.height;
  int channels = channelsForImageType(format.imageType);
  if (channels == 0) {
    // 兜底: 用字节数反推通道数 (紧密打包)。
    const int total = buf->getBufferSize();
    channels = total / (w * h);
  }
  if (channels != 1 && channels != 3 && channels != 4) {
    out.error = "不支持的图像通道数: " + std::to_string(channels);
    return out;
  }
  const uint8_t* pixels = buf->getPointer();
  if (pixels == nullptr) {
    out.error = "图像像素为空: " + path;
    return out;
  }
  out.width = w;
  out.height = h;
  out.rgb.resize(static_cast<size_t>(w) * h * 3);
  const size_t count = static_cast<size_t>(w) * h;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t* p = pixels + i * channels;
    if (channels == 1) {
      out.rgb[i * 3 + 0] = p[0];
      out.rgb[i * 3 + 1] = p[0];
      out.rgb[i * 3 + 2] = p[0];
    } else if (channels == 3) {
      out.rgb[i * 3 + 0] = p[0];
      out.rgb[i * 3 + 1] = p[1];
      out.rgb[i * 3 + 2] = p[2];
    } else {
      // RGBA (stb 返回顺序): alpha 合成到白底。
      const int a = p[3];
      for (int c = 0; c < 3; ++c) {
        out.rgb[i * 3 + c] = static_cast<uint8_t>(
            (p[c] * a + (255 - a) * 255) / 255);
      }
    }
  }
  out.ok = true;
  return out;
}

// ---------------- 缩放 ----------------

void resizeRgbBilinear(const std::vector<uint8_t>& src, int srcW, int srcH,
                       std::vector<uint8_t>& dst, int dstW, int dstH) {
  dst.assign(static_cast<size_t>(dstW) * dstH * 3, 0);
  if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
  if (dstW == srcW && dstH == srcH) {
    dst = src;
    return;
  }
  const float xRatio = static_cast<float>(srcW) / dstW;
  const float yRatio = static_cast<float>(srcH) / dstH;
  for (int y = 0; y < dstH; ++y) {
    const float srcY = (y + 0.5f) * yRatio - 0.5f;
    const int y0 = std::clamp(static_cast<int>(srcY), 0, srcH - 1);
    const int y1 = std::clamp(y0 + 1, 0, srcH - 1);
    const float fy = std::clamp(srcY - static_cast<float>(y0), 0.0f, 1.0f);
    for (int x = 0; x < dstW; ++x) {
      const float srcX = (x + 0.5f) * xRatio - 0.5f;
      const int x0 = std::clamp(static_cast<int>(srcX), 0, srcW - 1);
      const int x1 = std::clamp(x0 + 1, 0, srcW - 1);
      const float fx = std::clamp(srcX - static_cast<float>(x0), 0.0f, 1.0f);
      const size_t dOff = (static_cast<size_t>(y) * dstW + x) * 3;
      const float w00 = (1.0f - fy) * (1.0f - fx);
      const float w01 = (1.0f - fy) * fx;
      const float w10 = fy * (1.0f - fx);
      const float w11 = fy * fx;
      for (int c = 0; c < 3; ++c) {
        const uint8_t* p00 = &src[(static_cast<size_t>(y0) * srcW + x0) * 3 + c];
        const uint8_t* p01 = &src[(static_cast<size_t>(y0) * srcW + x1) * 3 + c];
        const uint8_t* p10 = &src[(static_cast<size_t>(y1) * srcW + x0) * 3 + c];
        const uint8_t* p11 = &src[(static_cast<size_t>(y1) * srcW + x1) * 3 + c];
        const float v = w00 * p00[0] + w01 * p01[0] + w10 * p10[0] + w11 * p11[0];
        dst[dOff + c] = static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
      }
    }
  }
}

// ---------------- 落盘 ----------------

namespace {

// 把 3 通道 RGB 写成文件; 按扩展名选 .png/.jpg (默认 png)。复用 IImageBuffer + saveImagePath。
bool writeRgbFile(const std::string& path, int width, int height,
                  const std::vector<uint8_t>& rgb, std::string& error) {
  if (rgb.size() != static_cast<size_t>(width) * height * 3) {
    error = "RGB 数据长度与尺寸不符";
    return false;
  }
  std::unique_ptr<IImageBuffer> buf(createImageBuffer());
  if (!buf) {
    error = "无法创建图像缓冲";
    return false;
  }
  ImageFormat format;
  format.width = width;
  format.height = height;
  format.imageType = ImageType::rgb8;
  buf->setImageFormat(format);
  uint8_t* data = buf->getPointer();
  if (data == nullptr) {
    error = "图像缓冲分配失败";
    return false;
  }
  std::memcpy(data, rgb.data(), rgb.size());
  if (!saveImagePath(path.c_str(), buf.get())) {
    error = "写入图像失败: " + path;
    return false;
  }
  return true;
}

// 把 4 通道 RGBA 写成 PNG (带透明度)。
bool writeRgbaFile(const std::string& path, int width, int height,
                   const std::vector<uint8_t>& rgba, std::string& error) {
  if (rgba.size() != static_cast<size_t>(width) * height * 4) {
    error = "RGBA 数据长度与尺寸不符";
    return false;
  }
  std::unique_ptr<IImageBuffer> buf(createImageBuffer());
  if (!buf) {
    error = "无法创建图像缓冲";
    return false;
  }
  ImageFormat format;
  format.width = width;
  format.height = height;
  format.imageType = ImageType::rgba8;
  buf->setImageFormat(format);
  uint8_t* data = buf->getPointer();
  if (data == nullptr) {
    error = "图像缓冲分配失败";
    return false;
  }
  std::memcpy(data, rgba.data(), rgba.size());
  if (!saveImagePath(path.c_str(), buf.get())) {
    error = "写入图像失败: " + path;
    return false;
  }
  return true;
}

std::string lowerExtension(const std::string& path) {
  const size_t dot = path.rfind('.');
  if (dot == std::string::npos) return "png";
  std::string ext = path.substr(dot + 1);
  for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext;
}

}  // namespace

bool savePngFromRgb(const std::string& path, int width, int height,
                    const std::vector<uint8_t>& rgb, std::string& error) {
  return writeRgbFile(path, width, height, rgb, error);
}

bool saveRgbImageFile(const std::string& path, int width, int height,
                      const std::vector<uint8_t>& rgb, std::string& error) {
  return writeRgbFile(path, width, height, rgb, error);
}

bool saveRgbaPng(const std::string& path, int width, int height,
                 const std::vector<uint8_t>& rgba, std::string& error) {
  return writeRgbaFile(path, width, height, rgba, error);
}

CropResult cropImageFile(const std::string& srcPath, const VisionBox& requested,
                         int scale, const std::string& outputPath) {
  CropResult out;
  DecodedImage img = decodeImageToRgb(srcPath);
  if (!img.ok) {
    out.error = img.error;
    return out;
  }
  // 越界自动收敛到图内 (对齐 toolkit crop 的 clamp)。
  VisionBox box = requested;
  box.x1 = std::clamp(box.x1, 0, img.width);
  box.y1 = std::clamp(box.y1, 0, img.height);
  box.x2 = std::clamp(box.x2, 0, img.width);
  box.y2 = std::clamp(box.y2, 0, img.height);
  if (box.x1 > box.x2) std::swap(box.x1, box.x2);
  if (box.y1 > box.y2) std::swap(box.y1, box.y2);
  if (box.x2 <= box.x1 || box.y2 <= box.y1) {
    out.error = "裁剪区域为空 (x1,y1,x2,y2: " + std::to_string(box.x1) + ","
                + std::to_string(box.y1) + "," + std::to_string(box.x2) + ","
                + std::to_string(box.y2) + ")";
    return out;
  }
  const int cw = box.x2 - box.x1;
  const int ch = box.y2 - box.y1;
  // 裁剪到连续 RGB。
  std::vector<uint8_t> crop(static_cast<size_t>(cw) * ch * 3);
  for (int y = 0; y < ch; ++y) {
    const size_t srcOff = (static_cast<size_t>(box.y1 + y) * img.width + box.x1) * 3;
    std::memcpy(&crop[static_cast<size_t>(y) * cw * 3], &img.rgb[srcOff], cw * 3);
  }
  int outW = cw;
  int outH = ch;
  std::vector<uint8_t> finalRgb;
  if (scale > 1) {
    resizeRgbBilinear(crop, cw, ch, finalRgb, cw * scale, ch * scale);
    outW = cw * scale;
    outH = ch * scale;
  } else {
    finalRgb = std::move(crop);
  }
  std::string error;
  if (!writeRgbFile(outputPath, outW, outH, finalRgb, error)) {
    out.error = error;
    return out;
  }
  out.ok = true;
  out.width = outW;
  out.height = outH;
  return out;
}

// ---------------- 像素 diff ----------------

namespace {
inline int diffScore(int a, int b) { return std::abs(a - b); }
}  // namespace

PixelDiffResult computePixelDiff(const std::string& originalPath,
                                 const std::string& rebuiltPath, int grid, int top) {
  PixelDiffResult out;
  DecodedImage original = decodeImageToRgb(originalPath);
  if (!original.ok) {
    out.error = "参考图失败: " + original.error;
    return out;
  }
  DecodedImage rebuilt = decodeImageToRgb(rebuiltPath);
  if (!rebuilt.ok) {
    out.error = "重建图失败: " + rebuilt.error;
    return out;
  }
  const int width = original.width;
  const int height = original.height;
  out.width = width;
  out.height = height;
  out.rebuiltWidth = rebuilt.width;
  out.rebuiltHeight = rebuilt.height;

  // 重建图缩放到参考图尺寸 (尺寸不一致本身就是发现, 记 scaled)。
  const std::vector<uint8_t>* rot = &rebuilt.rgb;
  std::vector<uint8_t> resized;
  if (rebuilt.width != width || rebuilt.height != height) {
    resizeRgbBilinear(rebuilt.rgb, rebuilt.width, rebuilt.height, resized, width, height);
    rot = &resized;
    out.scaled = true;
  }

  const size_t count = static_cast<size_t>(width) * height;
  std::vector<double> grayPerPixel(count, 0.0);
  double acc = 0.0;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t* a = &original.rgb[i * 3];
    const uint8_t* b = &(*rot)[i * 3];
    const double g = (diffScore(a[0], b[0]) + diffScore(a[1], b[1]) + diffScore(a[2], b[2])) / 3.0;
    grayPerPixel[i] = g / 255.0 * 100.0;
    acc += g;
  }
  out.overallDifferencePct = acc / static_cast<double>(count) / 255.0 * 100.0;

  // 热力图: 低差异显示青色, 高差异显示红色。
  out.heatmap.resize(count * 3);
  for (size_t i = 0; i < count; ++i) {
    const double v = grayPerPixel[i];
    const uint8_t t = static_cast<uint8_t>(std::clamp(v, 0.0, 255.0));
    out.heatmap[i * 3 + 0] = t;          // red 随差异升高
    out.heatmap[i * 3 + 1] = 255 - t;    // green 随差异降低
    out.heatmap[i * 3 + 2] = 255 - t;    // blue 随差异降低
  }

  // 格子均值 → 最差区块 (对齐 toolkit cell_scores)。
  if (grid < 1) grid = 1;
  if (grid > 64) grid = 64;
  struct Cell {
    double score;
    VisionBox box;
  };
  std::vector<Cell> cells;
  for (int row = 0; row < grid; ++row) {
    for (int col = 0; col < grid; ++col) {
      const int x1 = static_cast<int>(std::round(col * width / grid));
      const int x2 = static_cast<int>(std::round((col + 1) * width / grid));
      const int y1 = static_cast<int>(std::round(row * height / grid));
      const int y2 = static_cast<int>(std::round((row + 1) * height / grid));
      if (x2 <= x1 || y2 <= y1) continue;
      double sum = 0.0;
      int n = 0;
      for (int y = y1; y < y2; ++y) {
        for (int x = x1; x < x2; ++x) {
          sum += grayPerPixel[static_cast<size_t>(y) * width + x];
          ++n;
        }
      }
      Cell cell;
      cell.score = sum / std::max(n, 1);
      cell.box = VisionBox{x1, y1, x2, y2};
      cells.push_back(cell);
    }
  }
  std::sort(cells.begin(), cells.end(),
            [](const Cell& lhs, const Cell& rhs) { return lhs.score > rhs.score; });
  const size_t take = std::min(static_cast<size_t>(top < 1 ? 1 : top), cells.size());
  for (size_t k = 0; k < take; ++k) {
    DiffRegion region;
    region.index = static_cast<int>(k + 1);
    region.differencePct = cells[k].score;
    region.box = cells[k].box;
    out.worst.push_back(region);
  }
  out.ok = true;
  return out;
}

}