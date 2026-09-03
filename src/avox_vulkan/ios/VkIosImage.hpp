#pragma once

#include "../VkContext.hpp"
// #include "avox_ios/MetalContext.hpp"

namespace avox {

// 需要让IRenderContext在前,不然后面转void*,再转vkiosimage指针会偏移
class VkIosImage : public IRenderContext, public VkContextRef {
 public:
  VkIosImage();
  virtual ~VkIosImage();

 private:
  VkImage vkImage = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  ImageFormat imageFormat = {};
  bool bCreate = false;
  IOSurfaceRef ioSurface = nullptr;

 public:
  virtual RenderType getRenderType() override { return RenderType::Metal; }

 public:
  inline VkImage getImage() { return vkImage; }
  inline IOSurfaceRef getIOSurface() { return ioSurface; }
  // metal->vulkan 输入传入IOSurfaceRef
  void setIOSurface(IOSurfaceRef ioSurface);
  // vulkan->metal 输出IOSurfaceRef
  void createIOSurface(const ImageFormat& imageFormat, int32_t rowPitch);

 public:
  void bindVK();
  void close();
  void logData();
};

}
