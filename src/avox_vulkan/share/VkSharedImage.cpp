#include "VkSharedImage.hpp"

#include "../VkHelper.hpp"

#ifdef _WIN32
#include <vulkan/vulkan_win32.h>
#include <windows.h>
#endif

#if defined(__ANDROID__)
#include <vulkan/vulkan_android.h>
#include <android/hardware_buffer.h>
#include <unistd.h>  // close (VkShareHandle fd RAII)
#endif

namespace avox {

// ── VkShareHandle RAII ──

VkShareHandle::~VkShareHandle() {
#ifdef _WIN32
  if (type == VkShareHandleType::opaqueWin32 && handle != nullptr &&
      handle != INVALID_HANDLE_VALUE) {
    CloseHandle((HANDLE)handle);
  }
#elif defined(__linux__) || defined(__ANDROID__)
  if (type == VkShareHandleType::opaqueFd && handle != nullptr) {
    close((int)(intptr_t)handle);
  }
  if (type == VkShareHandleType::androidHwBuffer && handle != nullptr) {
    AHardwareBuffer_release(reinterpret_cast<AHardwareBuffer*>(handle));
  }
#endif
  type = VkShareHandleType::none;
  handle = nullptr;
}

VkShareHandle::VkShareHandle(VkShareHandle&& other) noexcept {
  type = other.type;
  handle = other.handle;
  other.type = VkShareHandleType::none;
  other.handle = nullptr;
}

VkShareHandle& VkShareHandle::operator=(VkShareHandle&& other) noexcept {
  if (this != &other) {
    this->~VkShareHandle();
    type = other.type;
    handle = other.handle;
    other.type = VkShareHandleType::none;
    other.handle = nullptr;
  }
  return *this;
}

// ── VkSharedImage ──

VkSharedImage::VkSharedImage(/* args */) {}

VkSharedImage::~VkSharedImage() { release(); }

void VkSharedImage::release() {
  if (vkImage) {
    vkDestroyImage(vkDevice, vkImage, nullptr);
    vkImage = VK_NULL_HANDLE;
  }
  if (memory) {
    vkFreeMemory(vkDevice, memory, nullptr);
    memory = VK_NULL_HANDLE;
  }
  bExported = false;
  bImported = false;
}

bool VkSharedImage::createExportable(const VkSharedImageDesc& desc_) {
  release();
  desc = desc_;
  // 加载平台相关的导出/导入函数
#ifdef _WIN32
  if (desc.handleType == VkShareHandleType::opaqueWin32) {
    vkGetMemoryWin32HandleKHR =
        reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(
            vkGetDeviceProcAddr(vkDevice, "vkGetMemoryWin32HandleKHR"));
    if (!vkGetMemoryWin32HandleKHR) {
      LOGFLF(LogLevel::warn, "failed to load external memory functions");
      return false;
    }
  }
#endif
#ifdef __ANDROID__
  if (desc.handleType == VkShareHandleType::androidHwBuffer) {
    vkGetMemoryAndroidHardwareBufferANDROID =
        reinterpret_cast<PFN_vkGetMemoryAndroidHardwareBufferANDROID>(
            vkGetDeviceProcAddr(vkDevice, "vkGetMemoryAndroidHardwareBufferANDROID"));
    if (!vkGetMemoryAndroidHardwareBufferANDROID) {
      LOGFLF(LogLevel::warn, "failed to load android hw buffer functions");
      return false;
    }
  }
#endif

  // 1. 创建可导出的 VkImage
  VkExternalMemoryHandleTypeFlagBits vkHandleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#ifdef _WIN32
  if (desc.handleType == VkShareHandleType::opaqueWin32) {
    vkHandleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
  }
#endif
#ifdef __ANDROID__
  if (desc.handleType == VkShareHandleType::androidHwBuffer) {
    vkHandleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
  }
#endif

  VkExternalMemoryImageCreateInfo externalCreateInfo = {
      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
  externalCreateInfo.handleTypes = vkHandleType;

  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.pNext = &externalCreateInfo;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = desc.format;
  imageInfo.extent = {(uint32_t)desc.width, (uint32_t)desc.height, 1u};
  imageInfo.mipLevels = 1u;
  imageInfo.arrayLayers = 1u;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = desc.usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  AVOX_VULKAN_LOG(vkCreateImage(vkDevice, &imageInfo, nullptr, &vkImage),
                 "create exportable image failed");

  // 2. 获取内存需求
  VkMemoryRequirements memReqs = {};
  vkGetImageMemoryRequirements(vkDevice, vkImage, &memReqs);

  // 3. 分配可导出的 VkDeviceMemory
  VkExportMemoryAllocateInfo exportMemInfo = {
      VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
  exportMemInfo.handleTypes = vkHandleType;

  VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.pNext = &exportMemInfo;
  allocInfo.allocationSize = memReqs.size;

  // 查找 memory type
  uint32_t memoryTypeIndex = 0;
  bool bFound = wphyDevcie->getMemoryTypeIndex(memReqs.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                 memoryTypeIndex);
  if (!bFound) {
    LOGFLF(LogLevel::warn, "failed to find memory type for exportable image");
    return false;
  }
  allocInfo.memoryTypeIndex = memoryTypeIndex;

  AVOX_VULKAN_LOG(vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory),
                 "allocate exportable memory failed");

  // 4. 绑定
  AVOX_VULKAN_LOG(vkBindImageMemory(vkDevice, vkImage, memory, 0),
                 "bind exportable image memory failed");

  bExported = true;
  LOGFLF(LogLevel::info, "VkSharedImage::createExportable vkImage:", vkImage,
         " memory:", memory, " width:", desc.width, " height:", desc.height);
  return true;
}

VkShareHandle VkSharedImage::exportHandle() {
  VkShareHandle handle = {};
  if (!bExported || !memory) {
    return handle;
  }
#ifdef _WIN32
  if (desc.handleType == VkShareHandleType::opaqueWin32 && vkGetMemoryWin32HandleKHR) {
    VkMemoryGetWin32HandleInfoKHR getInfo = {
        VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR};
    getInfo.memory = memory;
    getInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    HANDLE winHandle = nullptr;
    VkResult result = vkGetMemoryWin32HandleKHR(vkDevice, &getInfo, &winHandle);
    if (result != VK_SUCCESS) {
      LOGFLF(LogLevel::warn, "export memory handle failed:", errorString(result));
      return handle;
    }
    handle.type = VkShareHandleType::opaqueWin32;
    handle.handle = winHandle;
  }
#endif
#ifdef __ANDROID__
  if (desc.handleType == VkShareHandleType::androidHwBuffer &&
      vkGetMemoryAndroidHardwareBufferANDROID) {
    VkMemoryGetAndroidHardwareBufferInfoANDROID getInfo = {
        VK_STRUCTURE_TYPE_MEMORY_GET_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
    getInfo.memory = memory;
    AHardwareBuffer* ahb = nullptr;
    VkResult result =
        vkGetMemoryAndroidHardwareBufferANDROID(vkDevice, &getInfo, &ahb);
    if (result != VK_SUCCESS) {
      LOGFLF(LogLevel::warn, "export android hw buffer failed:", errorString(result));
      return handle;
    }
    handle.type = VkShareHandleType::androidHwBuffer;
    handle.handle = ahb;
  }
#endif
  return handle;
}

bool VkSharedImage::importFromHandle(const VkShareHandle& handle,
                                      const VkSharedImageDesc& desc_) {
  release();
  desc = desc_;
  // 1. 创建 VkImage(带 external memory 标记)
  VkExternalMemoryHandleTypeFlagBits vkHandleType =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

  VkExternalMemoryImageCreateInfo externalCreateInfo = {
      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
  externalCreateInfo.handleTypes = vkHandleType;

  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.pNext = &externalCreateInfo;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = desc.format;
  imageInfo.extent = {(uint32_t)desc.width, (uint32_t)desc.height, 1u};
  imageInfo.mipLevels = 1u;
  imageInfo.arrayLayers = 1u;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = desc.usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  AVOX_VULKAN_LOG(vkCreateImage(vkDevice, &imageInfo, nullptr, &vkImage),
                 "create import image failed");

  // 2. 导入 VkDeviceMemory
  VkMemoryRequirements memReqs = {};
  vkGetImageMemoryRequirements(vkDevice, vkImage, &memReqs);

#ifdef _WIN32
  VkImportMemoryWin32HandleInfoKHR importMemInfo = {
      VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
  importMemInfo.handleType = vkHandleType;
  importMemInfo.handle = (HANDLE)handle.handle;

  VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.pNext = &importMemInfo;
  allocInfo.allocationSize = memReqs.size;

  // memoryTypeIndex 需要从 Win32 handle 属性和 image 需求交集获取
  PFN_vkGetMemoryWin32HandlePropertiesKHR vkGetMemoryWin32HandlePropertiesKHR =
      reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
          vkGetDeviceProcAddr(vkDevice, "vkGetMemoryWin32HandlePropertiesKHR"));
  uint32_t memoryTypeIndex = 0;
  if (vkGetMemoryWin32HandlePropertiesKHR) {
    VkMemoryWin32HandlePropertiesKHR win32Props = {
        VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
    vkGetMemoryWin32HandlePropertiesKHR(vkDevice, vkHandleType,
                                         (HANDLE)handle.handle, &win32Props);
    uint32_t memoryBit = memReqs.memoryTypeBits & win32Props.memoryTypeBits;
    if (memoryBit == 0) {
      LOGFLF(LogLevel::warn, "no compatible memory type for import");
      return false;
    }
    // 取第一个可用位
    for (uint32_t i = 0; i < 32; i++) {
      if (memoryBit & (1u << i)) {
        memoryTypeIndex = i;
        break;
      }
    }
  } else {
    // 回退: 直接用 image 的 memoryTypeBits 第一个
    for (uint32_t i = 0; i < 32; i++) {
      if (memReqs.memoryTypeBits & (1u << i)) {
        memoryTypeIndex = i;
        break;
      }
    }
  }
  allocInfo.memoryTypeIndex = memoryTypeIndex;

  AVOX_VULKAN_LOG(vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory),
                 "allocate import memory failed");
#endif

  // 3. 绑定
  AVOX_VULKAN_LOG(vkBindImageMemory(vkDevice, vkImage, memory, 0),
                 "bind import image memory failed");

  bImported = true;
  LOGFLF(LogLevel::info, "VkSharedImage::importFromHandle vkImage:", vkImage,
         " memory:", memory, " width:", desc.width, " height:", desc.height);
  return true;
}

}
