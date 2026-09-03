#include "VkDecoder.hpp"
#include "avox/player/VideoTrack.hpp"

namespace avox {

void setStdHeader(VkVideoCodecOperationFlagBitsKHR vkcoderId,
                  VkVideoSessionCreateInfoKHR &createInfo) {
  static const VkExtensionProperties h264DecodeStdExtensionVersion = {
      VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME,
      VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION};
  static const VkExtensionProperties h265DecodeStdExtensionVersion = {
      VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME,
      VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION};
  static const VkExtensionProperties av1DecodeStdExtensionVersion = {
      VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_EXTENSION_NAME,
      VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_SPEC_VERSION};
  static const VkExtensionProperties h264EncodeStdExtensionVersion = {
      VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_EXTENSION_NAME,
      VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_SPEC_VERSION};
  static const VkExtensionProperties h265EncodeStdExtensionVersion = {
      VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_EXTENSION_NAME,
      VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_SPEC_VERSION};
  // static const VkExtensionProperties av1EncodeStdExtensionVersion = {
  //     VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_EXTENSION_NAME,
  //     VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_SPEC_VERSION};
  switch (vkcoderId) {
    case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
      createInfo.pStdHeaderVersion = &h264DecodeStdExtensionVersion;
      break;
    case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
      createInfo.pStdHeaderVersion = &h265DecodeStdExtensionVersion;
      break;
    case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR:
      createInfo.pStdHeaderVersion = &av1DecodeStdExtensionVersion;
      break;
    case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      createInfo.pStdHeaderVersion = &h264EncodeStdExtensionVersion;
      break;
    case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      createInfo.pStdHeaderVersion = &h265EncodeStdExtensionVersion;
      break;
    // case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR:
    //   createInfo.pStdHeaderVersion = &av1EncodeStdExtensionVersion;
    //   break;
    default:
      assert(0);
  }
}

VkDecoder::VkDecoder() {}

VkDecoder::~VkDecoder() {}

bool VkDecoder::onVaild() {
  bVulkanInit = false;
  if (!vkInstance) {
    setVkContext(VkContext::Shared());
  }
  if (!vkInstance) {   
    return false;
  }
  // 检查是否支持该解码器
  VkVideoCodecOperationFlagsKHR flags = getDeviceArgs()->getDecodeCodecs();
  vkCodecId = getVkCodec(trackContext->getCodecId());
  // 当前VK硬解是否支持该解码器
  if (!havaFlags(flags, vkCodecId)) {
    LOGFLF(LogLevel::warn, "vulkan not support codec: ",
           getVCodecName(trackContext->getCodecId()));   
    return false;
  }
  if (!vkInstance || !vkPhyDevice) {
    LOGFLF(LogLevel::warn, "vkcontext not init.");
  }
  return true;
}

bool VkDecoder::getVideoProfile() {
  // profileInfo 在子类被填充
  VkVideoDecodeCapabilitiesKHR videoDecodeCapabilities = {
      VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR};
  if (vkCodecId == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
    VkVideoDecodeH264CapabilitiesKHR h264Capabilities = {
        VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR, nullptr};
    videoDecodeCapabilities.pNext = &h264Capabilities;
  }
  if (vkCodecId == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
    VkVideoDecodeH264CapabilitiesKHR h265Capabilities = {
        VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_CAPABILITIES_KHR, nullptr};
    videoDecodeCapabilities.pNext = &h265Capabilities;
  }
  videoCapabilities = {VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR,
                       &videoDecodeCapabilities};
  // 调用GetPhysicalDeviceVideoCapabilitiesKHR
  AVOX_AVOX_VK_INST_FUNC_IMPL_INIT(vkInstance, GetPhysicalDeviceVideoCapabilitiesKHR)
  // 获取设备解码的能力属性
  VkResult result = GetPhysicalDeviceVideoCapabilitiesKHR(
      vkPhyDevice, &profileInfo, &videoCapabilities);
  AVOX_VULKAN_LOG_RETURN_FALSE(result, "get video capabilities fail.");
  log(LogLevel::info,
      "vulkan init decoder codeId:", getVCodecName(trackContext->getCodecId()),
      " minWidth:", videoCapabilities.minCodedExtent.width,
      " minHeigth:", videoCapabilities.minCodedExtent.height,
      " maxWidth:", videoCapabilities.maxCodedExtent.width,
      " maxHeigth:", videoCapabilities.maxCodedExtent.height);
  VkVideoDecodeCapabilityFlagsKHR capabilityFlags =
      (*(VkVideoDecodeCapabilitiesKHR *)videoCapabilities.pNext).flags;
  // COINCIDE 解码缓冲区和输出缓冲区是重合的
  // DISTINCT 解码缓冲区和输出缓冲区是独立的,解码先在DPB中,然后拷贝到输出缓冲区
  bDistinct = !(capabilityFlags &
                VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR);
  return true;
}

bool VkDecoder::getVideoFormat() {
  // 独立的只需要DPB就行,合并需要能复制
  VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
  // if (!bDistinct) {
  //   usageFlags = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR |
  //                VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR |
  //                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
  //                VK_IMAGE_USAGE_SAMPLED_BIT;
  // }
  const VkVideoProfileListInfoKHR videoProfiles = {
      VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR, nullptr, 1, &profileInfo};
  VkPhysicalDeviceVideoFormatInfoKHR formatInfo = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR,
      const_cast<VkVideoProfileListInfoKHR *>(&videoProfiles), usageFlags};
  // 获取支持的vkformat的个数
  uint32_t supportedFormatCount = 0;
  AVOX_AVOX_VK_INST_FUNC_IMPL_INIT(vkInstance, GetPhysicalDeviceVideoFormatPropertiesKHR)
  VkResult result = GetPhysicalDeviceVideoFormatPropertiesKHR(
      vkPhyDevice, &formatInfo, &supportedFormatCount, nullptr);
  AVOX_VULKAN_LOG_RETURN_FALSE(result, "get video format properties count fail.");
  // 获取支持的vkformat的信息
  std::vector<VkVideoFormatPropertiesKHR> pSupportedFormats(
      supportedFormatCount);
  for (uint32_t i = 0; i < supportedFormatCount; i++) {
    pSupportedFormats[i].sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;
  }
  result = GetPhysicalDeviceVideoFormatPropertiesKHR(vkPhyDevice, &formatInfo,
                                                     &supportedFormatCount,
                                                     pSupportedFormats.data());
  AVOX_VULKAN_LOG_RETURN_FALSE(result, "get video format properties fail.");
  // 选择第一个
  selectFormat = pSupportedFormats[0].format;
  // 如果太小，放大到解码支持的最小尺寸
  imageExtent.width =
      std::max(imageExtent.width, videoCapabilities.minCodedExtent.width);
  imageExtent.height =
      std::max(imageExtent.height, videoCapabilities.minCodedExtent.height);
  // 调整对齐
  uint32_t alignWidth = videoCapabilities.pictureAccessGranularity.width - 1;
  imageExtent.width = ((imageExtent.width + alignWidth) & ~alignWidth);
  uint32_t alignHeight = videoCapabilities.pictureAccessGranularity.height - 1;
  imageExtent.height = ((imageExtent.height + alignHeight) & ~alignHeight);
  return true;
}

bool VkDecoder::createVideoSession() {
  // 创建session
  VkVideoSessionCreateInfoKHR createInfo = {
      VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR};
  createInfo.flags = 0x0;
  createInfo.pVideoProfile = &profileInfo;
  createInfo.queueFamilyIndex = getDeviceArgs()->decodeIndex;
  createInfo.pictureFormat = selectFormat;
  createInfo.referencePictureFormat = selectFormat;
  createInfo.maxCodedExtent = {imageExtent.width, imageExtent.height};
  createInfo.maxDpbSlots = videoCapabilities.maxDpbSlots - 1;
  createInfo.maxActiveReferencePictures = std::min(
      createInfo.maxDpbSlots, videoCapabilities.maxActiveReferencePictures);

  setStdHeader(vkCodecId, createInfo);
  AVOX_AVOX_VK_INST_FUNC_IMPL_INIT(vkInstance, CreateVideoSessionKHR)
  VkResult result =
      CreateVideoSessionKHR(vkDevice, &createInfo, nullptr, &videoSession);
  AVOX_VULKAN_LOG_RETURN_FALSE(result, "create video session fail.");

  return true;
}

bool VkDecoder::memoryVideoSession() {
  memoryRequirements = 0;
  AVOX_AVOX_VK_INST_FUNC_IMPL_INIT(vkInstance, GetVideoSessionMemoryRequirementsKHR)
  VkResult result = GetVideoSessionMemoryRequirementsKHR(
      vkDevice, videoSession, &memoryRequirements, nullptr);
  AVOX_VULKAN_LOG_RETURN_FALSE(result,
                           "get video session memory requirements fail.");
  // 如果是集成显卡,memoryRequirements为0是正常的
  if (memoryRequirements > 0) {
    std::vector<VkVideoSessionMemoryRequirementsKHR>
        decodeSessionMemoryRequirements(memoryRequirements);
    for (uint32_t i = 0; i < memoryRequirements; i++) {
      decodeSessionMemoryRequirements[i].sType =
          VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;
    }
    result = GetVideoSessionMemoryRequirementsKHR(
        vkDevice, videoSession, &memoryRequirements,
        decodeSessionMemoryRequirements.data());
    AVOX_VULKAN_LOG_RETURN_FALSE(result,
                             "get video session memory requirements fail.");
    memoryBounds.resize(memoryRequirements);
    std::vector<VkBindVideoSessionMemoryInfoKHR> decodeSessionBindMemory(
        memoryRequirements);
    uint32_t memoryTypeIndex = 0;
    for (uint32_t memIdx = 0; memIdx < memoryRequirements; memIdx++) {
      uint32_t memoryTypeBits = decodeSessionMemoryRequirements[memIdx]
                                    .memoryRequirements.memoryTypeBits;
      if (memoryTypeBits == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
      }
      // Find an available memory type that satisfies the requested properties.
      for (; !(memoryTypeBits & 1); memoryTypeIndex++) {
        memoryTypeBits >>= 1;
      }

      VkMemoryAllocateInfo memInfo = {
          VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
          NULL,
          decodeSessionMemoryRequirements[memIdx].memoryRequirements.size,
          memoryTypeIndex,
      };

      result = vkAllocateMemory(vkDevice, &memInfo, 0, &memoryBounds[memIdx]);
      AVOX_VULKAN_LOG_RETURN_FALSE(result, "allocate memory fail.");
      assert(result == VK_SUCCESS);
      decodeSessionBindMemory[memIdx].pNext = NULL;
      decodeSessionBindMemory[memIdx].sType =
          VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
      decodeSessionBindMemory[memIdx].memory = memoryBounds[memIdx];
      decodeSessionBindMemory[memIdx].memoryBindIndex =
          decodeSessionMemoryRequirements[memIdx].memoryBindIndex;
      decodeSessionBindMemory[memIdx].memoryOffset = 0;
      decodeSessionBindMemory[memIdx].memorySize =
          decodeSessionMemoryRequirements[memIdx].memoryRequirements.size;
    }
    AVOX_AVOX_VK_INST_FUNC_IMPL_INIT(vkInstance, BindVideoSessionMemoryKHR)
    result =
        BindVideoSessionMemoryKHR(vkDevice, videoSession, memoryRequirements,
                                  decodeSessionBindMemory.data());
    AVOX_VULKAN_LOG_RETURN_FALSE(result, "bind video session memory fail.");
  }
  return true;
}

bool VkDecoder::createFrameQueue() {
  // 解码缓冲区队列图像
  VkImageUsageFlags dpbImageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
  // 输出缓冲区队列图像
  VkImageUsageFlags outImageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
  // 解码与输出重合
  if (!bDistinct) {
    dpbImageUsage |= VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
  }
  // 使用CS把YUV转换成RGBA
  dpbImageUsage |= (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
  outImageUsage |= (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);

  VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imageInfo.pNext = nullptr;
  imageInfo.format = selectFormat;
  imageInfo.extent = imageExtent;
  // DPB缓冲
  imageInfo.arrayLayers = 16;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.mipLevels = 1;
  // 采样数
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  // 平铺方式
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = dpbImageUsage;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.queueFamilyIndexCount = 1;
  imageInfo.pQueueFamilyIndices = nullptr;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.flags = 0;

  return true;
}

bool VkDecoder::initVkDecoder() {
  bool bNext = getVideoProfile();
  if (bNext) {
    bNext = getVideoFormat();
  }
  if (bNext) {
    bNext = createVideoSession();
  }
  if (bNext) {
    bNext = memoryVideoSession();
  }
  if (bNext) {
    bNext = createFrameQueue();
  }
  return bNext;
}

bool VkDecoder::decode(const AvoxPacket & packet) {
  bool result = onParsePacket(packet);
  return result;
}

void VkDecoder::flush() {}

}
