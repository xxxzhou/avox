#include "VkAndImage.hpp"

// https://source.android.com/devices/graphics/implement-vulkan?hl=zh-cn
// vkQueueSignalReleaseImageANDROID

namespace avox {

VkAndImage::VkAndImage(/* args */) {}

VkAndImage::~VkAndImage() { onRelease(); }

void VkAndImage::onInit() {
  bindVK();
  LOGFLF(LogLevel::info, "VkAndImage::onInit vkImage:", vkImage,
         " memory:", memory);
}

void VkAndImage::onRelease() {
  if (vkImage) {
    vkDestroyImage(vkDevice, vkImage, nullptr);
    vkImage = VK_NULL_HANDLE;
  }
  if (memory) {
    vkFreeMemory(vkDevice, memory, nullptr);
    memory = VK_NULL_HANDLE;
  }
  LOGFLF(LogLevel::info, " completed");
}

// https://android.googlesource.com/platform/cts/+/master/tests/tests/graphics/jni/VulkanTestHelpers.cpp
void VkAndImage::bindVK() {
  AHardwareBuffer_Desc bufferDesc = {};
  bool bGet = getHardwareBuffer(hardwareBuffer, &bufferDesc);
  if (!bGet) {
    bufferDesc.width = format.width;
    bufferDesc.height = format.height;
    bufferDesc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    // AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
    bufferDesc.usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER |
                       AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
    LOGFLF(LogLevel::info, "get hardware buffer failed");
  }
  VkAndroidHardwareBufferFormatPropertiesANDROID formatInfo = {
      .sType =
          VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
      .pNext = nullptr,
  };
  VkAndroidHardwareBufferPropertiesANDROID properties = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
      .pNext = &formatInfo,
  };
  LOGFLF(LogLevel::info, "hardwareBuffer:", hardwareBuffer,
         " vkGetAndroidHardwareBufferPropertiesANDROID:",
         vkGetAndroidHardwareBufferPropertiesANDROID);
  vkGetAndroidHardwareBufferPropertiesANDROID(vkDevice, hardwareBuffer,
                                              &properties);
  VkExternalFormatANDROID externalFormat{
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
      .pNext = nullptr,
      .externalFormat = formatInfo.externalFormat,
  };
  VkExternalMemoryImageCreateInfo externalCreateInfo{
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .pNext = nullptr,
      .handleTypes =
          VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
  };
  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.pNext = &externalCreateInfo;
  imageInfo.flags = 0u;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = formatInfo.format;
  imageInfo.extent = {
      bufferDesc.width,
      bufferDesc.height,
      1u,
  };
  imageInfo.mipLevels = 1u, imageInfo.arrayLayers = 1u;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.queueFamilyIndexCount = 0;
  imageInfo.pQueueFamilyIndices = nullptr;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  AVOX_VULKAN_LOG(vkCreateImage(vkDevice, &imageInfo, nullptr, &vkImage),
                 "create image failed");

  VkImportAndroidHardwareBufferInfoANDROID androidHardwareBufferInfo{
      .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
      .pNext = nullptr,
      .buffer = hardwareBuffer,
  };
  VkMemoryDedicatedAllocateInfo memoryAllocateInfo{
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = &androidHardwareBufferInfo,
      .image = vkImage,
      .buffer = VK_NULL_HANDLE,
  };
  // android的hardbuffer位置(properties)
  VkMemoryRequirements vrequires = {};
  vkGetImageMemoryRequirements(vkDevice, vkImage, &vrequires);
  uint32_t memoryTypeIndex = 0;
  bool getIndex = wphyDevcie->getMemoryTypeIndex(properties.memoryTypeBits, 0,
                                                 memoryTypeIndex);
  assert(getIndex);
  VkMemoryAllocateInfo memoryInfo = {};
  memoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memoryInfo.pNext = &memoryAllocateInfo;
  memoryInfo.memoryTypeIndex = memoryTypeIndex;
  memoryInfo.allocationSize = properties.allocationSize;
  AVOX_VULKAN_LOG(vkAllocateMemory(vkDevice, &memoryInfo, nullptr, &memory),
                 "allocate memory failed");

  VkBindImageMemoryInfo bindImageInfo = {};
  bindImageInfo.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
  bindImageInfo.pNext = nullptr;
  bindImageInfo.image = vkImage;
  bindImageInfo.memory = memory;
  bindImageInfo.memoryOffset = 0;
  AVOX_VULKAN_LOG(vkBindImageMemory2KHR(vkDevice, 1, &bindImageInfo),
                 "bind image memory failed");
}

}