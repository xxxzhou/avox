#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../AvoxMath.h"
#include "../AvoxVideo.h"
#include "../module/LogHelper.hpp"
#include "ImageBuffer.hpp"
#include "VideoRender.hpp"

namespace avox {

int32_t getPixelSize(ImageType imageType) {
  switch (imageType) {
#define XX(name, value, channel, size, str) \
  case ImageType::name:                     \
    return size;
    AVOX_MAP_IMAGE(XX)
#undef XX
    default:
      return 0;
  }
}

int32_t getImageSize(const ImageFormat& imageFormat) {
  int32_t imageTypeSize = getPixelSize(imageFormat.imageType);
  int32_t rowPitch =
      std::max(imageFormat.rowPitch, imageFormat.width * imageTypeSize);
  return rowPitch * imageFormat.height;
}

const char* getImageTypeStr(ImageType imageType) {
  switch (imageType) {
#define XX(name, value, channel, size, str) \
  case ImageType::name:                     \
    return str;
    AVOX_MAP_IMAGE(XX)
#undef XX
    default:
      return "unknow";
  }
}

const char* getYuvTypeStr(YuvType yuvType) {
  switch (yuvType) {
#define XX(name, value, group, groupsize, str) \
  case YuvType::name:                          \
    return str;
    AVOX_MAP_YUV(XX)
#undef XX
    default:
      return "unknow";
  }
}
int32_t getYuvFrameSize(const YUVFormat& format, int32_t yStride) {
  int32_t rowPitch = std::max(yStride, format.width);
  switch (format.type) {
#define XX(name, value, group, groupsize, str) \
  case YuvType::name:                          \
    return rowPitch * format.height * groupsize / group;
    AVOX_MAP_YUV(XX)
#undef XX
    default:
      return 0;
  };
}

bool bVPlaneFormat(YuvType yuvType) {
  // NV12是半平面,YUV420的变种,UV是交叉
  if (yuvType == YuvType::yuv420P || yuvType == YuvType::yuv422P ||
      yuvType == YuvType::yuv444P || yuvType == YuvType::nv12 ||
      yuvType == YuvType::yuv420P10) {
    return true;
  }
  return false;
}

void yuv2ImageFormat(const YUVFrame& yuvFrame, ImageFormat& format) {
  YUVFormat yuvFormat = yuvFrame.format;
  format.width = yuvFormat.width;
  format.height = yuvFormat.height;
  int32_t group = 1;
  int32_t groupsize = 1;
  switch (yuvFormat.type) {
#define XX(name, value, group_, groupsize_, str) \
  case YuvType::name:                            \
    group = group_;                              \
    groupsize = groupsize_;                      \
    break;
    AVOX_MAP_YUV(XX)
#undef XX
  }
  // 如果是平面格式
  bool bPlane = bVPlaneFormat(yuvFormat.type);
  if (bPlane) {
    // 高度变高
    // yuv420P10使用r16,其他使用r8
    if (yuvFormat.type == YuvType::yuv420P10) {
      format.imageType = ImageType::r16;
      format.height = yuvFormat.height * 3 / 2;
    } else {
      format.imageType = ImageType::r8;
      format.height = yuvFormat.height * groupsize / group;
    }
  } else if (yuvFormat.type == YuvType::uyvy422_10B ||
             yuvFormat.type == YuvType::gray) {
    format.imageType = ImageType::r8;
    format.width = yuvFormat.width * groupsize / group;
  } else {
    // 如yuv2I/yvyuI/uyvyI,和rgba8类似,一个点四个字节，但是宽度少一半
    format.imageType = ImageType::rgba8;
    format.width = yuvFormat.width * group / groupsize;
  }
  int32_t yuvStride = yuvFrame.stride[0];
  int32_t rowPitch = format.width * getPixelSize(format.imageType);
  format.rowPitch = std::max(yuvStride, rowPitch);
}

void image2YUVFormat(const ImageFormat& imFormat, YuvType yuvType,
                     YUVFormat& format) {
  // yuv交叉用rgba,yuv平面用r8/r16,到imageformat长宽要变化
  int32_t pixelSize = getPixelSize(imFormat.imageType);
  format.width = imFormat.width;
  format.height = imFormat.height;
  format.type = yuvType;
  int32_t group = 1;
  int32_t groupsize = 1;
  switch (yuvType) {
#define XX(name, value, group_, groupsize_, str) \
  case YuvType::name:                            \
    group = group_;                              \
    groupsize = groupsize_;                      \
    break;
    AVOX_MAP_YUV(XX)
#undef XX
  }
  if (yuvType == YuvType::yuv420P10) {
    // yuv420P10: ImageFormat height = 1620, 需要转换回 YUVFormat height = 1080
    // height * 2/3 = 1620 * 2/3 = 1080
    format.height = imFormat.height * 2 / 3;
  } else if (bVPlaneFormat(yuvType)) {
    // 平面格式，长度变化
    format.height = imFormat.height * group / groupsize;
  } else {
    format.width = imFormat.width * pixelSize * group / groupsize;
  }
}

bool bTightlyPacked(const YUVFrame& frame) {
  // 1. 获取 Y 平面的基准步长
  int32_t yRowPitch = frame.stride[0];
  int32_t height = frame.format.height;
  // 3. 针对不同格式进行“步长比例”与“地址连续性”双重校验
  if (frame.format.type == YuvType::yuv420P ||
      frame.format.type == YuvType::yuv422P ||
      frame.format.type == YuvType::yuv444P ||
      frame.format.type == YuvType::yuv420P10) {
    // 获取比例：420P(w/2, h/2), 422P(w/2, h), 444P(w, h)
    int32_t uvWidthDiv = (frame.format.type == YuvType::yuv444P) ? 1 : 2;
    int32_t uvHeightDiv = (frame.format.type == YuvType::yuv420P ||
                           frame.format.type == YuvType::yuv420P10)
                              ? 2
                              : 1;
    // 步长比例必须严格匹配
    // 如果 yRowPitch 是 1122，那么 uvPitch 必须是 561。
    // 如果是 568（对齐了），则不属于“紧密排列”
    if (frame.stride[1] != yRowPitch / uvWidthDiv ||
        frame.stride[2] != yRowPitch / uvWidthDiv) {
      return false;
    }
    // 地址偏移必须严格等于 步长 * 高度
    uintptr_t ystart = (uintptr_t)frame.data[0];
    uintptr_t ustart = (uintptr_t)frame.data[1];
    uintptr_t vstart = (uintptr_t)frame.data[2];
    uint64_t ySize = (uint64_t)yRowPitch * height;
    uint64_t uSize = ySize / (uvWidthDiv * uvHeightDiv);
    return (ustart == ystart + ySize) && (vstart == ustart + uSize);
  } else if (frame.format.type == YuvType::nv12) {
    if (!frame.data[1]) return false;
    // NV12 的 UV 平面步长必须等于 Y 的步长
    if (frame.stride[1] != yRowPitch) return false;
    uintptr_t ystart = (uintptr_t)frame.data[0];
    uintptr_t uvstart = (uintptr_t)frame.data[1];
    return (uvstart == ystart + (uint64_t)yRowPitch * height);
  }
  return true;
}

void copyPlaneYUV2TightlyBuffer(const YUVFrame& frame, uint8_t* bfdata) {
  int32_t height = frame.format.height;
  int32_t yrowpitch = std::max(frame.format.width, frame.stride[0]);
  // yuv420P10每像素2字节,需要乘以像素大小
  if (frame.format.type == YuvType::yuv420P10) {
    yrowpitch = std::max(frame.format.width * 2, frame.stride[0]);
  }
  uint64_t y_logic_size = (uint64_t)yrowpitch * height;
  // 复制Y平面,直接以Y平面的rowpitch做的基准, Y是可以直接memcpy 的
  memcpy(bfdata, frame.data[0], y_logic_size);
  // 2. 复制 UV 平面
  if (frame.format.type == YuvType::yuv420P10) {
    int32_t uv_pitch = yrowpitch / 2;
    int32_t uv_height = height / 2;
    uint64_t uv_size_dst = (uint64_t)uv_pitch * uv_height;
    for (int p = 1; p <= 2; ++p) {
      uint8_t* dst = bfdata + y_logic_size + (p - 1) * uv_size_dst;
      if (frame.stride[p] == uv_pitch) {
        memcpy(dst, frame.data[p], uv_size_dst);
      } else {
        for (int h = 0; h < uv_height; ++h) {
          memcpy(dst + h * uv_pitch, frame.data[p] + h * frame.stride[p],
                 uv_pitch);
        }
      }
    }
  } else if (frame.format.type == YuvType::nv12) {
    // NV12 的 UV 平面物理大小正好是 Y 的一半
    uint8_t* src_uv =
        frame.data[1] ? frame.data[1] : (frame.data[0] + y_logic_size);
    memcpy(bfdata + y_logic_size, src_uv, y_logic_size / 2);
  } else if (frame.format.type == YuvType::yuv444P) {
    memcpy(bfdata + y_logic_size, frame.data[1], y_logic_size);
    memcpy(bfdata + y_logic_size + y_logic_size, frame.data[2], y_logic_size);
  } else if (frame.format.type == YuvType::yuv420P ||
             frame.format.type == YuvType::yuv422P) {
    int32_t halfWidth = frame.format.width / 2;
    int32_t uvHeight =
        frame.format.type == YuvType::yuv422P ? height : height / 2;
    uint64_t uvSize = (uint64_t)halfWidth * uvHeight;
    uint8_t* uvDst = bfdata + y_logic_size;
    if (halfWidth == frame.stride[1] || frame.stride[1] == 0) {
      // UV行无padding, 直接整块拷贝
      memcpy(uvDst, frame.data[1], uvSize);
      memcpy(uvDst + uvSize, frame.data[2], uvSize);
    } else {
      // 丢给GPU,padding重新组合
      // unpackGpuYUV的逆操作: U(pad)U(pad) → UU(pad)(pad)
      // 每物理行yrowpitch宽: 前halfWidth放偶数行, 后halfWidth放奇数行
      int32_t physRows = uvHeight / 2;
      for (int p = 0; p < 2; ++p) {
        const uint8_t* src = frame.data[p + 1];
        uint8_t* rdst = uvDst + (uint64_t)p * physRows * yrowpitch;
        int stride = frame.stride[p + 1];
        for (int i = 0; i < physRows; ++i) {
          memcpy(rdst + (uint64_t)i * yrowpitch,
                 src + (uint64_t)(2 * i) * stride, halfWidth);
          memcpy(rdst + (uint64_t)i * yrowpitch + halfWidth,
                 src + (uint64_t)(2 * i + 1) * stride, halfWidth);
        }
      }
    }
  }
}

bool image2YUVFrame(IImageBuffer* buffer, YUVFrame& yuvFrame, YuvType yuvType) {
  ImageFormat imageFormat = buffer->getImageFormat();
  image2YUVFormat(imageFormat, yuvType, yuvFrame.format);
  int32_t rowPitch = imageFormat.width * getPixelSize(imageFormat.imageType);
  if (imageFormat.rowPitch > rowPitch) {
    rowPitch = imageFormat.rowPitch;
  }
  yuvFrame.stride[0] = rowPitch;
  // 上面没处理rowPtich
  int32_t frameSize = getYuvFrameSize(yuvFrame.format, yuvFrame.stride[0]);
  // buffer的长度应该大于等于frameSize
  if (buffer->getBufferSize() < frameSize) {
    return false;
  }
  yuvFrame.data[0] = buffer->getPointer();
  int32_t ysize = rowPitch * yuvFrame.format.height;
  if (yuvType == YuvType::yuv420P10) {
    // yuv420P10: 每像素2字节, UV是Y的一半宽高
    int32_t uvPitch = rowPitch / 2;
    int32_t uvHeight = yuvFrame.format.height / 2;
    int32_t uvSize = uvPitch * uvHeight;
    yuvFrame.data[1] = yuvFrame.data[0] + ysize;
    yuvFrame.data[2] = yuvFrame.data[1] + uvSize;
    yuvFrame.stride[1] = uvPitch;
    yuvFrame.stride[2] = uvPitch;
  } else if (yuvType == YuvType::nv12) {
    yuvFrame.data[1] = yuvFrame.data[0] + ysize;
    yuvFrame.stride[1] = rowPitch;
  } else if (yuvType == YuvType::yuv420P || yuvType == YuvType::yuv422P) {
    int32_t uvPitch = rowPitch / 2;
    int32_t uvHeightDiv = (yuvType == YuvType::yuv420P) ? 2 : 1;
    int32_t uvHeight = yuvFrame.format.height / uvHeightDiv;
    int32_t halfUvSize = uvPitch * uvHeight;
    yuvFrame.data[1] = yuvFrame.data[0] + ysize;
    yuvFrame.stride[1] = uvPitch;
    yuvFrame.data[2] = yuvFrame.data[1] + halfUvSize;
    yuvFrame.stride[2] = uvPitch;
  }
  return true;
}

bool image2SplitYUVFrame(IImageBuffer* buffer, YuvType yuvType,
                         YUVFrame& yuvFrame, IImageBuffer* tmp) {
  ImageFormat imageFormat = buffer->getImageFormat();
  int32_t tightPitch = imageFormat.width * getPixelSize(imageFormat.imageType);
  // 仅420P/422P的packed(每物理行[偶|奇|pad])与split(等距行)在有padding时不重合
  bool bDiverge = (yuvType == YuvType::yuv420P || yuvType == YuvType::yuv422P) &&
                  imageFormat.rowPitch != tightPitch;
  if (bDiverge) {
    if (!tmp) {
      return false;
    }
    tmp->setImageFormat(imageFormat);
    tmp->copyFrom(buffer, true);
    unpackGpuYUV(tmp, yuvType);
    buffer = tmp;
  }
  return image2YUVFrame(buffer, yuvFrame, yuvType);
}

IImageBuffer* createImageBuffer() {
  ImageBuffer* buffer = new ImageBuffer();
  return buffer;
}

bool saveImageBufferBinary(const char* filePath, IImageBuffer* buffer) {
  try {
    std::ofstream fileStream(filePath, std::ios::binary | std::ios::out);
    // 写入buffer的信息
    ImageFormat format = buffer->getImageFormat();
    fileStream.write(reinterpret_cast<const char*>(&format), sizeof(format));
    int32_t bufferSize = buffer->getBufferSize();
    fileStream.write(reinterpret_cast<const char*>(&bufferSize),
                     sizeof(int32_t));
    // 写入buffer的图像数据
    fileStream.write(reinterpret_cast<const char*>(buffer->getPointer()),
                     bufferSize);
    fileStream.close();
    return true;
  } catch (const std::exception& ex) {
    log(LogLevel::warn, "could write path: ", filePath, " error:", ex.what());
  }
  return false;
}

bool loadImageBufferBinary(const char* filePath, IImageBuffer* buffer) {
  try {
    std::ifstream is(filePath, std::ios::binary | std::ios::in | std::ios::ate);
    if (is.is_open()) {
      int32_t lenght = is.tellg();
      is.seekg(0, std::ios::beg);
      ImageFormat format = {};
      is.read(reinterpret_cast<char*>(&format), sizeof(format));
      int32_t bufferSize = 0;
      is.read(reinterpret_cast<char*>(&bufferSize), sizeof(int32_t));
      // 设置信息
      buffer->setImageFormat(format);
      // 读取图像数据
      is.read(reinterpret_cast<char*>(buffer->getPointer()), bufferSize);
      is.close();
      return true;
    } else {
      log(LogLevel::warn, "could not open path: ", filePath);
      return false;
    }
  } catch (const std::exception& ex) {
    log(LogLevel::warn, "could load path: ", filePath, " error:", ex.what());
    return false;
  }
}

vec4i getViewRect(int swidth, int sheight, float aspect) {
  vec4i vp = {};
  float windowAspect = static_cast<float>(swidth) / sheight;
  if (aspect > windowAspect) {
    // 视频更宽：宽度撑满窗口，上下留黑边 (Letterboxing)
    vp.z = swidth;
    vp.w = static_cast<int>(swidth / aspect);
  } else {
    vp.z = static_cast<int>(sheight * aspect);
    // 视频更高：高度撑满窗口，左右留黑边 (Pillarboxing)
    vp.w = sheight;
  }
  // 居中计算
  vp.x = (swidth - vp.z) / 2;
  vp.y = (sheight - vp.w) / 2;
  return vp;
}

vec4i getTextureRect(int texWidth, int texHeight, float aspect) {
  vec4i rect = {};
  if (aspect == 0 || texWidth == 0) {
    rect.x = 0;
    rect.y = 0;
    rect.z = texWidth;
    rect.w = texHeight;
    return rect;
  }
  float texAspect = static_cast<float>(texWidth) / texHeight;
  if (texAspect > aspect) {
    rect.w = texHeight;
    rect.z = static_cast<int>(texHeight * aspect);
  } else {
    rect.z = texWidth;
    rect.w = static_cast<int>(texWidth / aspect);
  }
  // Center the rectangle
  rect.x = (texWidth - rect.z) / 2;
  rect.y = (texHeight - rect.w) / 2;
  return rect;
}

}