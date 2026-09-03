#include "VkIosImage.hpp"
#include <IOSurface/IOSurfaceRef.h>

#import <MetalKit/MetalKit.h>

namespace avox {

VkIosImage::VkIosImage() {}

VkIosImage::~VkIosImage() { close(); }

void VkIosImage::setIOSurface(IOSurfaceRef ioSurface_) {
  if (ioSurface == ioSurface_) {
    return;
  }
  ioSurface = ioSurface_;
  LOGFLF(LogLevel::info, "set io surface:", ioSurface, " vkiosimage:", this);
  bCreate = false;
}

void VkIosImage::createIOSurface(const ImageFormat &imageFormat,
                                 int32_t rowPitch) {
  int32_t pixelSize = getPixelSize(imageFormat.imageType);
  if (rowPitch < pixelSize * imageFormat.width) {
    rowPitch = pixelSize * imageFormat.width;
  }
  // 后面把ioSurface转CVPixelBuffer,但是格式只支持BGRA,不支持RGBA
  // CVPixelBufferCreateWithIOSurface
  OSType pixelType = kCVPixelFormatType_32BGRA;
  switch (imageFormat.imageType) {
  case ImageType::rgba8:
    pixelType = kCVPixelFormatType_32BGRA;
    break;
  case ImageType::bgra8:
    pixelType = kCVPixelFormatType_32BGRA;
    break;
  case ImageType::r8:
    pixelType = kCVPixelFormatType_OneComponent8;
    break;
  default:
  pixelType:
    kCVPixelFormatType_32BGRA;
    break;
  }
  NSDictionary *surfaceProps = @{
    (id)kIOSurfaceWidth : @(imageFormat.width),
    (id)kIOSurfaceHeight : @(imageFormat.height),
    (id)kIOSurfacePixelFormat : @(pixelType),
    (id)kIOSurfaceBytesPerElement : @(pixelSize),
    (id)kIOSurfaceBytesPerRow : @(rowPitch)
  };
  ioSurface = IOSurfaceCreate((CFDictionaryRef)surfaceProps);
  bCreate = true;
  LOGFLF(LogLevel::info, "create io surface:", ioSurface, " vkiosimage:", this);
  bindVK();
}

// https://registry.khronos.org/vulkan/specs/latest/man/html/VkImportMetalIOSurfaceInfoEXT.html
void VkIosImage::bindVK() {
  if (!ioSurface) {
    LOGFLF(LogLevel::warn, "iosurface is null");
    return;
  }
  size_t surfaceWidth = IOSurfaceGetWidth(ioSurface);
  size_t surfaceHeight = IOSurfaceGetHeight(ioSurface);
  size_t bytesPerRow = IOSurfaceGetBytesPerRow(ioSurface);
  OSType pixelFormat = IOSurfaceGetPixelFormat(ioSurface);
  LOGFLF(LogLevel::info, "surface width:", surfaceWidth,
         " surface height:", surfaceHeight, " bytesPerRow:", bytesPerRow,
         " pixelFormat:", pixelFormat);
  if (surfaceWidth <= 0 || surfaceHeight <= 0) {
    return;
  }
  VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
  switch (pixelFormat) {
  case kCVPixelFormatType_32BGRA:
    format = VK_FORMAT_B8G8R8A8_UNORM;
    break;
  case kCVPixelFormatType_32RGBA:
    format = VK_FORMAT_R8G8B8A8_UNORM;
    break;
  case kCVPixelFormatType_OneComponent8:
    format = VK_FORMAT_R8_UNORM;
    break;
  default:
    format = VK_FORMAT_R8G8B8A8_UNORM;
    break;
  }
  // 将 IOSurfaceRef 映射到 VkImage
  VkImportMetalIOSurfaceInfoEXT importInfo = {};
  importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_METAL_IO_SURFACE_INFO_EXT;
  importInfo.ioSurface = ioSurface;
  importInfo.pNext = nullptr;

  VkImageCreateInfo imageCreateInfo = {};
  imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCreateInfo.pNext = &importInfo;
  imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
  imageCreateInfo.format = format;
  imageCreateInfo.extent.width = surfaceWidth;
  imageCreateInfo.extent.height = surfaceHeight;
  imageCreateInfo.extent.depth = 1;
  imageCreateInfo.mipLevels = 1;
  imageCreateInfo.arrayLayers = 1;
  imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageCreateInfo.usage =
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkResult result =
      vkCreateImage(vkDevice, &imageCreateInfo, nullptr, &vkImage);
  if (result != VK_SUCCESS) {
    LOGFLF(LogLevel::warn, "failed to create image");
    return;
  }
  // 分配并绑定设备内存
  VkMemoryRequirements memoryRequirements = {};
  vkGetImageMemoryRequirements(vkDevice, vkImage, &memoryRequirements);
  uint32_t memoryTypeIndex = 0;
  bool getIndex = wphyDevcie->getMemoryTypeIndex(
      memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      memoryTypeIndex);
  if (!getIndex) {
    LOGFLF(LogLevel::warn, "failed to get memory type index");
    return;
  }
  VkMemoryAllocateInfo memoryAllocateInfo = {};
  memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memoryAllocateInfo.allocationSize = memoryRequirements.size;
  memoryAllocateInfo.memoryTypeIndex = memoryTypeIndex;
  vkAllocateMemory(vkDevice, &memoryAllocateInfo, nullptr, &memory);
  // 绑定
  vkBindImageMemory(vkDevice, vkImage, memory, 0);
}

void VkIosImage::close() {
  if (vkImage) {
    vkDestroyImage(vkDevice, vkImage, nullptr);
    vkImage = VK_NULL_HANDLE;
  }
  if (memory) {
    vkFreeMemory(vkDevice, memory, nullptr);
    memory = VK_NULL_HANDLE;
  }
  if (bCreate) {
    CFRelease(ioSurface);
    ioSurface = nil;
  }
}

void VkIosImage::logData() {
  if (ioSurface) {
    // 验证ioSurface是否成功写入数据,读取ioSurface里数据
    IOSurfaceLock(ioSurface, kIOSurfaceLockReadOnly, nil);
    void *baseAddress = IOSurfaceGetBaseAddress(ioSurface);
    uint8_t *data = (uint8_t *)baseAddress + 21300;
    AvoxData avoxData = {};
    avoxData.data = data;
    avoxData.size = 100;
    log(LogLevel::info, "ioSurface data:", avoxData);
    IOSurfaceUnlock(ioSurface, kIOSurfaceLockReadOnly, nil);
  }
}

}
