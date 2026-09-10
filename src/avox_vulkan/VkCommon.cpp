#include "VkCommon.hpp"

#include "VkHelper.hpp"

namespace avox {

static int32_t vkInstId = 0;
static const char* vkAppName = "avox_vk_instance";

VKLayerProps::VKLayerProps() {}

VKLayerProps::~VKLayerProps() {}

void VKLayerProps::init() {
  uint32_t layerCount = 0;
  AVOX_VULKAN_LOG(vkEnumerateInstanceLayerProperties(&layerCount, NULL),
                 "vkEnumerateInstanceLayerProperties failed");
  if (layerCount > 0) {
    layerProps.resize(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layerProps.data());
  }
  uint32_t exLayerCount = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &exLayerCount, nullptr);
  if (exLayerCount > 0) {
    exLayerProps.resize(exLayerCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &exLayerCount,
                                           exLayerProps.data());
  }
}

bool VKLayerProps::findLayer(const char* layerName) {
  for (const auto& layerProp : layerProps) {
    if (strcmp(layerName, layerProp.layerName) == 0) {
      return true;
    }
  }
  return false;
}

bool VKLayerProps::findExtension(const char* name) {
  for (const auto& layerProp : exLayerProps) {
    if (strcmp(name, layerProp.extensionName) == 0) {
      return true;
    }
  }
  return false;
}

void VKLayerProps::dump() {
  for (const auto& layerProp : layerProps) {
    log(LogLevel::info, "layerName:", layerProp.layerName,
        " desc:", layerProp.description);
  }
  for (const auto& layerProp : exLayerProps) {
    log(LogLevel::info, "ex layerName:", layerProp.extensionName);
  }
}

VkInstanceArgs VkInstanceArgs::defArgs(bool bDebug) {
  VkInstanceArgs args = {};
  args.appName = vkAppName + std::to_string(vkInstId++);
  args.bDebugMsg = bDebug;
  // Vulkan 验证层用于调试和检测错误
  if (bDebug) {
    args.layers = {{"VK_LAYER_KHRONOS_validation"}};
  }
  args.extensions = {
      {VK_KHR_SURFACE_EXTENSION_NAME},
#ifdef _WIN32
      {VK_KHR_WIN32_SURFACE_EXTENSION_NAME},
      {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME},
      {VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME},
#elif defined(__ANDROID__)
      {VK_KHR_ANDROID_SURFACE_EXTENSION_NAME},
      // 和android里的AHardwareBuffer交互
      {VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME},
      {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME},
#elif defined(__APPLE__)
      {VK_EXT_METAL_SURFACE_EXTENSION_NAME},
#endif
  };
  if (bDebug) {
    // VK_EXT_DEBUG_REPORT_EXTENSION_NAME/VK_EXT_DEBUG_UTILS_EXTENSION_NAME
    // 都可以调试，选择一种，使用对应的API函数
    // args.extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    args.extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }
  return args;
}

VkInstance VkInstanceArgs::crateInstace() {
  // 初始化layer/extensions
  vLayerProps.init();
  // 移除不支持的layers项
  auto it = layers.begin();
  while (it != layers.end()) {
    const auto& layer = *it;
    if (!vLayerProps.findLayer(layer)) {
      log(LogLevel::warn, "vulkan layer not support:", layer);
      it = layers.erase(it);
    } else {
      ++it;
    }
  }
  // 移除不支持的extensions项
  auto extIt = extensions.begin();
  while (extIt != extensions.end()) {
    const auto& ex = *extIt;
    if (!vLayerProps.findExtension(ex)) {
      log(LogLevel::warn, "vulkan extensions not support:", ex);
      extIt = extensions.erase(extIt);
    } else {
      ++extIt;
    }
  }
  VkApplicationInfo app_info = {};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = appName.c_str();
  app_info.applicationVersion = 0;
#if defined(__ANDROID__)
  // Android 默认使用 1.3 版本
  app_info.apiVersion = VK_API_VERSION_1_3;
#elif defined(VK_HEADER_VERSION_COMPLETE)
  // Windows/Linux 使用最新头文件版本
  app_info.apiVersion = VK_HEADER_VERSION_COMPLETE;
#else
  app_info.apiVersion = VK_MAKE_API_VERSION(0, 1, 1, 0);  // 兜底方案
#endif

  VkInstanceCreateInfo instance_info = {};
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pApplicationInfo = &app_info;
#if defined(__APPLE__)
  // MoltenVK 是 portability 驱动: 不置枚举位并显式启用该扩展, vkCreateInstance 按
  // spec 返回 ERROR_INCOMPATIBLE_DRIVER (Apple 上 Vulkan 一直因此起不来回退 Metal)
  instance_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif
  instance_info.enabledLayerCount = (uint32_t)layers.size();
  instance_info.ppEnabledLayerNames = layers.data();
  instance_info.enabledExtensionCount = (uint32_t)extensions.size();
  instance_info.ppEnabledExtensionNames = extensions.data();
  VkInstance instance = VK_NULL_HANDLE;
  AVOX_VULKAN_LOG(vkCreateInstance(&instance_info, nullptr, &instance),
                 "create instance failed");
  return instance;
}

void VKPhysDevWrapper::form(VkPhysicalDevice phyDevice) {
  clear();
  physicalDevice = phyDevice;
  vkGetPhysicalDeviceProperties(phyDevice, &properties);
  // LUID
  VkPhysicalDeviceIDProperties deviceIDProps = {};
  deviceIDProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
  deviceIDProps.pNext = nullptr;
  VkPhysicalDeviceProperties2 props2 = {};
  props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  props2.pNext = &deviceIDProps;  
  // 调用获取属性
  vkGetPhysicalDeviceProperties2(phyDevice, &props2);  
  if (deviceIDProps.deviceLUIDValid) {   
    memcpy(&luid, deviceIDProps.deviceLUID, VK_LUID_SIZE);
    // LOGFLF(LogLevel::info, "Vulkan Device LUID:", luid);
  }
  uint32_t queueFamilyCount = 0;
  // 获取队列族基本属性，包含是否支持图像，计算，传输，解码
  vkGetPhysicalDeviceQueueFamilyProperties2(phyDevice, &queueFamilyCount,
                                            nullptr);
  props.resize(queueFamilyCount);
#ifdef AVOX_ENABLE_VULKAN_DECODE
  videoQueues.resize(queueFamilyCount);
  queryResultStatus.resize(queueFamilyCount);
#endif
  // vkGetPhysicalDeviceQueueFamilyProperties2利用next得到更多属性
  if (queueFamilyCount > 0) {
    for (uint32_t i = 0; i < props.size(); i++) {
      props[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
#ifdef AVOX_ENABLE_VULKAN_DECODE
      videoQueues[i].sType =
          VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
      props[i].pNext = &videoQueues[i];
      queryResultStatus[i].sType =
          VK_STRUCTURE_TYPE_QUEUE_FAMILY_QUERY_RESULT_STATUS_PROPERTIES_KHR;
      videoQueues[i].pNext = &queryResultStatus[i];
#endif
    }
    vkGetPhysicalDeviceQueueFamilyProperties2(phyDevice, &queueFamilyCount,
                                              props.data());
  }

  for (int j = 0; j < props.size(); j++) {
    uint32_t queueFlags = props[j].queueFamilyProperties.queueFlags;
    bool bGraphics = havaFlags(queueFlags, VK_QUEUE_GRAPHICS_BIT);
    bool bCompute = havaFlags(queueFlags, VK_QUEUE_COMPUTE_BIT);
    bool bTransfer = havaFlags(queueFlags, VK_QUEUE_TRANSFER_BIT);
#ifdef AVOX_ENABLE_VULKAN_DECODE
    bool bDecode = havaFlags(queueFlags, VK_QUEUE_VIDEO_DECODE_BIT_KHR);
    if (bDecode) {
      decodeIndexs.push_back(j);
    }
#endif
    if (bGraphics) {
      graphicsIndexs.push_back(j);
    }
    if (bTransfer) {
      transferIndexs.push_back(j);
    }
    if (bCompute) {
      computeIndexs.push_back(j);
    }
  }
  // 获取扩展属性
  uint32_t exCount = 0;
  VkResult result = vkEnumerateDeviceExtensionProperties(
      physicalDevice, nullptr, &exCount, nullptr);
  if (exCount > 0) {
    exProps.resize(exCount);
    result = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr,
                                                  &exCount, exProps.data());
  }
  // 获取内存属性
  vkGetPhysicalDeviceMemoryProperties(phyDevice, &mempryProperties);
}

void VKPhysDevWrapper::clear() {
  props.clear();
#ifdef AVOX_ENABLE_VULKAN_DECODE
  videoQueues.clear();
  queryResultStatus.clear();
#endif
  exProps.clear();
  graphicsIndexs.clear();
  computeIndexs.clear();
  decodeIndexs.clear();
  encodeIndexs.clear();
  transferIndexs.clear();
  physicalDevice = VK_NULL_HANDLE;
}

bool VKPhysDevWrapper::findIndex(QueueType type, int32_t& index) {
  for (int j = 0; j < props.size(); j++) {
    uint32_t queueFlags = props[j].queueFamilyProperties.queueFlags;
    bool bGraphics = havaFlags(queueFlags, VK_QUEUE_GRAPHICS_BIT);
    bool bCompute = havaFlags(queueFlags, VK_QUEUE_COMPUTE_BIT);
    bool mustGraphics =
        havaFlags((uint32_t)type, (uint32_t)QueueType::graphics);
    bool mustCompute = havaFlags((uint32_t)type, (uint32_t)QueueType::compute);
#ifdef AVOX_ENABLE_VULKAN_DECODE
    bool bDecode = havaFlags(queueFlags, VK_QUEUE_VIDEO_DECODE_BIT_KHR);
    bool mustDecode = havaFlags((uint32_t)type, (uint32_t)QueueType::decode);
    if (mustDecode && !bDecode) {
      continue;
    }
#endif
    if (mustGraphics && !bGraphics) {
      continue;
    }
    if (mustCompute && !bCompute) {
      continue;
    }

    index = j;
    return true;
  }
  return false;
}

bool VKPhysDevWrapper::findSurfaceQueue(VkSurfaceKHR surface,
                                        int32_t graphicsIndex,
                                        int32_t& presentIndex) {
  uint32_t queueFamilyCount = static_cast<uint32_t>(props.size());
  std::vector<VkBool32> supportsPresent(queueFamilyCount);
  // 检查每个通道的表面是否支持显示
  for (uint32_t i = 0; i < queueFamilyCount; i++) {
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface,
                                         &supportsPresent[i]);
  }
  // 查看是否能呈现渲染画面的
  if (supportsPresent[graphicsIndex] == VK_TRUE) {
    presentIndex = graphicsIndex;
    return true;
  }
  for (uint32_t i = 0; i < queueFamilyCount; i++) {
    if (supportsPresent[i] == VK_TRUE) {
      presentIndex = i;
      return false;
    }
  }
  presentIndex = -1;
  return false;
}

bool VKPhysDevWrapper::getMemoryTypeIndex(uint32_t typeBits,
                                          VkFlags quirementsMaks,
                                          uint32_t& index) {
  // 遍历所有显存类型
  for (uint32_t i = 0; i < mempryProperties.memoryTypeCount; i++) {
    // typeBits对应显存类型当前位是否可用(限制最多只有32位显存类型)
    if ((typeBits & 1) == 1) {
      if (havaFlags(mempryProperties.memoryTypes[i].propertyFlags,
                    quirementsMaks)) {
        index = i;
        return true;
      }
    }
    typeBits >>= 1;
  }
  return false;
}

bool VKPhysDevWrapper::bInterpDx11() {
  // 检测是否支持dx11交互
#ifdef _WIN32
  VkPhysicalDeviceExternalImageFormatInfo
      PhysicalDeviceExternalImageFormatInfo = {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
  PhysicalDeviceExternalImageFormatInfo.handleType =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
  VkPhysicalDeviceImageFormatInfo2 PhysicalDeviceImageFormatInfo2 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
  PhysicalDeviceImageFormatInfo2.pNext = &PhysicalDeviceExternalImageFormatInfo;
  PhysicalDeviceImageFormatInfo2.format = VK_FORMAT_R8G8B8A8_UNORM;
  PhysicalDeviceImageFormatInfo2.type = VK_IMAGE_TYPE_2D;
  PhysicalDeviceImageFormatInfo2.tiling = VK_IMAGE_TILING_OPTIMAL;
  PhysicalDeviceImageFormatInfo2.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  VkExternalImageFormatProperties ExternalImageFormatProperties = {
      VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
  VkImageFormatProperties2 ImageFormatProperties2 = {
      VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
  ImageFormatProperties2.pNext = &ExternalImageFormatProperties;
  VkResult result = vkGetPhysicalDeviceImageFormatProperties2(
      physicalDevice, &PhysicalDeviceImageFormatInfo2, &ImageFormatProperties2);
  bool bInterp = result == VK_SUCCESS;
  bInterp &= (ExternalImageFormatProperties.externalMemoryProperties
                  .externalMemoryFeatures &
              VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) > 0;
  bInterp &= (ExternalImageFormatProperties.externalMemoryProperties
                  .externalMemoryFeatures &
              VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) > 0;
  bInterp &= (ExternalImageFormatProperties.externalMemoryProperties
                  .compatibleHandleTypes &
              VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT) > 0;
  return bInterp;
#endif
  return false;
}

bool VKPhysDevWrapper::bInterpAndroid() {
#ifdef __ANDROID__
  bool bExt = findExtension(
      VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME);
  return bExt;
#endif
  return false;
}

bool VKPhysDevWrapper::bInterpMetal() {
#ifdef __APPLE__
  bool bExt = findExtension(VK_EXT_METAL_OBJECTS_EXTENSION_NAME);
  return bExt;
#endif
  return false;
}

#ifdef AVOX_ENABLE_VULKAN_DECODE
uint32_t VKPhysDevWrapper::getYcbcrConversionCount(VkFormat format) {
  VkSamplerYcbcrConversionImageFormatProperties
      samplerYcbcrConversionImageFormatProperties = {
          VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES};

  VkImageFormatProperties2 imageFormatProperties = {
      VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
      &samplerYcbcrConversionImageFormatProperties};

  const VkPhysicalDeviceImageFormatInfo2 imageFormatInfo =
      VkPhysicalDeviceImageFormatInfo2{
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
          nullptr,
          format,
          VK_IMAGE_TYPE_2D,
          VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_SAMPLED_BIT,
          0};

  VkResult result = vkGetPhysicalDeviceImageFormatProperties2(
      physicalDevice, &imageFormatInfo, &imageFormatProperties);
  if (result != VK_SUCCESS) {
    log(LogLevel::info, "getYcbcrConversionCount failed");
  }
  return samplerYcbcrConversionImageFormatProperties
      .combinedImageSamplerDescriptorCount;
}
#endif

bool VKPhysDevWrapper::findExtension(const char* name) const {
  for (const auto& extPorperty : exProps) {
    if (strcmp(extPorperty.extensionName, name) == 0) {
      return true;
    }
  }
  return false;
}

VkDeviceArgs VkDeviceArgs::defArgs(const VKPhysDevWrapper& phyDevice) {
  VkDeviceArgs agrs = {};
  if (phyDevice.graphicsIndexs.size() > 0) {
    agrs.graphicsIndex = phyDevice.graphicsIndexs[0];
  }
  if (phyDevice.computeIndexs.size() > 0) {
    agrs.computeIndex = phyDevice.computeIndexs[0];
  }
#ifdef AVOX_ENABLE_VULKAN_DECODE
  if (phyDevice.decodeIndexs.size() > 0) {
    agrs.decodeIndex = phyDevice.decodeIndexs[0];
    // 检查支持的解码器,后续改进选择范围最大的
    for (int32_t i = 0; i < phyDevice.decodeIndexs.size(); i++) {
      int32_t decodeIndex = phyDevice.decodeIndexs[i];
      int32_t decodeCodecs =
          phyDevice.videoQueues[decodeIndex].videoCodecOperations;
      if (!havaFlags(decodeCodecs,
                     VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR)) {
        log(LogLevel::warn, "vk decode not support h264");
      }
      if (!havaFlags(decodeCodecs,
                     VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR)) {
        log(LogLevel::warn, "vk decode not support h265");
      }
      if (!havaFlags(decodeCodecs,
                     VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR)) {
        log(LogLevel::warn, "vk decode not support av1");
      }
    }
  }
  if (phyDevice.encodeIndexs.size() > 0) {
    agrs.encodeIndex = phyDevice.encodeIndexs[0];
  }
  if (phyDevice.transferIndexs.size() > 0) {
    agrs.transferIndex = phyDevice.transferIndexs[0];
  }
#endif
  agrs.extensions = {
      {VK_KHR_SWAPCHAIN_EXTENSION_NAME},
#ifdef WIN32
      {VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME},
      {VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME},
      {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME},
      {VK_KHR_BIND_MEMORY_2_EXTENSION_NAME},
      {VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME},
      {VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME},
      {VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME},
      {VK_KHR_EXTERNAL_FENCE_WIN32_EXTENSION_NAME},
#elif __ANDROID__
      // 和android里的AHardwareBuffer交互,没有的话,相关vkGetDeviceProcAddr获取不到对应函数
      {VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME},
      {VK_KHR_BIND_MEMORY_2_EXTENSION_NAME},
      {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME},
      {VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME},
      {VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME},
#endif
  };
#ifdef AVOX_ENABLE_VULKAN_DECODE
  if (agrs.decodeIndex >= 0) {
    // 解码需要的扩展特性
    agrs.extensions.push_back({VK_KHR_VIDEO_QUEUE_EXTENSION_NAME});
    agrs.extensions.push_back({VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME});
  }
#endif
  return agrs;
}

VkDevice VkDeviceArgs::crateDevice(const VKPhysDevWrapper& phyDevice) {
  VkDeviceCreateInfo devInfo = {};
  devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  devInfo.pNext = nullptr;
  devInfo.queueCreateInfoCount = 0;
  // 不设优先级
  float queuePriorities[1] = {0.0};
  // 队列创建
  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  // 是否需要解码
  std::vector<int32_t> indexs;
#ifdef AVOX_ENABLE_VULKAN_DECODE
  if (decodeIndex >= 0) {
    // 直接从videoQueues里拿，这里最大的支持可能
    decodeCodecs = phyDevice.videoQueues[decodeIndex].videoCodecOperations;
    VkPhysicalDeviceVideoMaintenance1FeaturesKHR videoMaintenance1Features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_1_FEATURES_KHR,
        nullptr, false};
    VkPhysicalDeviceSynchronization2Features synchronization2Features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
        &videoMaintenance1Features, false};
    VkPhysicalDeviceFeatures2 deviceFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        &synchronization2Features};
    vkGetPhysicalDeviceFeatures2(phyDevice.physicalDevice, &deviceFeatures);
    devInfo.pNext = &deviceFeatures;
    indexs.push_back(decodeIndex);
  }
#endif
  if (graphicsIndex >= 0) {
    // 检查graphicsIndex是否已经在indexs中，如果不存在则添加
    auto it = std::find(indexs.begin(), indexs.end(), graphicsIndex);
    if (it == indexs.end()) {
      indexs.push_back(graphicsIndex);
    }
  }
  if (computeIndex >= 0) {
    // 检查computeIndex是否已经在indexs中，如果不存在则添加
    auto it = std::find(indexs.begin(), indexs.end(), computeIndex);
    if (it == indexs.end()) {
      indexs.push_back(computeIndex);
    }
  }
  if (transferIndex >= 0) {
    // 检查transferIndex是否已经在indexs中，如果不存在则添加
    auto it = std::find(indexs.begin(), indexs.end(), transferIndex);
    if (it == indexs.end()) {
      indexs.push_back(transferIndex);
    }
  }
  for (int32_t index : indexs) {
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = index;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = queuePriorities;
    queueCreateInfos.push_back(queueInfo);
  }
  devInfo.pQueueCreateInfos = queueCreateInfos.data();
  devInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());

  // 过滤设备不支持的扩展
  auto devExtIt = extensions.begin();
  while (devExtIt != extensions.end()) {
    const char* ext = *devExtIt;
    if (!phyDevice.findExtension(ext)) {
      log(LogLevel::warn, "device extensions not support:", ext);
      devExtIt = extensions.erase(devExtIt);
    } else {
      ++devExtIt;
    }
  }
  devInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  devInfo.ppEnabledExtensionNames = extensions.data();
  VkDevice device = VK_NULL_HANDLE;
  AVOX_VULKAN_LOG(
      vkCreateDevice(phyDevice.physicalDevice, &devInfo, nullptr, &device),
      "create device failed");
  volkLoadDevice(device);
  return device;
}

void VKDeviceWrapper::form(VkDevice device_) { device = device_; }
}
