
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../module/AvoxManager.hpp"  // imageProcHub (cv 能力经 avox_opencv 插件提供)
#include "../module/LogHelper.hpp"
#include "../module/Sha256.hpp"
#include "IImageProc.hpp"
#include "ImageBuffer.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "../../3rdparty/stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../3rdparty/stb/stb_image_write.h"

#ifdef __ANDROID__
#include <android/asset_manager.h>
#endif

namespace avox {

static int getStbChannels(ImageType type) {
  switch (type) {
    case ImageType::r8:
    case ImageType::r16:
    case ImageType::r32:
    case ImageType::r32f:
      return 1;
    case ImageType::rgb8:
    case ImageType::bgr8:
    case ImageType::rgb8P:
    case ImageType::bgr8P:
      return 3;
    case ImageType::rgba8:
    case ImageType::bgra8:
    case ImageType::argb8:
    case ImageType::rgba16:
    case ImageType::rgba32:
    case ImageType::rgba32f:
    case ImageType::rg16f:
      return 4;
    default:
      return 4;  // default to RGBA
  }
}

static ImageType stbChannelsToImageType(int channels) {
  switch (channels) {
    case 1:
      return ImageType::r8;
    case 2:
      return ImageType::rg16f;
    case 3:
      return ImageType::rgb8;
    case 4:
      return ImageType::rgba8;
    default:
      return ImageType::rgba8;
  }
}

// OpenCV 图像加载(WEBP/HEIF)/SIMD 缩放逻辑已迁 plugins/avox_opencv/ImageProc.cpp
// (经 IImageProc + imageProcHub), 核心不再链 opencv; loadImagePath/resizeImage 下方经 hub 调用。

bool loadImageAsset(const char* filename, IImageBuffer* buffer) {
  // 处理路径：拼接 assets/images/
  std::string imagePath = getImageFilePath(filename);
  return loadImagePath(imagePath.c_str(), buffer);
}

bool loadImagePath(const char* filePath, IImageBuffer* buffer) {
  ImageBuffer* imageBuffer = dynamic_cast<ImageBuffer*>(buffer);
  if (!imageBuffer) {
    LOGFLF(LogLevel::warn, "buffer is not ImageBuffer");
    return false;
  }
  // 加载图像，stb 自动处理多种格式（BMP/PNG/JPG/TGA 等）
  int width, height, channels;
  // stbi_set_flip_vertically_on_load(1);  // 如果需要翻转Y轴，可以启用
  uint8_t* pixels = stbi_load(filePath, &width, &height, &channels, 0);
  if (pixels) {
    // 设置图像格式
    ImageFormat format = {};
    format.width = width;
    format.height = height;
    format.rowPitch = width * channels;  // 紧密排列
    format.imageType = stbChannelsToImageType(channels);
    imageBuffer->setData(pixels, format, true);
    // 释放 stb 分配的内存
    stbi_image_free(pixels);
    return true;
  }

#ifdef AVOX_ENABLE_OPENCV
  // stb 解码失败(典型: WEBP/HEIF 等 stb 不支持的格式) -> 经 imageProcHub 走 avox_opencv 插件;
  // 插件没装返回 nullptr 降级(仅靠 stb, WEBP 等会加载失败)
  std::unique_ptr<IImageProc> proc(AvoxManager::Get().imageProcHub.create("opencv"));
  if (proc && proc->loadPath(filePath, buffer)) return true;
#endif
  LOGFLF(LogLevel::warn, "load ", filePath,
         " failed(stb):", stbi_failure_reason());
  return false;
}

static IEncodeType getFileEncodeType(const char* filePath) {
  std::string path(filePath);
  auto pos = path.rfind('.');
  if (pos == std::string::npos) return IEncodeType::png;
  std::string ext = path.substr(pos + 1);
  for (auto& c : ext) c = (char)tolower((unsigned char)c);
  if (ext == "jpg" || ext == "jpeg") return IEncodeType::jpg;
  if (ext == "bmp") return IEncodeType::bmp;
  if (ext == "tga") return IEncodeType::tga;
  return IEncodeType::png;
}

static void writeFunc(void* context, void* data, int size) {
  auto* out = static_cast<std::vector<uint8_t>*>(context);
  out->insert(out->end(), (uint8_t*)data, (uint8_t*)data + size);
}

static bool encodeToBuffer(IEncodeConfig config, int32_t width, int32_t height,
                           int32_t pitch, int channels, uint8_t* data,
                           std::vector<uint8_t>& out) {
  switch (config.encodeType) {
    case IEncodeType::jpg:
      stbi_write_jpg_to_func(writeFunc, &out, width, height, channels, data,
                             config.quality);
      break;
    case IEncodeType::png:
      stbi_write_png_to_func(writeFunc, &out, width, height, channels, data,
                             pitch);
      break;
    case IEncodeType::bmp:
      stbi_write_bmp_to_func(writeFunc, &out, width, height, channels, data);
      break;
    case IEncodeType::tga:
      stbi_write_tga_to_func(writeFunc, &out, width, height, channels, data);
      break;
    default:
      return false;
  }
  return !out.empty();
}

bool saveImagePath(const char* filePath, IImageBuffer* buffer) {
  ImageBuffer* imageBuffer = dynamic_cast<ImageBuffer*>(buffer);
  if (!imageBuffer) {
    LOGFLF(LogLevel::warn, "buffer is not ImageBuffer");
    return false;
  }
  ImageFormat format = imageBuffer->getImageFormat();
  int32_t width = format.width;
  int32_t height = format.height;
  int32_t pitch = format.rowPitch;
  if (pitch == 0) {
    pitch = width * getPixelSize(format.imageType);
  }
  uint8_t* data = imageBuffer->getPointer();
  int channels = getStbChannels(format.imageType);
  IEncodeConfig config;
  config.encodeType = getFileEncodeType(filePath);
  config.quality = 85;
  std::vector<uint8_t> out;
  if (!encodeToBuffer(config, width, height, pitch, channels, data, out)) {
    return false;
  }
  FILE* f = fopen(filePath, "wb");
  if (!f) {
    LOGFLF(LogLevel::warn, "open ", filePath, " failed");
    return false;
  }
  size_t written = fwrite(out.data(), 1, out.size(), f);
  fclose(f);
  return written == out.size();
}

const char* getImageBase64(IImageBuffer* buffer, const IEncodeConfig& config) {
  ImageBuffer* imageBuffer = dynamic_cast<ImageBuffer*>(buffer);
  if (!imageBuffer) {
    LOGFLF(LogLevel::warn, "buffer is not ImageBuffer");
    return nullptr;
  }
  ImageFormat format = imageBuffer->getImageFormat();
  int32_t width = format.width;
  int32_t height = format.height;
  int32_t pitch = format.rowPitch;
  if (pitch == 0) {
    pitch = width * getPixelSize(format.imageType);
  }
  uint8_t* data = imageBuffer->getPointer();
  int channels = getStbChannels(format.imageType);
  std::vector<uint8_t> encoded;
  if (!encodeToBuffer(config, width, height, pitch, channels, data, encoded)) {
    LOGFLF(LogLevel::warn, "image encode failed");
    return nullptr;
  }
  static std::mutex sMutex;
  static std::string sResult;
  std::lock_guard<std::mutex> lock(sMutex);
  sResult = base64Encode(encoded);
  return sResult.c_str();
}

bool resizeImage(IImageBuffer* inBuf, IImageBuffer* outBuf, int32_t width,
                 int32_t height) {
  if (!inBuf || !outBuf || width <= 0 || height <= 0) {
    return false;
  }
  ImageFormat inFormat = inBuf->getImageFormat();
  if (!inFormat.bVailid()) {
    return false;
  }
  int32_t pixelSize = getPixelSize(inFormat.imageType);
  int32_t inW = inFormat.width;
  int32_t inH = inFormat.height;
  int32_t inPitch = inFormat.rowPitch > 0 ? inFormat.rowPitch : inW * pixelSize;
  uint8_t* inData = inBuf->getPointer();
  if (!inData) {
    return false;
  }
  // set output format: same imageType, new dimensions
  ImageFormat outFormat = {};
  outFormat.width = width;
  outFormat.height = height;
  outFormat.imageType = inFormat.imageType;
  outBuf->setImageFormat(outFormat);
  uint8_t* outData = outBuf->getPointer();
  int32_t outPitch = width * pixelSize;
  // same size, just copy
  if (inW == width && inH == height) {
    memcpy(outData, inData, inPitch * inH);
    return true;
  }
#ifdef AVOX_ENABLE_OPENCV
  // 经 imageProcHub 走 avox_opencv 插件的 SIMD 加速版(8 位 packed 格式),
  // 插件没装或该格式不支持 -> 返回 false 回退下方手写双线性
  std::unique_ptr<IImageProc> proc(AvoxManager::Get().imageProcHub.create("opencv"));
  if (proc && proc->resize8u(inData, inW, inH, inPitch, outData, width, height,
                             outPitch, inFormat.imageType)) {
    return true;
  }
#endif
  // bilinear interpolation (byte-level, correct for 8-bit-per-channel packed
  // types)
  float xRatio = (float)inW / width;
  float yRatio = (float)inH / height;
  for (int32_t y = 0; y < height; ++y) {
    float srcY = (y + 0.5f) * yRatio - 0.5f;
    int32_t y0 = std::max(0, std::min((int32_t)floorf(srcY), inH - 1));
    int32_t y1 = std::max(0, std::min(y0 + 1, inH - 1));
    float fy = srcY - floorf(srcY);
    if (fy < 0) fy = 0;
    for (int32_t x = 0; x < width; ++x) {
      float srcX = (x + 0.5f) * xRatio - 0.5f;
      int32_t x0 = std::max(0, std::min((int32_t)floorf(srcX), inW - 1));
      int32_t x1 = std::max(0, std::min(x0 + 1, inW - 1));
      float fx = srcX - floorf(srcX);
      if (fx < 0) fx = 0;
      const uint8_t* p00 = inData + y0 * inPitch + x0 * pixelSize;
      const uint8_t* p01 = inData + y0 * inPitch + x1 * pixelSize;
      const uint8_t* p10 = inData + y1 * inPitch + x0 * pixelSize;
      const uint8_t* p11 = inData + y1 * inPitch + x1 * pixelSize;
      uint8_t* out = outData + y * outPitch + x * pixelSize;
      float w00 = (1 - fy) * (1 - fx);
      float w01 = (1 - fy) * fx;
      float w10 = fy * (1 - fx);
      float w11 = fy * fx;
      for (int32_t b = 0; b < pixelSize; ++b) {
        float v = w00 * p00[b] + w01 * p01[b] + w10 * p10[b] + w11 * p11[b];
        out[b] = (uint8_t)std::max(0.0f, std::min(255.0f, v));
      }
    }
  }
  return true;
}

bool cropImage(IImageBuffer* inBuf, IImageBuffer* outBuf, int32_t x, int32_t y,
               int32_t width, int32_t height) {
  if (!inBuf || !outBuf || x < 0 || y < 0 || width <= 0 || height <= 0) {
    return false;
  }
  ImageFormat inFormat = inBuf->getImageFormat();
  if (!inFormat.bVailid()) {
    return false;
  }
  // 裁剪区域不能超出源图像范围
  if (x + width > inFormat.width || y + height > inFormat.height) {
    return false;
  }
  int32_t pixelSize = getPixelSize(inFormat.imageType);
  int32_t inPitch =
      inFormat.rowPitch > 0 ? inFormat.rowPitch : inFormat.width * pixelSize;
  uint8_t* inData = inBuf->getPointer();
  if (!inData) {
    return false;
  }
  // 设置输出格式: 同imageType, 新尺寸
  ImageFormat outFormat = {};
  outFormat.width = width;
  outFormat.height = height;
  outFormat.imageType = inFormat.imageType;
  outBuf->setImageFormat(outFormat);
  uint8_t* outData = outBuf->getPointer();
  int32_t outPitch = width * pixelSize;
  // 逐行复制裁剪区域
  const uint8_t* srcRow = inData + y * inPitch + x * pixelSize;
  for (int32_t row = 0; row < height; ++row) {
    memcpy(outData + row * outPitch, srcRow + row * inPitch, outPitch);
  }
  return true;
}

}
