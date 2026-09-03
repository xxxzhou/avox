#pragma once

#include <vector>

#include "../AvoxVideo.h"

namespace avox {

// @brief 提供一个用于保存在内存中的图像数据
class AVOX_EXPORT ImageBuffer : public IImageBuffer {
 public:
  ImageBuffer(/* args */);
  virtual ~ImageBuffer();

 protected:
  std::vector<uint8_t> buffer;
  // 图像数据,可能引用外部数据，也可能是内部buff
  uint8_t* data = nullptr;
  // size应该和imageFormat是绑定的
  int32_t size = 0;
  ImageFormat imageFormat = {};
  // 数据是否引用外部数据
  bool bDataReference = false;

 protected:
  void changeBufferSize();

 public:
  virtual void setImageFormat(const ImageFormat& imageFormat) override;

  virtual int32_t getBufferSize() override;
  virtual uint8_t* getPointer() override;
  virtual ImageFormat getImageFormat() override;
  virtual bool bDataRef() override;

  virtual void copyFrom(IImageBuffer* buffer, bool bCopyData) override;
  virtual void copyTo(IImageBuffer* buffer, bool bCopyData) override;
  virtual void clear() override;

 public:
  void setData(uint8_t* data, const ImageFormat& format, bool bCopy);
};

// 下面方法只是给electron+软解非vulkan截图使用,请不要每帧调用

// YUV420P → RGBA 转换
void yuv420p2Rgba(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                  int32_t width, int32_t height,
                  int32_t yStride, int32_t uvStride,
                  uint8_t* rgba, int32_t rgbaStride);

// NV12 → RGBA 转换
void nv122Rgba(const uint8_t* y, const uint8_t* uv,
               int32_t width, int32_t height,
               int32_t yStride, int32_t uvStride,
               uint8_t* rgba, int32_t rgbaStride);

}