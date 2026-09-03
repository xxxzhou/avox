#include "VkShareChecker.hpp"
#include "VkSharedImage.hpp"

#include "../VkHelper.hpp"

namespace avox {

// VkShareHandleType → VkExternalMemoryHandleTypeFlagBits
static VkExternalMemoryHandleTypeFlagBits toVkHandleType(VkShareHandleType type) {
  switch (type) {
    case VkShareHandleType::opaqueWin32:
      return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    case VkShareHandleType::opaqueFd:
      return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    case VkShareHandleType::androidHwBuffer:
      return VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    case VkShareHandleType::hostAllocation:
      return VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    default:
      return static_cast<VkExternalMemoryHandleTypeFlagBits>(0);
  }
}

// VkFormat → VkImageFormatProperties2 查询
static bool checkExternalImageFormat(VkPhysicalDevice phyDevice, VkFormat format,
                                     VkImageUsageFlags usage,
                                     VkExternalMemoryHandleTypeFlagBits vkHandleType,
                                     bool bExport) {
  VkPhysicalDeviceExternalImageFormatInfo externalInfo = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
  externalInfo.handleType = vkHandleType;

  VkPhysicalDeviceImageFormatInfo2 formatInfo = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
  formatInfo.pNext = &externalInfo;
  formatInfo.format = format;
  formatInfo.type = VK_IMAGE_TYPE_2D;
  formatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  formatInfo.usage = usage;

  VkExternalImageFormatProperties externalProps = {
      VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
  VkImageFormatProperties2 imageProps = {
      VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
  imageProps.pNext = &externalProps;

  VkResult result = vkGetPhysicalDeviceImageFormatProperties2(
      phyDevice, &formatInfo, &imageProps);
  if (result != VK_SUCCESS) {
    return false;
  }

  const auto& features = externalProps.externalMemoryProperties.externalMemoryFeatures;
  if (bExport) {
    return (features & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) != 0;
  }
  return (features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
}

bool VkShareChecker::checkExportable(VkPhysicalDevice phyDevice,
                                      const VkSharedImageDesc& desc,
                                      VkShareHandleType handleType) {
  VkExternalMemoryHandleTypeFlagBits vkType = toVkHandleType(handleType);
  if (vkType == 0) {
    return false;
  }
  return checkExternalImageFormat(phyDevice, desc.format, desc.usage, vkType, true);
}

bool VkShareChecker::checkImportable(VkPhysicalDevice phyDevice,
                                      const VkSharedImageDesc& desc,
                                      VkShareHandleType handleType) {
  VkExternalMemoryHandleTypeFlagBits vkType = toVkHandleType(handleType);
  if (vkType == 0) {
    return false;
  }
  return checkExternalImageFormat(phyDevice, desc.format, desc.usage, vkType, false);
}

bool VkShareChecker::checkCompatible(VkPhysicalDevice phyA, VkPhysicalDevice phyB,
                                      VkShareHandleType handleType) {
  // 两端都必须支持指定 handle type 的导出+导入
  VkExternalMemoryHandleTypeFlagBits vkType = toVkHandleType(handleType);
  if (vkType == 0) {
    return false;
  }
  // 检查 A 端导出 + B 端导入的基本能力(不指定具体 format/usage)
  // 简化: 直接检查 handle type 的 exportable/importable 特性
  VkPhysicalDeviceExternalImageFormatInfo externalInfo = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
  externalInfo.handleType = vkType;

  VkPhysicalDeviceImageFormatInfo2 formatInfo = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
  formatInfo.pNext = &externalInfo;
  formatInfo.format = VK_FORMAT_R8G8B8A8_UNORM;  // 用常见格式做能力检测
  formatInfo.type = VK_IMAGE_TYPE_2D;
  formatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  formatInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

  VkExternalImageFormatProperties extPropsA = {
      VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
  VkImageFormatProperties2 imgPropsA = {
      VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
  imgPropsA.pNext = &extPropsA;

  VkResult resultA = vkGetPhysicalDeviceImageFormatProperties2(
      phyA, &formatInfo, &imgPropsA);
  if (resultA != VK_SUCCESS) {
    return false;
  }

  VkExternalImageFormatProperties extPropsB = {
      VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
  VkImageFormatProperties2 imgPropsB = {
      VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
  imgPropsB.pNext = &extPropsB;

  VkResult resultB = vkGetPhysicalDeviceImageFormatProperties2(
      phyB, &formatInfo, &imgPropsB);
  if (resultB != VK_SUCCESS) {
    return false;
  }

  const auto& featuresA = extPropsA.externalMemoryProperties.externalMemoryFeatures;
  const auto& featuresB = extPropsB.externalMemoryProperties.externalMemoryFeatures;
  return (featuresA & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) != 0 &&
         (featuresB & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
}

VkShareHandleType VkShareChecker::selectBestHandle(VkPhysicalDevice phyA,
                                                     VkPhysicalDevice phyB,
                                                     const VkSharedImageDesc& desc) {
  // 优先 OPAQUE(零拷贝)
#ifdef _WIN32
  if (checkExportable(phyA, desc, VkShareHandleType::opaqueWin32) &&
      checkImportable(phyB, desc, VkShareHandleType::opaqueWin32)) {
    return VkShareHandleType::opaqueWin32;
  }
#elif defined(__ANDROID__)
  if (checkExportable(phyA, desc, VkShareHandleType::androidHwBuffer) &&
      checkImportable(phyB, desc, VkShareHandleType::androidHwBuffer)) {
    return VkShareHandleType::androidHwBuffer;
  }
#elif defined(__linux__)
  if (checkExportable(phyA, desc, VkShareHandleType::opaqueFd) &&
      checkImportable(phyB, desc, VkShareHandleType::opaqueFd)) {
    return VkShareHandleType::opaqueFd;
  }
#endif
  // 回退 host allocation
  if (checkExportable(phyA, desc, VkShareHandleType::hostAllocation) &&
      checkImportable(phyB, desc, VkShareHandleType::hostAllocation)) {
    return VkShareHandleType::hostAllocation;
  }
  return VkShareHandleType::none;
}

bool VkShareChecker::bSameGpu(VkPhysicalDevice phyA, VkPhysicalDevice phyB) {
  if (phyA == phyB) {
    return true;
  }
  // 通过 LUID 比较
  VkPhysicalDeviceIDProperties idPropsA = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
  VkPhysicalDeviceProperties2 propsA = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  propsA.pNext = &idPropsA;
  vkGetPhysicalDeviceProperties2(phyA, &propsA);

  VkPhysicalDeviceIDProperties idPropsB = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
  VkPhysicalDeviceProperties2 propsB = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  propsB.pNext = &idPropsB;
  vkGetPhysicalDeviceProperties2(phyB, &propsB);

  if (!idPropsA.deviceLUIDValid || !idPropsB.deviceLUIDValid) {
    return false;
  }
  return memcmp(idPropsA.deviceLUID, idPropsB.deviceLUID, VK_LUID_SIZE) == 0;
}

}
