#pragma once

#include <map>

#include "VkCommon.hpp"
#include "VkExport.h"
#include "avox/AvoxMath.h"

#ifdef __ANDROID__
#include <android/asset_manager.h>
#endif

namespace avox {

std::string errorString(VkResult errorCode);

#define AVOX_VULKAN_LOG(ret, ...)                                               \
  do {                                                                         \
    VkResult vkret = (ret);                                                    \
    if (vkret != VK_SUCCESS) {                                                 \
      LOGFLF(LogLevel::warn, __VA_ARGS__, " vk error: ", errorString(vkret));  \
    }                                                                          \
  } while (0)

// ret需要是VkResult,非返回VkResult的函数
#define AVOX_VULKAN_LOG_RETURN(ret, result, ...)                                \
  do {                                                                         \
    VkResult vkret = (ret);                                                    \
    if (vkret != VK_SUCCESS) {                                                 \
      LOGFLF(LogLevel::warn, __VA_ARGS__, " vk error: ", errorString(vkret));  \
      return result;                                                           \
    }                                                                          \
  } while (0)

#define AVOX_VULKAN_LOG_RETURN_FALSE(ret, ...)                                  \
  AVOX_VULKAN_LOG_RETURN(ret, false, __VA_ARGS__)

#define AVOX_VK_INST_FUNC_IMPL(func) PFN_vk##func func = nullptr;

#define AVOX_VK_INST_FUNC_INIT(instance, func)                                  \
  func = (PFN_vk##func)vkGetInstanceProcAddr(instance, AVOX_TSTR(vk##func));    \
  if (!func) {                                                                 \
    LOGFLF(LogLevel::warn, "fun:", AVOX_TSTR(vk##func), "not get");             \
  }

#define AVOX_AVOX_VK_INST_FUNC_IMPL_INIT(instance, func)                         \
  PFN_vk##func func =                                                          \
      (PFN_vk##func)vkGetInstanceProcAddr(instance, AVOX_TSTR(vk##func));       \
  if (!func) {                                                                 \
    LOGFLF(LogLevel::warn, "fun:", AVOX_TSTR(vk##func), "not get");             \
  }

struct VkImageParam {
  int32_t width = 0;
  int32_t height = 0;
  int32_t bitDepth = 0;
  VkFormat format = VK_FORMAT_UNDEFINED;
  // 默认用于CS,storage
  VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT;
  // 默认用于device local
  VkMemoryPropertyFlags memoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
};

// VkFormat对应每像素大小
int32_t vkPixelSize(VkFormat format);

// flags是否包含flag
bool havaFlags(uint32_t flags, uint32_t flag);

#if AVOX_ENABLE_VULKAN_DECODE
VkVideoCodecOperationFlagBitsKHR getVkCodec(VCodecId type);

VkVideoChromaSubsamplingFlagBitsKHR getVkYUVType(YuvType type);

VkVideoComponentBitDepthFlagsKHR getVkBitDepth(int32_t bitDepth);
#endif

std::string physicalDeviceTypeString(VkPhysicalDeviceType type);

void DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks *pAllocator);

VkResult createVkDebug(VkInstance instance,
                       VkDebugUtilsMessengerEXT &vkDebugExt);
// 获取实例的物理显卡
VkResult enumerateDevice(VkInstance instance,
                         std::vector<VKPhysDevWrapper> &physicalDevices);

VkFormat getVkFormat(YuvType type, int32_t bitDepth = 8);

VkFormat getVkFormat(ImageType type);

VkShaderModule loadShader(const char *fileName, VkDevice device);
#ifdef __ANDROID__
VkShaderModule loadShader(AAssetManager *assetManager, const char *fileName,
                          VkDevice device);
#endif

void changeLayout(VkCommandBuffer command, VkImage image,
                  VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags oldStageFlags,
                  VkPipelineStageFlags newStageFlags,
                  VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  VkAccessFlags newAccessFlags = 0);

void changeLayout(VkCommandBuffer command, VkBuffer buffer,
                  VkPipelineStageFlags oldStageFlags,
                  VkPipelineStageFlags newStageFlags,
                  VkAccessFlags oldAccessFlags, VkAccessFlags newAccessFlags);

void createSampler(VkDevice device, bool bLinear, VkSampler &sampler);

void bufferToImage(VkCommandBuffer cmd, const class VkWrapBuffer *buffer,
                   const class VkTexture *texture, int32_t rowPitch = 0);
void imageToBuffer(VkCommandBuffer cmd, const class VkTexture *texture,
                   class VkWrapBuffer *buffer, int32_t rowPitch = 0);
// 如果类似1199的宽度,假设对齐宽度32,对齐后1216
// 如果是RGBA8,则rowPitch=1216*4=4864
// 这里pitchWidth表示对齐后的宽度是1216，非4864
void imageToBuffer(VkCommandBuffer cmd, VkImage texture, VkBuffer buffer,
                   int32_t width, int32_t height, int32_t pitchWidth = 0);
void blitFillImage(VkCommandBuffer cmd, const class VkTexture *src,
                   const class VkTexture *dest);
void blitFillImage(
    VkCommandBuffer cmd, const VkTexture *src, VkImage dest, int32_t destWidth,
    int32_t destHeight,
    VkImageLayout destLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
void blitFillImage(
    VkCommandBuffer cmd, const VkTexture *src, VkImage dest, vec4i rect,
    VkImageLayout destLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

void copyImage(VkCommandBuffer cmd, const class VkTexture *src, VkImage dest);
void copyImage(VkCommandBuffer cmd, VkImage src, VkImage dest, int32_t width,
               int32_t height);

}