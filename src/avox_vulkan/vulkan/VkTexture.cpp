#include "VkTexture.hpp"

namespace avox {

VkTexture::VkTexture() {}

VkTexture::~VkTexture() {
  if (sampler) {
    vkDestroySampler(vkDevice, sampler, nullptr);
    sampler = VK_NULL_HANDLE;
  }
  if (view) {
    vkDestroyImageView(vkDevice, view, nullptr);
    view = VK_NULL_HANDLE;
  }
  if (image) {
    vkDestroyImage(vkDevice, image, nullptr);
    image = VK_NULL_HANDLE;
  }
  if (memory) {
    vkFreeMemory(vkDevice, memory, nullptr);
    memory = VK_NULL_HANDLE;
  }
}

void VkTexture::InitResource(uint32_t width, uint32_t height, VkFormat format,
                             VkImageUsageFlags usageFlag,
                             VkMemoryPropertyFlags memoryFlag, uint8_t* cpuData,
                             uint8_t cpuPitch) {
  bool bGpu = cpuData == nullptr;
  this->width = width;
  this->height = height;
  this->format = format;
  // 重新初始化
  layout = VK_IMAGE_LAYOUT_UNDEFINED;
  accessFlags = 0;
  if (!bGpu) {
    // 初始化只能UNDEFINED/PREINITIALIZED,PREINITIALIZED可以用内存数据初始化
    layout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    accessFlags = VK_ACCESS_HOST_WRITE_BIT;
  }
  stageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  // SRV,UVA资源一般不支持LINEAR格式,LINEAR限制较大,请先检查是否支持相应UsageFlag
  VkImageTiling tiling =
      bGpu ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR;

  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.flags = 0;
  imageInfo.pNext = nullptr;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = format;  // 一般VK_FORMAT_R8G8B8A8_UNORM
  imageInfo.extent = {width, height, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = tiling;
  // VK_IMAGE_LAYOUT_PREINITIALIZED 内存数据初始化,可以直接存储在设备内存
  imageInfo.initialLayout = layout;
  imageInfo.usage = usageFlag;  // usageFlag;
  // 一般来说,我们只需要单队列访问 VK_SHARING_MODE_EXCLUSIVE,故为0
  imageInfo.queueFamilyIndexCount = 0;
  imageInfo.pQueueFamilyIndices = nullptr;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  // 创建image
  AVOX_VULKAN_LOG(vkCreateImage(vkDevice, &imageInfo, nullptr, &image),
                 "create image fail");
  VkMemoryRequirements meoryRequires = {};
  vkGetImageMemoryRequirements(vkDevice, image, &meoryRequires);
  uint32_t memoryTypeIndex = 0;
  bool getIndex = wphyDevcie->getMemoryTypeIndex(meoryRequires.memoryTypeBits,
                                                 memoryFlag, memoryTypeIndex);
  // assert(getIndex);
  if (!getIndex) {
    log(LogLevel::info, "get memory type ", memoryFlag,
        " index fail,change to 0");
    getIndex = wphyDevcie->getMemoryTypeIndex(meoryRequires.memoryTypeBits, 0,
                                              memoryTypeIndex);
    assert(getIndex);
  }
  VkMemoryAllocateInfo memoryInfo = {};
  memoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memoryInfo.pNext = nullptr;
  memoryInfo.memoryTypeIndex = memoryTypeIndex;
  memoryInfo.allocationSize = meoryRequires.size;
  // 生成设备显存空间
  AVOX_VULKAN_LOG(vkAllocateMemory(vkDevice, &memoryInfo, nullptr, &memory),
                 "allocate memory fail");
  AVOX_VULKAN_LOG(vkBindImageMemory(vkDevice, image, memory, 0),
                 "bind image memory fail");
  // vkGetImageSubresourceLayout 只支持 LINEAR tiling
  // 直接传入cpu数据到image,需要检查image相应的rowPitch
  // 可以考虑使用buffer,然后vkCmdCopyBufferToImage
  pixelSize = vkPixelSize(format);
  VkSubresourceLayout subresourceLayout = {};
  if (tiling == VK_IMAGE_TILING_LINEAR) {
    VkImageSubresource subres = {};
    // 对于深度纹理,使用 DEPTH_BIT 而不是 COLOR_BIT
    subres.aspectMask =
        (usageFlag == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
            ? VK_IMAGE_ASPECT_DEPTH_BIT
            : VK_IMAGE_ASPECT_COLOR_BIT;
    subres.mipLevel = 0;
    subres.arrayLayer = 0;
    vkGetImageSubresourceLayout(vkDevice, image, &subres, &subresourceLayout);
  } else {
    // OPTIMAL tiling 下,直接计算 rowPitch
    rowPitch = width * pixelSize;    
  }
  // DEVICE_LOCAL 内存不能映射到 CPU，深度纹理不应该有 cpuData
  if (cpuData && (memoryFlag & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0) {
    uint8_t* pData = nullptr;
    AVOX_VULKAN_LOG(
        vkMapMemory(vkDevice, memory, 0, meoryRequires.size, 0, (void**)&pData),
        "map memory fail");
    // uint8_t cpuPitch = width * getByteSize(format);
    if (cpuPitch != 0 && cpuPitch != rowPitch) {
      assert(rowPitch >= cpuPitch);
      for (uint32_t i = 0; i < height; i++) {
        memcpy(pData + rowPitch * i, cpuData + i * cpuPitch, cpuPitch);
      }
    } else {
      memcpy(pData, cpuData, meoryRequires.size);
    }
    // 这段逻辑需要再考虑下
    if ((memoryFlag & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
      VkMappedMemoryRange memRange;
      memRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      memRange.pNext = nullptr;
      memRange.memory = memory;
      memRange.offset = 0;
      memRange.size = meoryRequires.size;
      AVOX_VULKAN_LOG(vkFlushMappedMemoryRanges(vkDevice, 1, &memRange),
                     "flush memory fail");
    }
    vkUnmapMemory(vkDevice, memory);
  }
  bool bDepth = usageFlag == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  // 创建view
  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.pNext = nullptr;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
  viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
  viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
  viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
  viewInfo.subresourceRange.aspectMask =
      bDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;
  AVOX_VULKAN_LOG(vkCreateImageView(vkDevice, &viewInfo, nullptr, &view),
                 "create view fail");
  // 填充VkDescriptorImageInfo
  descInfo.imageView = view;
  descInfo.sampler = VK_NULL_HANDLE;
  // 渲染管线一般用VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,计算管线用general
  descInfo.imageLayout = layout;
}

void VkTexture::createSampler(bool bLinear) {
  // 创建sampler
  VkSamplerCreateInfo samplerCreateInfo = {};
  samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerCreateInfo.magFilter =
      bLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  samplerCreateInfo.minFilter =
      bLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
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
  AVOX_VULKAN_LOG(
      vkCreateSampler(vkDevice, &samplerCreateInfo, nullptr, &sampler),
      "create sampler fail");
  descInfo.sampler = sampler;
}

void VkTexture::addBarrier(VkCommandBuffer command, VkImageLayout newLayout,
                           VkPipelineStageFlags newStageFlags,
                           VkAccessFlags newAccessFlags) {
  VkImageLayout oldLayout = layout;
  VkPipelineStageFlags oldStageFlags = stageFlags;
  // 根据格式判断是否为深度纹理
  bool isDepth =
      (format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D16_UNORM_S8_UINT ||
       format == VK_FORMAT_D24_UNORM_S8_UINT ||
       format == VK_FORMAT_D32_SFLOAT ||
       format == VK_FORMAT_D32_SFLOAT_S8_UINT);
  VkImageAspectFlags aspectMask =
      isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

  changeLayout(command, image, oldLayout, newLayout, oldStageFlags,
               newStageFlags, aspectMask, newAccessFlags);
  layout = newLayout;
  stageFlags = newStageFlags;
  accessFlags = newAccessFlags;
  // 同步 descInfo 中的布局
  descInfo.imageLayout = newLayout;
}

}
