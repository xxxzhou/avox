#include "VkWinImage.hpp"

#include <vulkan/vulkan_win32.h>

namespace avox {

int32_t getMemoryTypeIndex(uint32_t bits) {
  for (uint32_t memoryTypeIndex = 0; (1u << memoryTypeIndex) <= bits;
       memoryTypeIndex++) {
    if ((bits & (1u << memoryTypeIndex)) != 0) {
      return memoryTypeIndex;
    }
  }
  return -1;
}

VkWinImage::VkWinImage(/* args */) {
  shardTex = std::make_unique<Dx11SharedTex>();
}

VkWinImage::~VkWinImage() { release(); }

void VkWinImage::release() {
  bInit = false;
#if defined(VK_KHR_external_fence_win32)
  if (vkExternalFence) {
    vkDestroyFence(vkDevice, vkExternalFence, nullptr);
    vkExternalFence = VK_NULL_HANDLE;
  }
  vkFenceHandle = nullptr;
#endif
  if (vkImage) {
    vkDestroyImage(vkDevice, vkImage, nullptr);
    vkImage = VK_NULL_HANDLE;
  }
  if (memory) {
    vkFreeMemory(vkDevice, memory, nullptr);
    memory = VK_NULL_HANDLE;
  }
}

void VkWinImage::bindD3D(ID3D11Device* device, ImageFormat format_) {
  release();
  if (interopType == InteropType::none) {
    return;
  }
#ifdef VK_KHR_external_memory_win32
  vkGetMemoryWin32HandleKHR = reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(
      vkGetDeviceProcAddr(vkDevice, "vkGetMemoryWin32HandleKHR"));
  vkGetMemoryWin32HandlePropertiesKHR =
      reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
          vkGetDeviceProcAddr(vkDevice, "vkGetMemoryWin32HandlePropertiesKHR"));
#endif
  format = format_;
  DXGI_FORMAT dxFormat = getImageDXFormt(format.imageType);
  Dx11SharedTex* interopTex = nullptr;
  if (interopType == InteropType::inputNt ||
      interopType == InteropType::outputNt) {
    if (!exShardTex) {
      return;
    }
    exShardTex->setInteropDevice(device);
    interopTex = exShardTex;
#if defined(VK_KHR_external_fence_win32)
    // 初始化外部 fence（从 DX11 导入）
    HANDLE interopFenceHandle = exShardTex->getInteropFenceHandle();
    if (interopFenceHandle) {
      // if (!initExternalFence(interopFenceHandle)) {
      //   LOGFLF(LogLevel::warn, "failed to init external fence");
      // }
    }
#endif
  } else if (interopType == InteropType::input ||
             interopType == InteropType::output) {
    Dx11Texture* wstex = shardTex->getDx11Texture();
    wstex->setTextureSize(format.width, format.height, dxFormat);
    bool bInitRes = shardTex->initTexture(device);
    if (!bInitRes) {
      return;
    }
    interopTex = shardTex.get();
  }
  VkExternalMemoryHandleTypeFlagBits handleType =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
  // 创建对应上面的vulkan资源
  VkExternalMemoryImageCreateInfo externalCreateInfo = {
      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
  externalCreateInfo.handleTypes = handleType;
  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.pNext = &externalCreateInfo;
  imageInfo.flags = 0u;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = getVkFormat(format.imageType);
  imageInfo.extent = {
      (uint32_t)format.width,
      (uint32_t)format.height,
      1u,
  };
  imageInfo.mipLevels = 1u, imageInfo.arrayLayers = 1u;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  // VK_IMAGE_LAYOUT_UNDEFINED VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
  // VK_IMAGE_LAYOUT_GENERAL
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  AVOX_VULKAN_LOG(vkCreateImage(vkDevice, &imageInfo, nullptr, &vkImage),
                 "create image failed");
  // 创建从DX11纹理中的ImageMemory,绑定到上面的vkImage.
  const VkImageMemoryRequirementsInfo2 requirementsInfo = {
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
      nullptr,
      vkImage,
  };
  VkMemoryDedicatedRequirements dedicatedRequirements = {
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
      nullptr,
      VK_FALSE,
      VK_FALSE,
  };
  VkMemoryRequirements2 requirements = {
      VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
      &dedicatedRequirements,
      {
          0u,
          0u,
          0u,
      },
  };
  // 在Zbook studio G8会crash,换成vkGetImageMemoryRequirements
  VkMemoryWin32HandlePropertiesKHR memoryWin32HandleProperties = {
      VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR, nullptr, 0u};
  vkGetMemoryWin32HandlePropertiesKHR(vkDevice, handleType,
                                      interopTex->sharedHandle,
                                      &memoryWin32HandleProperties);
  VkMemoryRequirements rement = {};
  vkGetImageMemoryRequirements(vkDevice, vkImage, &rement);
  // 后面需要搞清楚这里的memoryTypeBits具体信息,如何影响VK分配
  uint32_t memoryBit =
      rement.memoryTypeBits & memoryWin32HandleProperties.memoryTypeBits;
  assert(memoryBit != 0);
  uint32_t memoryTypeIndex = getMemoryTypeIndex(memoryBit);
  assert(memoryTypeIndex >= 0);
  // create image memory
  VkMemoryDedicatedAllocateInfo dedicatedInfo = {
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
  dedicatedInfo.image = vkImage;
  VkImportMemoryWin32HandleInfoKHR importInfo = {
      VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
  if (!!dedicatedRequirements.requiresDedicatedAllocation) {
    importInfo.pNext = &dedicatedInfo;
  }
  importInfo.handleType = handleType;
  importInfo.handle = interopTex->sharedHandle;
  VkMemoryAllocateInfo memoryInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  memoryInfo.pNext = &importInfo;
  memoryInfo.allocationSize = rement.size;
  memoryInfo.memoryTypeIndex = memoryTypeIndex;
  AVOX_VULKAN_LOG(vkAllocateMemory(vkDevice, &memoryInfo, nullptr, &memory),
                 "allocate memory failed");
  VkBindImageMemoryInfo BindImageMemoryInfo = {
      VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO};
  BindImageMemoryInfo.image = vkImage;
  BindImageMemoryInfo.memory = memory;
  // AVOX_VULKAN_LOG(vkBindImageMemory2(vkDevice, 1, &BindImageMemoryInfo));
  AVOX_VULKAN_LOG(vkBindImageMemory(vkDevice, vkImage, memory, 0),
                 "bind memory failed");
  bInit = true;
}

void VkWinImage::updateInputContext(IRenderContext* context) {
  RenderType renderType = context->getRenderType();
  if (renderType == RenderType::D3D11) {
    IDx11Context* dx11Context = (IDx11Context*)context;
    if (dx11Context->bInteropTexture()) {
      Dx11SharedTex* temp = (Dx11SharedTex*)dx11Context;
      if (temp != exShardTex) {
        exShardTex = temp;
        bInit = false;
      }
      interopType = InteropType::inputNt;
    } else {
      interopType = InteropType::input;
    }
  }
  // 如何的如果本身就是共享纹理,则不处理
  if (interopType != InteropType::input) {
    return;
  }
  if (bInit && shardTex) {
    // 把context的结果给shardTex
    shardTex->interopTexture(context, false);
    shardTex->signalInteropFence();
  }
}

void VkWinImage::updateOutputContext(IRenderContext* context) {
  RenderType renderType = context->getRenderType();
  if (renderType == RenderType::D3D11) {
    IDx11Context* dx11Context = (IDx11Context*)context;
    if (dx11Context->bInteropTexture()) {
      Dx11SharedTex* temp = (Dx11SharedTex*)dx11Context;
      if (temp != exShardTex) {
        exShardTex = temp;
        bInit = false;
      }
      interopType = InteropType::outputNt;
    } else {
      interopType = InteropType::output;
    }
  }
  if (interopType != InteropType::output) {
    return;
  }
  if (bInit && shardTex) {
    // 把shardTex的结果给context
    shardTex->interopTexture(context, true);
    shardTex->signalInteropFence();
  }
}

void VkWinImage::signalFence() {
  if (!bInit) {
    return;
  }
  if (interopType == InteropType::inputNt ||
      interopType == InteropType::outputNt) {
    if (exShardTex) {
      exShardTex->signalInteropFence();
    }
  } else if (interopType == InteropType::input ||
             interopType == InteropType::output) {
    if (shardTex) {
      shardTex->signalFence();
    }
  }
}

#if defined(VK_KHR_external_fence_win32)
bool VkWinImage::initExternalFence(HANDLE dx11FenceHandle) {
  // 加载扩展函数
  vkGetFenceWin32HandleKHR = reinterpret_cast<PFN_vkGetFenceWin32HandleKHR>(
      vkGetDeviceProcAddr(vkDevice, "vkGetFenceWin32HandleKHR"));
  vkImportFenceWin32HandleKHR =
      reinterpret_cast<PFN_vkImportFenceWin32HandleKHR>(
          vkGetDeviceProcAddr(vkDevice, "vkImportFenceWin32HandleKHR"));

  if (!vkGetFenceWin32HandleKHR || !vkImportFenceWin32HandleKHR) {
    LOGFLF(LogLevel::warn, "failed to load external fence extension functions");
    return false;
  }
  // 创建用于导入的 fence（不需要 export 标志）
  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.pNext = nullptr;
  fenceInfo.flags = 0;
  VkResult result =
      vkCreateFence(vkDevice, &fenceInfo, nullptr, &vkExternalFence);
  if (result != VK_SUCCESS) {
    LOGFLF(LogLevel::warn, "failed to create external fence:", result);
    return false;
  }
  // 导入 DX11 的 fence 句柄
  VkImportFenceWin32HandleInfoKHR importInfo = {};
  importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_WIN32_HANDLE_INFO_KHR;
  importInfo.fence = vkExternalFence;
  importInfo.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
  importInfo.handle = dx11FenceHandle;
  importInfo.flags = 0;
  result = vkImportFenceWin32HandleKHR(vkDevice, &importInfo);
  if (result != VK_SUCCESS) {
    LOGFLF(LogLevel::warn, "failed to import fence handle:", result);
    vkDestroyFence(vkDevice, vkExternalFence, nullptr);
    vkExternalFence = VK_NULL_HANDLE;
    return false;
  }
  vkFenceHandle = dx11FenceHandle;
  LOGFLF(LogLevel::info, "successfully imported external fence handle");
  return true;
}
#endif

}