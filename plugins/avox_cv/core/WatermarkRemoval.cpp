#include "WatermarkRemoval.hpp"

#include "../detect/YOLODetector.hpp"
#include "../inpaint/LamaInpainter.hpp"
#include "../inpaint/AOTGanInpainter.hpp"
#include "../inpaint/TiledInpainter.hpp"
#include "MaskProcessor.hpp"

#include "avox/Avox.hpp"
#include "avox/AvoxVision.h"
#include "avox/vision/OnnxModel.hpp"
#include "avox/module/LogHelper.hpp"

#include <chrono>
#include <cstring>

namespace avox {

// ============== 辅助函数: IImageBuffer <-> RGB 转换 ==============

// 从 IImageBuffer 提取 RGB 数据
static bool imageBufferToRGB(IImageBuffer* buffer, std::vector<uint8_t>& rgbData,
                              int& width, int& height) {
  if (!buffer) return false;

  ImageFormat format = buffer->getImageFormat();
  if (!format.bVailid()) return false;

  width = format.width;
  height = format.height;

  const uint8_t* srcData = buffer->getPointer();
  if (!srcData) return false;

  int srcStride = format.rowPitch > 0 ? format.rowPitch : width * getPixelSize(format.imageType);
  rgbData.resize(width * height * 3);

  // 根据图像类型转换
  switch (format.imageType) {
    case ImageType::rgb8:
      // 直接复制，可能需要处理步幅
      for (int y = 0; y < height; y++) {
        std::memcpy(rgbData.data() + y * width * 3, srcData + y * srcStride, width * 3);
      }
      break;

    case ImageType::rgba8:
    case ImageType::bgra8:
    case ImageType::argb8:
      // 去除 Alpha 通道
      for (int y = 0; y < height; y++) {
        const uint8_t* srcRow = srcData + y * srcStride;
        uint8_t* dstRow = rgbData.data() + y * width * 3;

        for (int x = 0; x < width; x++) {
          if (format.imageType == ImageType::rgba8) {
            dstRow[x * 3 + 0] = srcRow[x * 4 + 0];  // R
            dstRow[x * 3 + 1] = srcRow[x * 4 + 1];  // G
            dstRow[x * 3 + 2] = srcRow[x * 4 + 2];  // B
          } else if (format.imageType == ImageType::bgra8) {
            dstRow[x * 3 + 0] = srcRow[x * 4 + 2];  // R (from B)
            dstRow[x * 3 + 1] = srcRow[x * 4 + 1];  // G
            dstRow[x * 3 + 2] = srcRow[x * 4 + 0];  // B (from R)
          } else { // argb8
            dstRow[x * 3 + 0] = srcRow[x * 4 + 1];  // R
            dstRow[x * 3 + 1] = srcRow[x * 4 + 2];  // G
            dstRow[x * 3 + 2] = srcRow[x * 4 + 3];  // B
          }
        }
      }
      break;

    case ImageType::bgr8:
      // BGR 转 RGB
      for (int y = 0; y < height; y++) {
        const uint8_t* srcRow = srcData + y * srcStride;
        uint8_t* dstRow = rgbData.data() + y * width * 3;

        for (int x = 0; x < width; x++) {
          dstRow[x * 3 + 0] = srcRow[x * 3 + 2];  // R
          dstRow[x * 3 + 1] = srcRow[x * 3 + 1];  // G
          dstRow[x * 3 + 2] = srcRow[x * 3 + 0];  // B
        }
      }
      break;

    default:
      LOGFLF(LogLevel::error, "[imageBufferToRGB] Unsupported image type: ", static_cast<int>(format.imageType));
      return false;
  }

  return true;
}

// 将 RGB 数据写入 IImageBuffer
static bool rgbToImageBuffer(const uint8_t* rgbData, int width, int height,
                              IImageBuffer* buffer, ImageType outputType) {
  if (!buffer || !rgbData) return false;

  ImageFormat format;
  format.width = width;
  format.height = height;
  format.imageType = outputType;
  format.rowPitch = width * getPixelSize(outputType);

  buffer->setImageFormat(format);
  uint8_t* dstData = buffer->getPointer();
  if (!dstData) return false;

  int dstStride = format.rowPitch;

  // 根据输出类型转换
  switch (outputType) {
    case ImageType::rgb8:
      for (int y = 0; y < height; y++) {
        std::memcpy(dstData + y * dstStride, rgbData + y * width * 3, width * 3);
      }
      break;

    case ImageType::rgba8:
      for (int y = 0; y < height; y++) {
        const uint8_t* srcRow = rgbData + y * width * 3;
        uint8_t* dstRow = dstData + y * dstStride;

        for (int x = 0; x < width; x++) {
          dstRow[x * 4 + 0] = srcRow[x * 3 + 0];  // R
          dstRow[x * 4 + 1] = srcRow[x * 3 + 1];  // G
          dstRow[x * 4 + 2] = srcRow[x * 3 + 2];  // B
          dstRow[x * 4 + 3] = 255;                // A
        }
      }
      break;

    case ImageType::bgra8:
      for (int y = 0; y < height; y++) {
        const uint8_t* srcRow = rgbData + y * width * 3;
        uint8_t* dstRow = dstData + y * dstStride;

        for (int x = 0; x < width; x++) {
          dstRow[x * 4 + 0] = srcRow[x * 3 + 2];  // B
          dstRow[x * 4 + 1] = srcRow[x * 3 + 1];  // G
          dstRow[x * 4 + 2] = srcRow[x * 3 + 0];  // R
          dstRow[x * 4 + 3] = 255;                // A
        }
      }
      break;

    case ImageType::bgr8:
      for (int y = 0; y < height; y++) {
        const uint8_t* srcRow = rgbData + y * width * 3;
        uint8_t* dstRow = dstData + y * dstStride;

        for (int x = 0; x < width; x++) {
          dstRow[x * 3 + 0] = srcRow[x * 3 + 2];  // B
          dstRow[x * 3 + 1] = srcRow[x * 3 + 1];  // G
          dstRow[x * 3 + 2] = srcRow[x * 3 + 0];  // R
        }
      }
      break;

    default:
      LOGFLF(LogLevel::error, "[rgbToImageBuffer] Unsupported output image type: ", static_cast<int>(outputType));
      return false;
  }

  return true;
}

// ============== WatermarkRemoval::Impl ==============

class WatermarkRemoval::Impl {
 public:
  // 检测器
  YOLODetector yoloDetector;

  // 修复器
  LamaInpainter lamaInpainter;
  AOTGanInpainter aotganInpainter;
  TiledInpainter tiledInpainter;

  // 当前使用的修复器类型
  InpaintMode currentModelType = InpaintMode::lama;

  // Mask 处理器
  MaskProcessor maskProcessor;

  // 配置
  InpaintConfig config;
  ModelLevel modelLevel = ModelLevel::base;

  // 临时缓冲
  std::vector<uint8_t> maskBuffer;
  std::vector<uint8_t> rgbInputBuffer;
  std::vector<uint8_t> rgbOutputBuffer;

  // 生成 Mask (分割掩码)
  void generateMaskFromSeg(const std::vector<WatermarkSeg>& segs,
                           int width, int height);

  // 执行修复 (根据配置选择分块或直接处理)
  bool doInpaint(const uint8_t* rgbImage,
                 const uint8_t* mask,
                 int width, int height,
                 uint8_t* output);
};

void WatermarkRemoval::Impl::generateMaskFromSeg(const std::vector<WatermarkSeg>& segs,
                                                  int width, int height) {
  maskBuffer.assign(width * height, 0);

  for (const auto& seg : segs) {
    // 过滤太小的检测框
    if (seg.bbox.width < config.minWatermarkSize || seg.bbox.height < config.minWatermarkSize) {
      continue;
    }

    if (!seg.valid()) {
      // 如果没有分割掩码，退回到边界框
      int dilate = config.maskDilate;
      int x1 = std::max(0, static_cast<int>(seg.bbox.x) - dilate);
      int y1 = std::max(0, static_cast<int>(seg.bbox.y) - dilate);
      int x2 = std::min(width, static_cast<int>(seg.bbox.x + seg.bbox.width) + dilate);
      int y2 = std::min(height, static_cast<int>(seg.bbox.y + seg.bbox.height) + dilate);

      for (int y = y1; y < y2; y++) {
        for (int x = x1; x < x2; x++) {
          maskBuffer[y * width + x] = 255;
        }
      }
    } else {
      // 使用精确分割掩码
      // 将掩码映射到原图坐标
      float scaleX = seg.bbox.width / seg.maskWidth;
      float scaleY = seg.bbox.height / seg.maskHeight;

      for (int my = 0; my < seg.maskHeight; my++) {
        for (int mx = 0; mx < seg.maskWidth; mx++) {
          if (seg.mask[my * seg.maskWidth + mx] > 0) {
            // 映射到原图坐标
            int dstX = static_cast<int>(seg.bbox.x + mx * scaleX);
            int dstY = static_cast<int>(seg.bbox.y + my * scaleY);

            if (dstX >= 0 && dstX < width && dstY >= 0 && dstY < height) {
              maskBuffer[dstY * width + dstX] = 255;
            }
          }
        }
      }
    }
  }

  // 二值化处理 (确保 mask 只有 0 和 255)
  for (auto& v : maskBuffer) {
    v = v > 127 ? 255 : 0;
  }

  // 使用 MaskProcessor 进行形态学处理
  maskProcessor.process(maskBuffer, width, height,
                        config.maskDilate,
                        config.maskErode,
                        config.maskBlur,
                        config.maskClose,
                        config.maskOpen);
}

bool WatermarkRemoval::Impl::doInpaint(const uint8_t* rgbImage,
                                        const uint8_t* mask,
                                        int width, int height,
                                        uint8_t* output) {
  // 判断是否需要分块处理
  bool useTiledProcessing = config.useTiled ||
                            (width > config.tileSize || height > config.tileSize);

  // 根据模型类型选择修复器
  bool useAOTGan = (config.modelType == InpaintMode::aotgan);

  if (useTiledProcessing) {
    tiledInpainter.setTileSize(config.tileSize);
    tiledInpainter.setOverlap(config.tileOverlap);

    if (useAOTGan) {
      tiledInpainter.setInpainter(&aotganInpainter);
      return tiledInpainter.inpaintWithAOTGan(rgbImage, mask, width, height, output);
    } else {
      tiledInpainter.setInpainter(&lamaInpainter);
      return tiledInpainter.inpaintWithLama(rgbImage, mask, width, height, output);
    }
  } else {
    if (useAOTGan) {
      return aotganInpainter.inpaint(rgbImage, mask, width, height, output);
    } else {
      return lamaInpainter.inpaint(rgbImage, mask, width, height, output);
    }
  }
}

// ============== WatermarkRemoval ==============

WatermarkRemoval::WatermarkRemoval()
    : impl(std::make_unique<Impl>()) {}

WatermarkRemoval::~WatermarkRemoval() = default;

// === IWatermarkRemoval 接口实现 ===

void WatermarkRemoval::setModelLevel(ModelLevel level) {
  impl->modelLevel = level;
}

void WatermarkRemoval::setInpaintMode(InpaintMode mode) {
  impl->config.modelType = mode;
}

void WatermarkRemoval::setMaskDilate(int dilatePixels) {
  impl->config.maskDilate = dilatePixels;
}

void WatermarkRemoval::setDetectThreshold(float threshold) {
  impl->config.detectThreshold = threshold;
}

void WatermarkRemoval::setUseGPU(bool useGPU) {
  impl->config.useGPU = useGPU;
}

bool WatermarkRemoval::open() {
  // 保存当前模型类型
  impl->currentModelType = impl->config.modelType;

  // 根据配置加载修复模型
  if (impl->config.modelType == InpaintMode::aotgan) {
    // 加载 AOT-GAN 模型
    if (!impl->aotganInpainter.open(impl->config.useGPU)) {
      LOGFLF(LogLevel::warn, "[Inpaint] Failed to load AOT-GAN model");
      return false;
    }
    LOGFLF(LogLevel::info, "[Inpaint] Using AOT-GAN model");
  } else {
    // 加载 LaMa 模型 (默认)
    if (!impl->lamaInpainter.open(impl->modelLevel, impl->config.useGPU)) {
      LOGFLF(LogLevel::warn, "[Inpaint] Failed to load LaMa model");
      return false;
    }
    LOGFLF(LogLevel::info, "[Inpaint] Using LaMa model");
  }

  // 根据 ModelLevel 选 YOLO-Seg 模型 (路径集中在 OnnxModel 表里)
  OnnxModel yoloModel = OnnxModel::YoloSegBase;
  switch (impl->modelLevel) {
    case ModelLevel::mini:
      yoloModel = OnnxModel::YoloSegMini;
      break;
    case ModelLevel::high:
      yoloModel = OnnxModel::YoloSegHigh;
      break;
    case ModelLevel::base:
    default:
      yoloModel = OnnxModel::YoloSegBase;
      break;
  }
  if (!impl->yoloDetector.open(yoloModel, impl->config.useGPU)) {
    LOGFLF(LogLevel::warn, "[Inpaint] Failed to load YOLO-Seg model (modelLevel=",
           static_cast<int>(impl->modelLevel), ")");
    return false;
  }

  LOGFLF(LogLevel::info, "[Inpaint] Models loaded successfully");
  return true;
}

void WatermarkRemoval::close() {
  impl->yoloDetector.close();
  impl->lamaInpainter.close();
  impl->aotganInpainter.close();
}

bool WatermarkRemoval::ready() {
  if (impl->config.modelType == InpaintMode::aotgan) {
    return impl->aotganInpainter.ready();
  }
  return impl->lamaInpainter.ready();
}

bool WatermarkRemoval::process(IImageBuffer* input, IImageBuffer* output) {
  lastResult = InpaintResult();
  auto totalStart = std::chrono::high_resolution_clock::now();

  // 转换输入为 RGB
  int width, height;
  if (!imageBufferToRGB(input, impl->rgbInputBuffer, width, height)) {
    LOGFLF(LogLevel::error, "[process] Failed to convert input buffer to RGB");
    return false;
  }

  // 检测
  auto detectStart = std::chrono::high_resolution_clock::now();
  auto segs = impl->yoloDetector.detectWithMask(
      impl->rgbInputBuffer.data(), width, height,
      impl->config.detectThreshold,
      impl->config.nmsThreshold);
  auto detectEnd = std::chrono::high_resolution_clock::now();
  lastResult.detectTimeMs = std::chrono::duration<float, std::milli>(
      detectEnd - detectStart).count();

  // 保存检测结果
  for (const auto& seg : segs) {
    WatermarkInfo info;
    info.bbox = seg.bbox;
    info.type = static_cast<WatermarkType>(seg.bbox.classId);
    lastResult.watermarks.push_back(info);
  }

  // 生成 Mask
  if (!segs.empty()) {
    impl->generateMaskFromSeg(segs, width, height);
  } else {
    impl->maskBuffer.assign(width * height, 0);
  }

  // 准备输出缓冲
  impl->rgbOutputBuffer.resize(width * height * 3);

  // 修复
  auto inpaintStart = std::chrono::high_resolution_clock::now();
  if (!segs.empty()) {
    impl->doInpaint(
        impl->rgbInputBuffer.data(),
        impl->maskBuffer.data(),
        width, height,
        impl->rgbOutputBuffer.data());
  } else {
    std::memcpy(impl->rgbOutputBuffer.data(), impl->rgbInputBuffer.data(), impl->rgbInputBuffer.size());
  }
  auto inpaintEnd = std::chrono::high_resolution_clock::now();
  lastResult.inpaintTimeMs = std::chrono::duration<float, std::milli>(
      inpaintEnd - inpaintStart).count();

  // 转换输出
  ImageType outputType = input->getImageFormat().imageType;
  if (!rgbToImageBuffer(impl->rgbOutputBuffer.data(), width, height, output, outputType)) {
    LOGFLF(LogLevel::error, "[process] Failed to convert RGB to output buffer");
    return false;
  }

  auto totalEnd = std::chrono::high_resolution_clock::now();
  lastResult.totalTimeMs = std::chrono::duration<float, std::milli>(
      totalEnd - totalStart).count();
  lastResult.success = true;

  return true;
}

bool WatermarkRemoval::detect(IImageBuffer* input, IImageBuffer* maskOutput) {
  lastResult = InpaintResult();

  // 转换输入为 RGB
  int width, height;
  if (!imageBufferToRGB(input, impl->rgbInputBuffer, width, height)) {
    LOGFLF(LogLevel::error, "[detect] Failed to convert input buffer to RGB");
    return false;
  }

  // 检测
  auto detectStart = std::chrono::high_resolution_clock::now();
  auto segs = impl->yoloDetector.detectWithMask(
      impl->rgbInputBuffer.data(), width, height,
      impl->config.detectThreshold,
      impl->config.nmsThreshold);
  auto detectEnd = std::chrono::high_resolution_clock::now();
  lastResult.detectTimeMs = std::chrono::duration<float, std::milli>(
      detectEnd - detectStart).count();

  // 保存检测结果
  for (const auto& seg : segs) {
    WatermarkInfo info;
    info.bbox = seg.bbox;
    info.type = static_cast<WatermarkType>(seg.bbox.classId);
    lastResult.watermarks.push_back(info);
  }

  if (segs.empty()) {
    return false;
  }

  // 生成 Mask
  impl->generateMaskFromSeg(segs, width, height);

  // 输出 mask 到 IImageBuffer
  if (maskOutput) {
    ImageFormat maskFormat;
    maskFormat.width = width;
    maskFormat.height = height;
    maskFormat.imageType = ImageType::r8;  // 单通道灰度
    maskFormat.rowPitch = width;
    maskOutput->setImageFormat(maskFormat);

    uint8_t* dstData = maskOutput->getPointer();
    if (dstData) {
      std::memcpy(dstData, impl->maskBuffer.data(), width * height);
    }
  }

  return true;
}

bool WatermarkRemoval::inpaint(IImageBuffer* input, IImageBuffer* mask, IImageBuffer* output) {
  lastResult = InpaintResult();

  if (!mask) return false;

  // 转换输入为 RGB
  int width, height;
  if (!imageBufferToRGB(input, impl->rgbInputBuffer, width, height)) {
    LOGFLF(LogLevel::error, "[inpaint] Failed to convert input buffer to RGB");
    return false;
  }

  // 获取 mask 数据
  ImageFormat maskFormat = mask->getImageFormat();
  if (!maskFormat.bVailid()) {
    LOGFLF(LogLevel::error, "[inpaint] Invalid mask format");
    return false;
  }

  const uint8_t* maskData = mask->getPointer();
  if (!maskData) {
    LOGFLF(LogLevel::error, "[inpaint] Mask has no data");
    return false;
  }

  // 如果 mask 不是单通道，提取第一个通道
  std::vector<uint8_t> grayMask;
  const uint8_t* maskPtr = maskData;

  if (maskFormat.imageType != ImageType::r8) {
    grayMask.resize(width * height);
    int maskPixelSize = getPixelSize(maskFormat.imageType);
    int maskStride = maskFormat.rowPitch > 0 ? maskFormat.rowPitch : maskFormat.width * maskPixelSize;

    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        // 取第一个通道 (R 或 gray)
        grayMask[y * width + x] = maskData[y * maskStride + x * maskPixelSize];
      }
    }
    maskPtr = grayMask.data();
  }

  // 准备输出缓冲
  impl->rgbOutputBuffer.resize(width * height * 3);

  // 修复
  auto inpaintStart = std::chrono::high_resolution_clock::now();
  bool success = impl->doInpaint(
      impl->rgbInputBuffer.data(), maskPtr,
      width, height,
      impl->rgbOutputBuffer.data());
  auto inpaintEnd = std::chrono::high_resolution_clock::now();
  lastResult.inpaintTimeMs = std::chrono::duration<float, std::milli>(
      inpaintEnd - inpaintStart).count();

  if (!success) return false;

  // 转换输出
  ImageType outputType = input->getImageFormat().imageType;
  return rgbToImageBuffer(impl->rgbOutputBuffer.data(), width, height, output, outputType);
}

int WatermarkRemoval::getWatermarkCount() {
  return static_cast<int>(lastResult.watermarks.size());
}

bool WatermarkRemoval::getWatermarkBBox(int index, float* x, float* y, float* w, float* h) {
  if (index < 0 || index >= static_cast<int>(lastResult.watermarks.size())) {
    return false;
  }

  const auto& bbox = lastResult.watermarks[index].bbox;
  if (x) *x = bbox.x;
  if (y) *y = bbox.y;
  if (w) *w = bbox.width;
  if (h) *h = bbox.height;
  return true;
}

float WatermarkRemoval::getDetectTimeMs() {
  return lastResult.detectTimeMs;
}

float WatermarkRemoval::getInpaintTimeMs() {
  return lastResult.inpaintTimeMs;
}

// === 扩展接口实现 ===

void WatermarkRemoval::setDetectMode(DetectMode mode) {
  // 保留接口兼容性，默认使用 YOLO 模式
}

void WatermarkRemoval::setConfig(const InpaintConfig& config) {
  impl->config = config;
}

void WatermarkRemoval::beginVideo(int width, int height) {
  // 仅保留接口
}

InpaintResult WatermarkRemoval::processVideoFrame(IImageBuffer* input, IImageBuffer* output) {
  InpaintResult result;
  auto totalStart = std::chrono::high_resolution_clock::now();

  // 转换输入为 RGB
  int width, height;
  if (!imageBufferToRGB(input, impl->rgbInputBuffer, width, height)) {
    return result;
  }

  // YOLO-Seg 检测
  auto detectStart = std::chrono::high_resolution_clock::now();
  auto segs = impl->yoloDetector.detectWithMask(
      impl->rgbInputBuffer.data(), width, height,
      impl->config.detectThreshold,
      impl->config.nmsThreshold);
  auto detectEnd = std::chrono::high_resolution_clock::now();
  result.detectTimeMs = std::chrono::duration<float, std::milli>(
      detectEnd - detectStart).count();

  // 转换结果
  for (const auto& seg : segs) {
    WatermarkInfo info;
    info.bbox = seg.bbox;
    info.type = WatermarkType::unknown;
    result.watermarks.push_back(info);
  }

  // 生成 Mask
  if (!segs.empty()) {
    impl->generateMaskFromSeg(segs, width, height);
  } else {
    impl->maskBuffer.assign(width * height, 0);
  }

  // 准备输出缓冲
  impl->rgbOutputBuffer.resize(width * height * 3);

  // 修复
  auto inpaintStart = std::chrono::high_resolution_clock::now();
  if (!segs.empty()) {
    impl->doInpaint(
        impl->rgbInputBuffer.data(),
        impl->maskBuffer.data(),
        width, height,
        impl->rgbOutputBuffer.data());
  } else {
    std::memcpy(impl->rgbOutputBuffer.data(), impl->rgbInputBuffer.data(), impl->rgbInputBuffer.size());
  }
  auto inpaintEnd = std::chrono::high_resolution_clock::now();
  result.inpaintTimeMs = std::chrono::duration<float, std::milli>(
      inpaintEnd - inpaintStart).count();

  // 转换输出
  ImageType outputType = input->getImageFormat().imageType;
  rgbToImageBuffer(impl->rgbOutputBuffer.data(), width, height, output, outputType);

  auto totalEnd = std::chrono::high_resolution_clock::now();
  result.totalTimeMs = std::chrono::duration<float, std::milli>(
      totalEnd - totalStart).count();
  result.success = true;

  return result;
}

void WatermarkRemoval::endVideo() {
  // 清理状态
}

// === 扩展接口实现 ===

std::vector<WatermarkSeg> WatermarkRemoval::detectWithMask(IImageBuffer* input) {
  int width, height;
  if (!imageBufferToRGB(input, impl->rgbInputBuffer, width, height)) {
    return {};
  }

  return impl->yoloDetector.detectWithMask(
      impl->rgbInputBuffer.data(), width, height,
      impl->config.detectThreshold,
      impl->config.nmsThreshold);
}

InpaintMask WatermarkRemoval::getSegMask() const {
  InpaintMask mask;
  mask.data = impl->maskBuffer.data();
  mask.width = static_cast<int>(std::sqrt(impl->maskBuffer.size()));
  mask.height = mask.width > 0 ? static_cast<int>(impl->maskBuffer.size() / mask.width) : 0;
  return mask;
}

// 工厂由 CvModule::loadModule 注册 []{ return new WatermarkRemoval(); } 到 AvoxManager::watermarkRemovalHub.create("inpaint")

}
