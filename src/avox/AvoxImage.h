#pragma once

#include "AvoxDef.h"  // AVOX_CV_* 深度/通道宏 + 基础类型

namespace avox {

// r16(深度)bgra8(游戏中fbo,rtt使用)rgba16(HDR)
// rgba32(CS中计算)rgb8(不带透明通道)rgb8P(cnn使用)
// rgb8P/bgr8P有三个平面
// name,value,channel,pixel size,str
#define AVOX_MAP_IMAGE(XX)           \
  XX(r8, 0, 1, 1, "r8")             \
  XX(r16, 1, 1, 2, "r16")           \
  XX(r32, 2, 1, 4, "r32")           \
  XX(rg16f, 3, 2, 4, "rg16f")       \
  XX(rgba8, 4, 4, 4, "rgba8")       \
  XX(bgra8, 5, 4, 4, "bgra8")       \
  XX(argb8, 6, 4, 4, "argb8")       \
  XX(rgba16, 7, 4, 8, "rgba16")     \
  XX(rgba32, 8, 4, 16, "rgba32")    \
  XX(r32f, 9, 1, 4, "r32f")         \
  XX(rgba32f, 10, 4, 16, "rgba32f") \
  XX(rgb8, 11, 3, 3, "rgb8")        \
  XX(bgr8, 12, 3, 3, "bgr8")        \
  XX(rgb8P, 13, 1, 1, "rgb8P")      \
  XX(bgr8P, 14, 1, 1, "bgr8P")     \
  XX(rgba16f, 15, 4, 8, "rgba16f")

enum class ImageType : int32_t {
  other = -1,
#define XX(name, value, channel, size, str) name = value,
  AVOX_MAP_IMAGE(XX)
#undef XX
};

// 图像类型,png/jpg/bmp/tga
enum class IEncodeType : int32_t { other = -1, jpg, png, bmp, tga };

struct IEncodeConfig {
  IEncodeType encodeType = IEncodeType::jpg;
  int32_t quality = 85;
};

struct ImageFormat {
  int32_t width = 0;
  int32_t height = 0;
  // 如果rowPitch为0,默认等于图像宽*pixelsize
  int32_t rowPitch = 0;
  ImageType imageType = ImageType::other;

  inline bool operator==(const ImageFormat& right) const {
    return width == right.width && height == right.height &&
           imageType == right.imageType && rowPitch == right.rowPitch;
  }
  inline bool operator!=(const ImageFormat& right) const {
    return !(*this == right);
  }

  bool bVailid() const {
    return width > 0 && height > 0 && imageType != ImageType::other;
  }
};

// 图像,非YUV可能多Plan组合的那种
class IImageBuffer {
 public:
  IImageBuffer(/* args */) = default;
  virtual ~IImageBuffer() {}

 public:
  virtual void setImageFormat(const ImageFormat& imageFormat) = 0;

  virtual int32_t getBufferSize() = 0;
  virtual uint8_t* getPointer() = 0;
  virtual ImageFormat getImageFormat() = 0;
  // 数据是否引用外部数据
  virtual bool bDataRef() = 0;

  // bCopyData为true,复制数据，否则只复制dataptr,size,imageformat
  virtual void copyFrom(IImageBuffer* src, bool bCopyData) = 0;
  virtual void copyTo(IImageBuffer* dest, bool bCopyData) = 0;
  virtual void clear() = 0;
};

extern "C" {
// ImageType像素大小
AVOX_EXPORT int32_t getPixelSize(ImageType imageType);
// 计算图像大小
AVOX_EXPORT int32_t getImageSize(const ImageFormat& imageFormat);
AVOX_EXPORT const char* getImageTypeStr(ImageType imageType);
AVOX_EXPORT IImageBuffer* createImageBuffer();
AVOX_EXPORT bool saveImageBufferBinary(const char* filePath,
                                      IImageBuffer* buffer);
AVOX_EXPORT bool loadImageBufferBinary(const char* filePath,
                                      IImageBuffer* buffer);
// 加载 assets/images 下图像 (文件名，会自动拼接路径)
// 支持 BMP/PNG/JPG/TGA 等格式
AVOX_EXPORT bool loadImageAsset(const char* filename, IImageBuffer* buffer);
// 加载图像 (完整路径，不拼接路径)
// 支持 BMP/PNG/JPG/TGA 等格式
AVOX_EXPORT bool loadImagePath(const char* filePath, IImageBuffer* buffer);
// 保存图像 (完整路径，根据后缀自动选择格式: .png/.jpg/.bmp/.tga，默认PNG)
AVOX_EXPORT bool saveImagePath(const char* filePath, IImageBuffer* buffer);
// 改变图像大小,不要在tick中使用,tick使用ISurfaceRender的GPU图像处理改变大小
AVOX_EXPORT bool resizeImage(IImageBuffer* inBuf, IImageBuffer* outBuf,
                            int32_t width, int32_t height);
// 从inBuf的(x,y)位置裁剪width*height区域到outBuf,不要在tick中使用
AVOX_EXPORT bool cropImage(IImageBuffer* inBuf, IImageBuffer* outBuf,
                          int32_t x, int32_t y, int32_t width, int32_t height);
// BGR/BGRA/RGB/RGBA → 单通道 r8 灰度 (0.299R+0.587G+0.114B), 返回新 buffer (失败 nullptr)
AVOX_EXPORT IImageBuffer* toGrayImage(IImageBuffer* inBuf);
// ROI(x,y,w,h) 内 "像素所有通道 ∈ [cLo,cHi]" 的像素占比 [0,1] (像素级掩码语义, 同 cv::inRange)
// 通道序跟随 buffer 原生格式: bgr8/bgra8=BGR, rgb8/rgba8=RGB, r8 仅用 c0 (c1/c2 忽略); bgra/rgba 忽略 alpha
// 整图请传 (0,0,width,height)。lo/hi 越界像素不计; 空 ROI 返回 0
AVOX_EXPORT float countInRange(IImageBuffer* inBuf, int32_t x, int32_t y,
                              int32_t w, int32_t h, int32_t c0Lo, int32_t c1Lo,
                              int32_t c2Lo, int32_t c0Hi, int32_t c1Hi,
                              int32_t c2Hi);
// 根据config设置,得到base64字符串.
// 内部 static 字符串缓冲,每次调用覆盖,线程安全
// C++调用要用std::string赋值深拷贝,不要保存const char*指针
// swig转别的语言会自动深拷贝到对应语言的字符串类型中,不用管
AVOX_EXPORT const char* getImageBase64(IImageBuffer* buffer,
                                      const IEncodeConfig& config);
}

}