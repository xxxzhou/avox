#include "MediaThumbRenderer.hpp"

#include <vector>

#include "MediaThumbUtil.hpp"
#include "avox/AvoxRuntime.h"

namespace avox {

MediaThumbRenderer::MediaThumbRenderer() {}

MediaThumbRenderer::~MediaThumbRenderer() {
  // enableImage 契约: 先摘图再放缓冲, 否则图分支仍指向已析构的 ImageBuffer
  if (renderer) {
    renderer->disableImage();
  }
  outBuffer.reset();
  renderer.reset();
}

bool MediaThumbRenderer::available() { return isVulkanAvailable(); }

bool MediaThumbRenderer::supportsYuv(YuvType type) {
  return thumbSupportsYuv(type);
}

bool MediaThumbRenderer::ensureRenderer() {
  if (renderer) {
    return true;
  }
  if (!available()) {
    return false;
  }
  renderer = std::make_unique<SurfaceRenderVk>();
  // 离屏 + 只走 enableImage 图分支, 不开 CPU YUV 输出
  renderer->setOffSurface(YuvType::other);
  renderer->setHdrMode(HdrMode::follow);
  outBuffer = std::make_unique<ImageBuffer>();
  return true;
}

bool MediaThumbRenderer::prepare(int32_t requestWidth, int32_t srcWidth,
                                 int32_t srcHeight, int32_t rotation,
                                 YuvType type, const ColorSpaceDesc& cs) {
  requestWidth = clampThumbWidth(requestWidth);
  if (srcWidth <= 0 || srcHeight <= 0 || !supportsYuv(type)) {
    return false;
  }
  if (!ensureRenderer()) {
    return false;
  }
  // 请求宽/旋转变化同样要重配目标图(输出尺寸与转置层都变了)
  const bool bRebuild = !bPrepared || this->requestWidth != requestWidth ||
                        this->srcWidth != srcWidth ||
                        this->srcHeight != srcHeight || this->rotation != rotation;
  this->requestWidth = requestWidth;
  this->rotation = rotation;
  colorSpace = cs;
  this->srcWidth = srcWidth;
  this->srcHeight = srcHeight;
  if (!bRebuild) {
    // 颜色矩阵/元数据只落成员, 不重建图
    renderer->setColorSpace(cs);
    return true;
  }
  const int32_t outHeight =
      thumbDisplayHeight(requestWidth, srcWidth, srcHeight, rotation);
  if (outHeight <= 0) {
    return false;
  }
  renderer->setVideoRotation(rotation);
  renderer->setColorSpace(cs);
  ImageFormat format = {};
  format.width = requestWidth;
  format.height = outHeight;
  format.imageType = ImageType::rgba8;
  outBuffer->setImageFormat(format);
  renderer->enableImage(outBuffer.get());
  // 图重建帧里用户缓冲引用的是旧映射, 必须先渲一张同规格黑帧
  bDummyDone = false;
  bPrepared = true;
  return true;
}

void MediaThumbRenderer::setHdrMeta(const HdrMeta& meta) {
  if (renderer) {
    renderer->setHdrMeta(meta);
  }
}

void MediaThumbRenderer::setDoviMeta(const DoviMeta& meta) {
  if (renderer) {
    renderer->setDoviMeta(meta);
  }
}

bool MediaThumbRenderer::buildDummy(const YUVFormat& format,
                                   std::vector<uint8_t>& data,
                                   YUVFrame* out) const {
  const int32_t width = format.width;
  const int32_t height = format.height;
  if (width <= 0 || height <= 0) {
    return false;
  }
  data.clear();
  *out = YUVFrame{};
  out->format = format;
  const size_t w = static_cast<size_t>(width);
  const size_t h = static_cast<size_t>(height);
  switch (format.type) {
    case YuvType::nv12: {
      const size_t ySize = w * h;
      data.assign(ySize + ySize / 2, 0);
      out->data[0] = data.data();
      out->stride[0] = width;
      out->data[1] = data.data() + ySize;
      out->stride[1] = width;
      return true;
    }
    case YuvType::yuv420P:
    case YuvType::yuv422P: {
      const size_t cw = (w + 1) / 2;
      const size_t ch = format.type == YuvType::yuv420P ? (h + 1) / 2 : h;
      const size_t ySize = w * h;
      const size_t cSize = cw * ch;
      data.assign(ySize + cSize * 2, 0);
      out->data[0] = data.data();
      out->stride[0] = width;
      out->data[1] = data.data() + ySize;
      out->stride[1] = static_cast<int32_t>(cw);
      out->data[2] = out->data[1] + cSize;
      out->stride[2] = static_cast<int32_t>(cw);
      return true;
    }
    case YuvType::yuv420P10:
    case YuvType::yuv422P10: {
      // 10bit 紧排: Y 行宽 2w, 色度行宽 w 字节; 422 色度全高, 420 色度行向上取整
      const size_t ySize = w * 2 * h;
      const size_t ch = format.type == YuvType::yuv420P10 ? (h + 1) / 2 : h;
      const size_t cSize = w * ch;
      data.assign(ySize + cSize * 2, 0);
      out->data[0] = data.data();
      out->stride[0] = width * 2;
      out->data[1] = data.data() + ySize;
      out->stride[1] = width;
      out->data[2] = out->data[1] + cSize;
      out->stride[2] = width;
      return true;
    }
    case YuvType::p010: {
      // 与 FFmpeg P010 同形: UV 交错单平面, 2 字节/样本(色度行向上取整)
      const size_t ySize = w * 2 * h;
      const size_t uvSize = w * 2 * ((h + 1) / 2);
      data.assign(ySize + uvSize, 0);
      out->data[0] = data.data();
      out->stride[0] = width * 2;
      out->data[1] = data.data() + ySize;
      out->stride[1] = width * 2;
      return true;
    }
    default:
      return false;
  }
}

bool MediaThumbRenderer::render(const YUVFrame& frame) {
  if (!bPrepared || !renderer || !outBuffer) {
    return false;
  }
  if (frame.format.width <= 0 || frame.format.height <= 0 || !frame.data[0]) {
    return false;
  }
  // 分辨率中途变化: 按原请求宽/旋转重配目标图
  if (frame.format.width != srcWidth || frame.format.height != srcHeight) {
    if (!prepare(requestWidth, frame.format.width, frame.format.height,
                 rotation, frame.format.type, colorSpace)) {
      return false;
    }
  }
  if (!bDummyDone) {
    YUVFrame dummy = {};
    if (!buildDummy(frame.format, dummyData, &dummy)) {
      return false;
    }
    renderer->render(dummy);
    bDummyDone = true;
  }
  renderer->render(frame);
  IImageBuffer* out = image();
  return out != nullptr && out->getImageFormat().width > 0;
}

IImageBuffer* MediaThumbRenderer::image() { return outBuffer.get(); }

}  // namespace avox
