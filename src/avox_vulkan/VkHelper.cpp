#include "VkHelper.hpp"

#include <fstream>

#include "VkContext.hpp"
#include "vulkan/VkTexture.hpp"
#include "vulkan/VkWrapBuffer.hpp"

namespace avox {
#if AVOX_ENABLE_VULKAN_DECODE
VkVideoCodecOperationFlagBitsKHR getVkCodec(VCodecId type) {
  switch (type) {
    case VCodecId::h264:
      return VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
    case VCodecId::h265:
      return VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR;
    default:
      return VK_VIDEO_CODEC_OPERATION_NONE_KHR;
  }
  return VK_VIDEO_CODEC_OPERATION_NONE_KHR;
}

VkVideoChromaSubsamplingFlagBitsKHR getVkYUVType(YuvType type) {
  switch (type) {
    case YuvType::gray:
      return VK_VIDEO_CHROMA_SUBSAMPLING_MONOCHROME_BIT_KHR;
    case YuvType::yuv420P:
      return VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    case YuvType::yuv422P:
      return VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR;
    case YuvType::yuv444P:
      return VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR;
    default:
      return VK_VIDEO_CHROMA_SUBSAMPLING_INVALID_KHR;
  }
}

VkVideoComponentBitDepthFlagsKHR getVkBitDepth(int32_t bitDepth) {
  switch (bitDepth) {
    case 8:
      return VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    case 10:
      return VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR;
    case 12:
      return VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR;
    default:
      return VK_VIDEO_COMPONENT_BIT_DEPTH_INVALID_KHR;
  }
  return VK_VIDEO_COMPONENT_BIT_DEPTH_INVALID_KHR;
}
#endif

const std::map<VkFormat, FormatInfo> formatTable = {
    {VK_FORMAT_UNDEFINED, {0, 0}},
    {VK_FORMAT_R4G4_UNORM_PACK8, {1, 2}},
    {VK_FORMAT_R4G4B4A4_UNORM_PACK16, {2, 4}},
    {VK_FORMAT_B4G4R4A4_UNORM_PACK16, {2, 4}},
    {VK_FORMAT_R5G6B5_UNORM_PACK16, {2, 3}},
    {VK_FORMAT_B5G6R5_UNORM_PACK16, {2, 3}},
    {VK_FORMAT_R5G5B5A1_UNORM_PACK16, {2, 4}},
    {VK_FORMAT_B5G5R5A1_UNORM_PACK16, {2, 4}},
    {VK_FORMAT_A1R5G5B5_UNORM_PACK16, {2, 4}},
    {VK_FORMAT_R8_UNORM, {1, 1}},
    {VK_FORMAT_R8_SNORM, {1, 1}},
    {VK_FORMAT_R8_USCALED, {1, 1}},
    {VK_FORMAT_R8_SSCALED, {1, 1}},
    {VK_FORMAT_R8_UINT, {1, 1}},
    {VK_FORMAT_R8_SINT, {1, 1}},
    {VK_FORMAT_R8_SRGB, {1, 1}},
    {VK_FORMAT_R8G8_UNORM, {2, 2}},
    {VK_FORMAT_R8G8_SNORM, {2, 2}},
    {VK_FORMAT_R8G8_USCALED, {2, 2}},
    {VK_FORMAT_R8G8_SSCALED, {2, 2}},
    {VK_FORMAT_R8G8_UINT, {2, 2}},
    {VK_FORMAT_R8G8_SINT, {2, 2}},
    {VK_FORMAT_R8G8_SRGB, {2, 2}},
    {VK_FORMAT_R8G8B8_UNORM, {3, 3}},
    {VK_FORMAT_R8G8B8_SNORM, {3, 3}},
    {VK_FORMAT_R8G8B8_USCALED, {3, 3}},
    {VK_FORMAT_R8G8B8_SSCALED, {3, 3}},
    {VK_FORMAT_R8G8B8_UINT, {3, 3}},
    {VK_FORMAT_R8G8B8_SINT, {3, 3}},
    {VK_FORMAT_R8G8B8_SRGB, {3, 3}},
    {VK_FORMAT_B8G8R8_UNORM, {3, 3}},
    {VK_FORMAT_B8G8R8_SNORM, {3, 3}},
    {VK_FORMAT_B8G8R8_USCALED, {3, 3}},
    {VK_FORMAT_B8G8R8_SSCALED, {3, 3}},
    {VK_FORMAT_B8G8R8_UINT, {3, 3}},
    {VK_FORMAT_B8G8R8_SINT, {3, 3}},
    {VK_FORMAT_B8G8R8_SRGB, {3, 3}},
    {VK_FORMAT_R8G8B8A8_UNORM, {4, 4}},
    {VK_FORMAT_R8G8B8A8_SNORM, {4, 4}},
    {VK_FORMAT_R8G8B8A8_USCALED, {4, 4}},
    {VK_FORMAT_R8G8B8A8_SSCALED, {4, 4}},
    {VK_FORMAT_R8G8B8A8_UINT, {4, 4}},
    {VK_FORMAT_R8G8B8A8_SINT, {4, 4}},
    {VK_FORMAT_R8G8B8A8_SRGB, {4, 4}},
    {VK_FORMAT_B8G8R8A8_UNORM, {4, 4}},
    {VK_FORMAT_B8G8R8A8_SNORM, {4, 4}},
    {VK_FORMAT_B8G8R8A8_USCALED, {4, 4}},
    {VK_FORMAT_B8G8R8A8_SSCALED, {4, 4}},
    {VK_FORMAT_B8G8R8A8_UINT, {4, 4}},
    {VK_FORMAT_B8G8R8A8_SINT, {4, 4}},
    {VK_FORMAT_B8G8R8A8_SRGB, {4, 4}},
    {VK_FORMAT_A8B8G8R8_UNORM_PACK32, {4, 4}},
    {VK_FORMAT_A8B8G8R8_SNORM_PACK32, {4, 4}},
    {VK_FORMAT_A8B8G8R8_USCALED_PACK32, {4, 4}},
    {VK_FORMAT_A8B8G8R8_SSCALED_PACK32, {4, 4}},
    {VK_FORMAT_A8B8G8R8_UINT_PACK32, {4, 4}},
    {VK_FORMAT_A8B8G8R8_SINT_PACK32, {4, 4}},
    {VK_FORMAT_A8B8G8R8_SRGB_PACK32, {4, 4}},
    {VK_FORMAT_A2R10G10B10_UNORM_PACK32, {4, 4}},
    {VK_FORMAT_A2R10G10B10_SNORM_PACK32, {4, 4}},
    {VK_FORMAT_A2R10G10B10_USCALED_PACK32, {4, 4}},
    {VK_FORMAT_A2R10G10B10_SSCALED_PACK32, {4, 4}},
    {VK_FORMAT_A2R10G10B10_UINT_PACK32, {4, 4}},
    {VK_FORMAT_A2R10G10B10_SINT_PACK32, {4, 4}},
    {VK_FORMAT_A2B10G10R10_UNORM_PACK32, {4, 4}},
    {VK_FORMAT_A2B10G10R10_SNORM_PACK32, {4, 4}},
    {VK_FORMAT_A2B10G10R10_USCALED_PACK32, {4, 4}},
    {VK_FORMAT_A2B10G10R10_SSCALED_PACK32, {4, 4}},
    {VK_FORMAT_A2B10G10R10_UINT_PACK32, {4, 4}},
    {VK_FORMAT_A2B10G10R10_SINT_PACK32, {4, 4}},
    {VK_FORMAT_R16_UNORM, {2, 1}},
    {VK_FORMAT_R16_SNORM, {2, 1}},
    {VK_FORMAT_R16_USCALED, {2, 1}},
    {VK_FORMAT_R16_SSCALED, {2, 1}},
    {VK_FORMAT_R16_UINT, {2, 1}},
    {VK_FORMAT_R16_SINT, {2, 1}},
    {VK_FORMAT_R16_SFLOAT, {2, 1}},
    {VK_FORMAT_R16G16_UNORM, {4, 2}},
    {VK_FORMAT_R16G16_SNORM, {4, 2}},
    {VK_FORMAT_R16G16_USCALED, {4, 2}},
    {VK_FORMAT_R16G16_SSCALED, {4, 2}},
    {VK_FORMAT_R16G16_UINT, {4, 2}},
    {VK_FORMAT_R16G16_SINT, {4, 2}},
    {VK_FORMAT_R16G16_SFLOAT, {4, 2}},
    {VK_FORMAT_R16G16B16_UNORM, {6, 3}},
    {VK_FORMAT_R16G16B16_SNORM, {6, 3}},
    {VK_FORMAT_R16G16B16_USCALED, {6, 3}},
    {VK_FORMAT_R16G16B16_SSCALED, {6, 3}},
    {VK_FORMAT_R16G16B16_UINT, {6, 3}},
    {VK_FORMAT_R16G16B16_SINT, {6, 3}},
    {VK_FORMAT_R16G16B16_SFLOAT, {6, 3}},
    {VK_FORMAT_R16G16B16A16_UNORM, {8, 4}},
    {VK_FORMAT_R16G16B16A16_SNORM, {8, 4}},
    {VK_FORMAT_R16G16B16A16_USCALED, {8, 4}},
    {VK_FORMAT_R16G16B16A16_SSCALED, {8, 4}},
    {VK_FORMAT_R16G16B16A16_UINT, {8, 4}},
    {VK_FORMAT_R16G16B16A16_SINT, {8, 4}},
    {VK_FORMAT_R16G16B16A16_SFLOAT, {8, 4}},
    {VK_FORMAT_R32_UINT, {4, 1}},
    {VK_FORMAT_R32_SINT, {4, 1}},
    {VK_FORMAT_R32_SFLOAT, {4, 1}},
    {VK_FORMAT_R32G32_UINT, {8, 2}},
    {VK_FORMAT_R32G32_SINT, {8, 2}},
    {VK_FORMAT_R32G32_SFLOAT, {8, 2}},
    {VK_FORMAT_R32G32B32_UINT, {12, 3}},
    {VK_FORMAT_R32G32B32_SINT, {12, 3}},
    {VK_FORMAT_R32G32B32_SFLOAT, {12, 3}},
    {VK_FORMAT_R32G32B32A32_UINT, {16, 4}},
    {VK_FORMAT_R32G32B32A32_SINT, {16, 4}},
    {VK_FORMAT_R32G32B32A32_SFLOAT, {16, 4}},
    {VK_FORMAT_R64_UINT, {8, 1}},
    {VK_FORMAT_R64_SINT, {8, 1}},
    {VK_FORMAT_R64_SFLOAT, {8, 1}},
    {VK_FORMAT_R64G64_UINT, {16, 2}},
    {VK_FORMAT_R64G64_SINT, {16, 2}},
    {VK_FORMAT_R64G64_SFLOAT, {16, 2}},
    {VK_FORMAT_R64G64B64_UINT, {24, 3}},
    {VK_FORMAT_R64G64B64_SINT, {24, 3}},
    {VK_FORMAT_R64G64B64_SFLOAT, {24, 3}},
    {VK_FORMAT_R64G64B64A64_UINT, {32, 4}},
    {VK_FORMAT_R64G64B64A64_SINT, {32, 4}},
    {VK_FORMAT_R64G64B64A64_SFLOAT, {32, 4}},
    {VK_FORMAT_B10G11R11_UFLOAT_PACK32, {4, 3}},
    {VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, {4, 3}},
    {VK_FORMAT_D16_UNORM, {2, 1}},
    {VK_FORMAT_X8_D24_UNORM_PACK32, {4, 1}},
    {VK_FORMAT_D32_SFLOAT, {4, 1}},
    {VK_FORMAT_S8_UINT, {1, 1}},
    {VK_FORMAT_D16_UNORM_S8_UINT, {3, 2}},
    {VK_FORMAT_D24_UNORM_S8_UINT, {4, 2}},
    {VK_FORMAT_D32_SFLOAT_S8_UINT, {8, 2}},
};

std::string errorString(VkResult errorCode) {
  switch (errorCode) {
#define STR(r) \
  case VK_##r: \
    return #r
    STR(NOT_READY);
    STR(TIMEOUT);
    STR(EVENT_SET);
    STR(EVENT_RESET);
    STR(INCOMPLETE);
    STR(ERROR_OUT_OF_HOST_MEMORY);
    STR(ERROR_OUT_OF_DEVICE_MEMORY);
    STR(ERROR_INITIALIZATION_FAILED);
    STR(ERROR_DEVICE_LOST);
    STR(ERROR_MEMORY_MAP_FAILED);
    STR(ERROR_LAYER_NOT_PRESENT);
    STR(ERROR_EXTENSION_NOT_PRESENT);
    STR(ERROR_FEATURE_NOT_PRESENT);
    STR(ERROR_INCOMPATIBLE_DRIVER);
    STR(ERROR_TOO_MANY_OBJECTS);
    STR(ERROR_FORMAT_NOT_SUPPORTED);
    STR(ERROR_SURFACE_LOST_KHR);
    STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
    STR(SUBOPTIMAL_KHR);
    STR(ERROR_OUT_OF_DATE_KHR);
    STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
    STR(ERROR_VALIDATION_FAILED_EXT);
    STR(ERROR_INVALID_SHADER_NV);
#undef STR
    default:
      return "UNKNOWN_ERROR";
  }
}

std::string physicalDeviceTypeString(VkPhysicalDeviceType type) {
  switch (type) {
#define STR(r)                      \
  case VK_PHYSICAL_DEVICE_TYPE_##r: \
    return #r
    STR(OTHER);
    STR(INTEGRATED_GPU);
    STR(DISCRETE_GPU);
    STR(VIRTUAL_GPU);
#undef STR
    default:
      return "UNKNOWN_DEVICE_TYPE";
  }
}

VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
              void* pUserData) {
  LogLevel level = LogLevel::info;
  if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    level = LogLevel::warn;
  } else if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    level = LogLevel::error;
  }
  log(level, pCallbackData->pMessage);
  return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger) {
  AVOX_AVOX_VK_INST_FUNC_IMPL_INIT(instance, CreateDebugUtilsMessengerEXT)
  CreateDebugUtilsMessengerEXT(instance, pCreateInfo, pAllocator,
                               pDebugMessenger);
  if (!CreateDebugUtilsMessengerEXT) {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
  return VK_SUCCESS;
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks* pAllocator) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
  AVOX_AVOX_VK_INST_FUNC_IMPL_INIT(instance, DestroyDebugUtilsMessengerEXT)
  DestroyDebugUtilsMessengerEXT(instance, debugMessenger, pAllocator);
}

VkResult createVkDebug(VkInstance instance,
                       VkDebugUtilsMessengerEXT& vkDebugExt) {
  VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;
  VkResult result =
      CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &vkDebugExt);
  if (result == VK_SUCCESS) {
    LOGFLF(LogLevel::info, "create vk debug utils message.");
  } else {
    AVOX_VULKAN_LOG(result, "create vk debug utils message failed.");
  }
  return result;
}

VkResult enumerateDevice(VkInstance instance,
                         std::vector<VKPhysDevWrapper>& pDevices) {
  // Physical device
  uint32_t gpuCount = 0;
  // Get number of available physical devices
  VkResult result = vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
  if (gpuCount == 0) {
    LOGFLF(LogLevel::info, "not find vulkan device");
    return result;
  }
  pDevices.resize(gpuCount);
  std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
  VkResult err =
      vkEnumeratePhysicalDevices(instance, &gpuCount, physicalDevices.data());
  for (uint32_t i = 0; i < gpuCount; i++) {
    pDevices[i].form(physicalDevices[i]);
  }
  return VK_SUCCESS;
}

VkFormat getVkFormat(YuvType type, int32_t bitDepth) {
  VkFormat vkFormat = VK_FORMAT_UNDEFINED;
  switch (type) {
    switch (type) {
      case YuvType::gray:
        switch (bitDepth) {
          case 8:
            vkFormat = VK_FORMAT_R8_UNORM;
            break;
          case 10:
            vkFormat = VK_FORMAT_R10X6_UNORM_PACK16;
            break;
          case 12:
            vkFormat = VK_FORMAT_R12X4_UNORM_PACK16;
            break;
          default:
            vkFormat = VK_FORMAT_R8_UNORM;
            break;
        }
        break;
      default:
        break;
    }
  }
  return vkFormat;
}

VkFormat getVkFormat(ImageType type) {
  switch (type) {
    case ImageType::bgra8:
      return VK_FORMAT_B8G8R8A8_UNORM;
    case ImageType::r16:
      return VK_FORMAT_R16_UINT;
    case ImageType::r8:
      return VK_FORMAT_R8_UNORM;
    case ImageType::rgba8:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case ImageType::rgba32f:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
    case ImageType::r32f:
      return VK_FORMAT_R32_SFLOAT;
    case ImageType::r32:
      return VK_FORMAT_R32_SINT;
    case ImageType::rgba32:
      return VK_FORMAT_R32G32B32A32_SINT;
    case ImageType::rgba16f:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    default:
      return VK_FORMAT_UNDEFINED;
  }
}

VkShaderModule loadShader(const std::vector<char>& code, VkDevice device) {
  VkShaderModule shaderModule;
  VkShaderModuleCreateInfo moduleCreateInfo{};
  moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  moduleCreateInfo.codeSize = code.size();
  moduleCreateInfo.pCode = (uint32_t*)code.data();
  AVOX_VULKAN_LOG(
      vkCreateShaderModule(device, &moduleCreateInfo, NULL, &shaderModule),
      "create shader module");
  return shaderModule;
}

VkShaderModule loadShader(const char* fileName, VkDevice device) {
  std::ifstream is(fileName, std::ios::binary | std::ios::in | std::ios::ate);
  if (is.is_open()) {
    size_t size = is.tellg();
    is.seekg(0, std::ios::beg);
    std::vector<char> shaderCode(size);
    is.read(shaderCode.data(), size);
    is.close();
    assert(size > 0);
    return loadShader(shaderCode, device);
  } else {
    log(LogLevel::error, "error: could not open shader file ", fileName);
    return VK_NULL_HANDLE;
  }
}
#ifdef __ANDROID__
VkShaderModule loadShader(AAssetManager* assetManager, const char* fileName,
                          VkDevice device) {
  AAsset* asset =
      AAssetManager_open(assetManager, fileName, AASSET_MODE_STREAMING);
  // asset 缺失(APK 没打 assets/glsl/*.spv)时 assert 在 release 被编译掉,
  // 直接 AAsset_getLength(nullptr) = SIGSEGV; 与桌面分支对称地软失败
  if (!asset) {
    LOGFLF(LogLevel::error, "error: could not open shader asset ", fileName);
    return VK_NULL_HANDLE;
  }
  size_t size = AAsset_getLength(asset);
  assert(size > 0);
  std::vector<char> shaderCode(size);
  AAsset_read(asset, shaderCode.data(), size);
  AAsset_close(asset);

  return loadShader(shaderCode, device);
}
#endif

int32_t vkPixelSize(VkFormat format) {
  auto item = formatTable.find(format);
  if (item != formatTable.end()) {
    return item->second.size;
  }
  return 0;
}

bool havaFlags(uint32_t flags, uint32_t flag) { return (flags & flag) == flag; }

void changeLayout(VkCommandBuffer command, VkImage image,
                  VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags oldStageFlags,
                  VkPipelineStageFlags newStageFlags,
                  VkImageAspectFlags aspectMask, VkAccessFlags newAccessFlags) {
  VkImageMemoryBarrier imageMemoryBarrier = {};
  imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  imageMemoryBarrier.pNext = nullptr;
  imageMemoryBarrier.srcAccessMask = 0;
  imageMemoryBarrier.dstAccessMask = 0;
  imageMemoryBarrier.oldLayout = oldLayout;
  imageMemoryBarrier.newLayout = newLayout;
  imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  imageMemoryBarrier.image = image;
  imageMemoryBarrier.subresourceRange = {aspectMask, 0, 1, 0, 1};
  switch (oldLayout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      // 必须仅用作传输命令的目标映像,要求启用VK_IMAGE_USAGE_TRANSFER_DST_BIT
      imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
      // 该布局旨在用作其内容由主机写入的图像的初始布局,因此无需首先执行布局转换就可以将数据立即写入内存
      imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
      break;
    default:
      break;
  }
  switch (newLayout) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      imageMemoryBarrier.dstAccessMask =
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      break;
    default:
      break;
  }
  if (newAccessFlags != 0) {
    imageMemoryBarrier.dstAccessMask = newAccessFlags;
  }
  // 等待命令列表里GPU里处理完成
  vkCmdPipelineBarrier(command, oldStageFlags, newStageFlags, 0, 0, nullptr, 0,
                       nullptr, 1, &imageMemoryBarrier);
}

void changeLayout(VkCommandBuffer command, VkBuffer buffer,
                  VkPipelineStageFlags oldStageFlags,
                  VkPipelineStageFlags newStageFlags,
                  VkAccessFlags oldAccessFlags, VkAccessFlags newAccessFlags) {
  VkBufferMemoryBarrier bufBarrier = {};
  bufBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;

  bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bufBarrier.buffer = buffer;
  bufBarrier.offset = 0;
  bufBarrier.size = VK_WHOLE_SIZE;
  bufBarrier.srcAccessMask = oldAccessFlags;
  bufBarrier.dstAccessMask = newAccessFlags;
  vkCmdPipelineBarrier(command, oldStageFlags, newStageFlags, 0, 0, nullptr, 1,
                       &bufBarrier, 0, nullptr);
}

void createSampler(VkDevice device, bool bLinear, VkSampler& sampler) {
  VkSamplerCreateInfo samplerCreateInfo = {};
  samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerCreateInfo.magFilter = bLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  samplerCreateInfo.minFilter = bLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerCreateInfo.mipLodBias = 0.0;
  samplerCreateInfo.anisotropyEnable = VK_FALSE;
  samplerCreateInfo.maxAnisotropy = 1;
  samplerCreateInfo.compareEnable = VK_FALSE;
  samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
  samplerCreateInfo.minLod = 0.0;
  samplerCreateInfo.maxLod = 0.0;
  samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  AVOX_VULKAN_LOG(vkCreateSampler(device, &samplerCreateInfo, nullptr, &sampler),
                 "create sampler failed");
}

void bufferToImage(VkCommandBuffer cmd, const VkWrapBuffer* buffer,
                   const VkTexture* texture, int32_t rowPitch) {
  VkBufferImageCopy copyRegion = {};
  copyRegion.bufferOffset = 0;
  copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copyRegion.imageSubresource.mipLevel = 0;
  copyRegion.imageSubresource.baseArrayLayer = 0;
  copyRegion.imageSubresource.layerCount = 1;
  copyRegion.imageExtent.width = texture->width;
  copyRegion.imageExtent.height = texture->height;
  copyRegion.imageExtent.depth = 1;
  // rowPitch > 0时，buffer中每行有stride对齐的填充，需要告诉Vulkan实际的行宽
  // rowPitch == 0时，bufferRowLength为0表示数据紧密排列
  if (rowPitch > 0 && texture->pixelSize > 0) {
    copyRegion.bufferRowLength = rowPitch / texture->pixelSize;
  }
  // buffer to image
  vkCmdCopyBufferToImage(cmd, buffer->buffer, texture->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
}

void imageToBuffer(VkCommandBuffer cmd, const VkTexture* texture,
                   VkWrapBuffer* buffer, int32_t rowPitch) {
  if (!texture || !buffer || texture->width <= 0 || texture->height <= 0) {
    return;
  }
  int32_t bufferSize = buffer->getBufferSize();
  int32_t pixelSize = texture->pixelSize <= 0 ? 1 : texture->pixelSize;
  int32_t textSize = pixelSize * texture->width * texture->height;
  int32_t pitchWidth = rowPitch / pixelSize;
  if (pitchWidth <= 0 && bufferSize != textSize) {
    // 每行的总字节数 = 总大小 / 行数
    int32_t rowPitchBytes = bufferSize / texture->height;
    // 每行的像素步长 = 每行总字节数 / 单个像素字节数
    pitchWidth = rowPitchBytes / pixelSize;
  }
  // 保障性校验：缓冲区的物理步长绝对不应小于图像本身的像素宽度
  if (pitchWidth < texture->width && pitchWidth > 0) {
    pitchWidth = texture->width;
  }
  imageToBuffer(cmd, texture->image, buffer->buffer, texture->width,
                texture->height, pitchWidth);
}

void imageToBuffer(VkCommandBuffer cmd, VkImage texture, VkBuffer buffer,
                   int32_t width, int32_t height, int32_t pitchWidth) {
  VkBufferImageCopy copyRegion = {};
  copyRegion.bufferOffset = 0;
  copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copyRegion.imageSubresource.mipLevel = 0;
  copyRegion.imageSubresource.baseArrayLayer = 0;
  copyRegion.imageSubresource.layerCount = 1;
  copyRegion.imageExtent.width = width;
  copyRegion.imageExtent.height = height;
  copyRegion.imageExtent.depth = 1;
  // 行尾部保留 padding 字节
  copyRegion.bufferRowLength = (pitchWidth > width) ? pitchWidth : 0;
  copyRegion.bufferImageHeight = 0; 
  vkCmdCopyImageToBuffer(cmd, texture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         buffer, 1, &copyRegion);
}

void blitFillImage(VkCommandBuffer cmd, const VkTexture* src,
                   const VkTexture* dest) {
  blitFillImage(cmd, src, dest->image, dest->width, dest->height, dest->layout);
}

void blitFillImage(VkCommandBuffer cmd, const VkTexture* src, VkImage dest,
                   int32_t destWidth, int32_t destHeight,
                   VkImageLayout destLayout) {
  vec4i destRect = {};
  destRect.x = 0;
  destRect.y = 0;
  destRect.z = destWidth;
  destRect.w = destHeight;
  blitFillImage(cmd, src, dest, destRect, destLayout);
}

void blitFillImage(VkCommandBuffer cmd, const VkTexture* src, VkImage dest,
                   vec4i rect, VkImageLayout destLayout) {
  VkImageBlit region = {};
  region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.srcSubresource.mipLevel = 0;
  region.srcSubresource.baseArrayLayer = 0;
  region.srcSubresource.layerCount = 1;
  region.srcOffsets[0] = {0, 0, 0};
  region.srcOffsets[1] = {(int32_t)src->width, (int32_t)src->height, 1};
  region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.dstSubresource.mipLevel = 0;
  region.dstSubresource.baseArrayLayer = 0;
  region.dstSubresource.layerCount = 1;
  region.dstOffsets[0] = {rect.x, rect.y, 0};
  region.dstOffsets[1] = {rect.x + rect.z, rect.y + rect.w, 1};
  // vkCmdBlitImage 要求源图像布局必须是 TRANSFER_SRC_OPTIMAL 或 GENERAL
  // 如果当前是 UNDEFINED 或 TRANSFER_DST_OPTIMAL，需要临时转换
  VkImageLayout srcLayout = src->layout;
  bool layoutTransitioned = false;
  if (srcLayout == VK_IMAGE_LAYOUT_UNDEFINED ||
      srcLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    changeLayout(cmd, src->image, srcLayout,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, src->stageFlags,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_ACCESS_TRANSFER_READ_BIT);
    srcLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    layoutTransitioned = true;
  }
  // 要将源图像的区域复制到目标图像中,并可能执行格式转换,任意缩放和过滤
  vkCmdBlitImage(cmd, src->image, srcLayout, dest, destLayout, 1, &region,
                 VK_FILTER_LINEAR);
  // 恢复原始布局
  if (layoutTransitioned) {
    VkImageLayout targetLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    changeLayout(cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 targetLayout, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_ACCESS_TRANSFER_WRITE_BIT);
  }
}

void copyImage(VkCommandBuffer cmd, const VkTexture* src, VkImage dest) {
  // GENERAL (计算着色器存储图输出, 如字幕 FontLayer) 可直接作为拷贝源布局,
  // 无需 TRANSFER_SRC 用法位; 其他布局按习惯转 TRANSFER_SRC_OPTIMAL
  const VkImageLayout srcLayout = src->layout == VK_IMAGE_LAYOUT_GENERAL
                                      ? VK_IMAGE_LAYOUT_GENERAL
                                      : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  VkImageCopy copyRegion = {};
  copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copyRegion.srcSubresource.layerCount = 1;
  copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copyRegion.dstSubresource.layerCount = 1;
  copyRegion.extent = {(uint32_t)src->width, (uint32_t)src->height, 1};
  vkCmdCopyImage(cmd, src->image, srcLayout, dest,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
}

void copyImage(VkCommandBuffer cmd, VkImage src, VkImage dest, int32_t width,
               int32_t height) {
  VkImageCopy copyRegion = {};

  copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copyRegion.srcSubresource.baseArrayLayer = 0;
  copyRegion.srcSubresource.mipLevel = 0;
  copyRegion.srcSubresource.layerCount = 1;
  copyRegion.srcOffset = {0, 0, 0};

  copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copyRegion.dstSubresource.baseArrayLayer = 0;
  copyRegion.dstSubresource.mipLevel = 0;
  copyRegion.dstSubresource.layerCount = 1;
  copyRegion.dstOffset = {0, 0, 0};

  copyRegion.extent.width = width;
  copyRegion.extent.height = height;
  copyRegion.extent.depth = 1;

  vkCmdCopyImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
}

}
