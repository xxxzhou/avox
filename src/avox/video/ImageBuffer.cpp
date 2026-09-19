#include "ImageBuffer.hpp"

#include <assert.h>

#include "ColorSpace.hpp"

namespace avox {

ImageBuffer::ImageBuffer() {}

ImageBuffer::~ImageBuffer() { buffer.clear(); }

void ImageBuffer::changeBufferSize() {
  int32_t newSize = getImageSize(imageFormat);
  if (buffer.size() != newSize) {
    buffer.resize(newSize);
  }
  data = buffer.data();
  size = newSize;
}

int32_t ImageBuffer::getBufferSize() { return size; }

uint8_t* ImageBuffer::getPointer() { return data; }

void ImageBuffer::setImageFormat(const ImageFormat& imageFormat_) {
  if (imageFormat == imageFormat_) {
    return;
  }
  imageFormat = imageFormat_;
  changeBufferSize();
}

ImageFormat ImageBuffer::getImageFormat() { return imageFormat; }

bool ImageBuffer::bDataRef() { return bDataReference; }

void ImageBuffer::clear() { buffer.clear(); }

void ImageBuffer::setData(uint8_t* data_, const ImageFormat& format,
                          bool bCopy) {
  bDataReference = !bCopy;
  size = getImageSize(format);
  if (bDataReference) {
    imageFormat = format;
    data = data_;
  } else {
    setImageFormat(format);
    memcpy(data, data_, size);
  }
}

void ImageBuffer::copyFrom(IImageBuffer* xbuffer, bool bCopyData) {
  setData(xbuffer->getPointer(), xbuffer->getImageFormat(), bCopyData);
}

void ImageBuffer::copyTo(IImageBuffer* xbuffer, bool bCopyData) {
  ImageBuffer* dest = dynamic_cast<ImageBuffer*>(xbuffer);
  assert(dest);
  dest->setData(data, imageFormat, bCopyData);
}

bool yuvframe2Rgba(const YUVFrame& frame, IImageBuffer* buffer,
                   const ColorSpaceDesc& cs) {
  int32_t width = frame.format.width;
  int32_t height = frame.format.height;
  if (width <= 0 || height <= 0 || !buffer) {
    return false;
  }

  ImageFormat format = {};
  format.width = width;
  format.height = height;
  format.imageType = ImageType::rgba8;
  buffer->setImageFormat(format);

  uint8_t* rgba = buffer->getPointer();
  int32_t rgbaStride = width * 4;

  if (frame.format.type != YuvType::yuv420P &&
      frame.format.type != YuvType::nv12) {
    return false;
  }
  // buildYuvToRgb(cs)与编码侧buildRgbToYuv(cs)严格互逆(含limited展开),
  // 输入归一化[0,1]且UV不预减0.5(-0.5已折叠进偏移列), 与yuv2rgbaV1.comp同源
  const Mat4x4f mat = buildYuvToRgb(cs);
  bool bNv12 = frame.format.type == YuvType::nv12;
  for (int32_t row = 0; row < height; ++row) {
    const uint8_t* yRow = frame.data[0] + (size_t)row * frame.stride[0];
    const uint8_t* uRow =
        frame.data[1] + (size_t)(row / 2) * frame.stride[1];
    const uint8_t* vRow =
        bNv12 ? uRow : frame.data[2] + (size_t)(row / 2) * frame.stride[2];
    uint8_t* rgbaRow = rgba + (size_t)row * rgbaStride;
    for (int32_t col = 0; col < width; ++col) {
      // 420P色度半宽采样点col/2; NV12字节交错对[col*2]=[U,V]
      float yn = yRow[col] / 255.0f;
      float un = (bNv12 ? uRow[col * 2] : uRow[col / 2]) / 255.0f;
      float vn = (bNv12 ? uRow[col * 2 + 1] : vRow[col / 2]) / 255.0f;
      float r = yn * mat.row0.x + un * mat.row0.y + vn * mat.row0.z + mat.row0.w;
      float g = yn * mat.row1.x + un * mat.row1.y + vn * mat.row1.z + mat.row1.w;
      float b = yn * mat.row2.x + un * mat.row2.y + vn * mat.row2.z + mat.row2.w;
      uint8_t* p = rgbaRow + col * 4;
      p[0] = (uint8_t)std::max(0, std::min(255, (int32_t)(r * 255.0f + 0.5f)));
      p[1] = (uint8_t)std::max(0, std::min(255, (int32_t)(g * 255.0f + 0.5f)));
      p[2] = (uint8_t)std::max(0, std::min(255, (int32_t)(b * 255.0f + 0.5f)));
      p[3] = 255;
    }
  }
  return true;
}

// shader把U/V按width×(height/4)紧凑打包(每物理行2条逻辑UV行),
// 加padding后每物理行布局: [前半width/2 | 后半width/2 | padding]
// 重排为: [前半width/2 | pad/2 | 后半width/2 | pad/2]
// 这样stride=rowPitch/2,FFmpeg按等距读取即可
void unpackGpuYUV(IImageBuffer* buffer, YuvType yuvType) {
  ImageFormat imageFormat = buffer->getImageFormat();
  uint8_t* data = buffer->getPointer();
  int32_t rowPitch = imageFormat.rowPitch;
  int32_t width = imageFormat.width;
  if ((yuvType == YuvType::yuv420P || yuvType == YuvType::yuv422P) &&
      rowPitch != width) {
    // shader把U/V紧凑打包(每物理行2条逻辑UV行),
    // 加padding后每物理行布局: [前半width/2 | 后半width/2 | padding]
    // 重排为: [前半width/2 | pad/2 | 后半width/2 | pad/2]
    // 这样stride=rowPitch/2,FFmpeg按等距读取即可
    int32_t halfPitch = rowPitch / 2;
    int32_t halfWidth = width / 2;
    int32_t yHeight, uvPhysRows;
    if (yuvType == YuvType::yuv420P) {
      // imageFormat.height = height * 3/2, Y占 height 行
      yHeight = imageFormat.height * 2 / 3;
      uvPhysRows = yHeight / 2;
    } else {
      // yuv422P: imageFormat.height = height * 2, Y占 height 行
      yHeight = imageFormat.height / 2;
      uvPhysRows = yHeight / 2;
    }
    uint8_t* uvStart = data + (size_t)rowPitch * yHeight;
    for (int32_t i = 0; i < uvPhysRows; ++i) {
      uint8_t* row = uvStart + (size_t)i * rowPitch;
      memmove(row + halfPitch, row + halfWidth, halfWidth);
    }
  }
}

// BGR/BGRA/RGB/RGBA/R8 → 单通道 r8 灰度; 亮度 0.299R+0.587G+0.114B
IImageBuffer* toGrayImage(IImageBuffer* inBuf) {
  if (!inBuf) return nullptr;
  ImageFormat fmt = inBuf->getImageFormat();
  if (!fmt.bVailid()) return nullptr;
  int32_t w = fmt.width;
  int32_t h = fmt.height;
  int32_t pxsz = getPixelSize(fmt.imageType);
  int32_t srcPitch = fmt.rowPitch ? fmt.rowPitch : w * pxsz;
  const uint8_t* src = inBuf->getPointer();
  if (!src) return nullptr;
  IImageBuffer* out = createImageBuffer();
  ImageFormat ofmt;
  ofmt.width = w;
  ofmt.height = h;
  ofmt.imageType = ImageType::r8;
  out->setImageFormat(ofmt);
  uint8_t* dst = out->getPointer();
  for (int32_t row = 0; row < h; ++row) {
    const uint8_t* rowp = src + (size_t)row * srcPitch;
    uint8_t* drow = dst + (size_t)row * w;
    for (int32_t col = 0; col < w; ++col) {
      const uint8_t* p = rowp + (size_t)col * pxsz;
      int32_t r, g, b;
      switch (fmt.imageType) {
        case ImageType::bgr8:
        case ImageType::bgra8:  b = p[0]; g = p[1]; r = p[2]; break;
        case ImageType::rgb8:
        case ImageType::rgba8:  r = p[0]; g = p[1]; b = p[2]; break;
        default:                r = g = b = p[0]; break;  // r8 及兜底
      }
      int32_t yv = (int32_t)(0.299f * r + 0.587f * g + 0.114f * b);
      drow[col] = (uint8_t)(yv < 0 ? 0 : (yv > 255 ? 255 : yv));
    }
  }
  return out;
}

// ROI 内 "像素所有参与通道 ∈ [cLo,cHi]" 的像素占比 (像素级掩码语义)
float countInRange(IImageBuffer* inBuf, int32_t x, int32_t y, int32_t w, int32_t h,
                   int32_t c0Lo, int32_t c1Lo, int32_t c2Lo,
                   int32_t c0Hi, int32_t c1Hi, int32_t c2Hi) {
  if (!inBuf) return 0.0f;
  ImageFormat fmt = inBuf->getImageFormat();
  if (!fmt.bVailid()) return 0.0f;
  int32_t iw = fmt.width;
  int32_t ih = fmt.height;
  // ROI 钳到图内
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= iw || y >= ih) return 0.0f;
  if (x + w > iw) w = iw - x;
  if (y + h > ih) h = ih - y;
  if (w <= 0 || h <= 0) return 0.0f;
  int32_t pxsz = getPixelSize(fmt.imageType);
  int32_t srcPitch = fmt.rowPitch ? fmt.rowPitch : iw * pxsz;
  const uint8_t* src = inBuf->getPointer();
  if (!src) return 0.0f;
  // 参与判定的通道数: r8=1, bgr8/rgb8=3, bgra8/rgba8=3(忽略 alpha); 兜底 1
  int32_t ch = 1;
  switch (fmt.imageType) {
    case ImageType::bgr8:
    case ImageType::rgb8:
    case ImageType::bgra8:
    case ImageType::rgba8: ch = 3; break;
    default:              ch = 1; break;
  }
  int32_t lo[3] = {c0Lo, c1Lo, c2Lo};
  int32_t hi[3] = {c0Hi, c1Hi, c2Hi};
  int32_t hit = 0;
  for (int32_t row = 0; row < h; ++row) {
    const uint8_t* rowp = src + (size_t)(y + row) * srcPitch + (size_t)x * pxsz;
    for (int32_t col = 0; col < w; ++col) {
      const uint8_t* p = rowp + (size_t)col * pxsz;
      bool ok = true;
      for (int32_t c = 0; c < ch && ok; ++c) {
        int32_t v = p[c];
        if (v < lo[c] || v > hi[c]) ok = false;
      }
      if (ok) ++hit;
    }
  }
  int32_t total = w * h;
  return total > 0 ? (float)hit / (float)total : 0.0f;
}

}
