#pragma once

#include <android/hardware_buffer.h>

#include "AndCommon.hpp"

namespace avox {

bool supportSharedGpuBuffer();
bool getHardwareBuffer(AHardwareBuffer* buffer,AHardwareBuffer_Desc* desc);

// 定义函数指针类型
typedef int (*PFN_AHardwareBuffer_allocate)(const AHardwareBuffer_Desc*,
                                            AHardwareBuffer**);
typedef void (*PFN_AHardwareBuffer_release)(AHardwareBuffer*);
typedef void (*PFN_AHardwareBuffer_describe)(const AHardwareBuffer*, AHardwareBuffer_Desc*);

// https://android.googlesource.com/platform/cts/+/master/tests/tests/graphics/jni/VulkanTestHelpers.cpp
class SharedGpuBuffer {
 public:
  SharedGpuBuffer(/* args */);
  ~SharedGpuBuffer();

 protected:
  AHardwareBuffer* hardwareBuffer = nullptr;
  EGLImageKHR eglImage = nullptr;

  ImageFormat format = {};
  bool bSupport = false; 

 public:
  AHardwareBuffer* getHarderBuffer() { return hardwareBuffer; }
  const ImageFormat& getFormat() { return format; }

 private:
  void bindEGL();
  void close();

 public:
  void createAndroidBuffer(const ImageFormat& format);
  void bindGL(uint32_t textureId, uint32_t texType = GL_TEXTURE_2D);
  void renderGL(uint32_t textureId, uint32_t texType = GL_TEXTURE_2D);
  void release();

  void logData();

 protected:
  virtual void onInit() {};
  virtual void onRelease() {};
};

}