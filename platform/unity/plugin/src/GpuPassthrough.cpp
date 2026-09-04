#include "GpuPassthrough.h"
#include "PlayerBridge.h"

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <volk.h>
#include <windows.h>

#include "unity/IUnityInterface.h"
#include "unity/IUnityGraphics.h"
#include "unity/IUnityGraphicsVulkan.h"
#include "unity/IUnityRenderingExtensions.h"

// ── Unity 插件接口 (UnityPluginLoad 自动调用) ──
static IUnityInterfaces* s_UnityInterfaces = nullptr;
static IUnityGraphics* s_Graphics = nullptr;
static IUnityGraphicsVulkan* s_GfxVulkan = nullptr;
static bool s_VolkReady = false;
static bool s_GpuAvailable = false;

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginLoad(IUnityInterfaces* unityInterfaces) {
  s_UnityInterfaces = unityInterfaces;
  s_Graphics = unityInterfaces->Get<IUnityGraphics>();
  s_GfxVulkan = unityInterfaces->Get<IUnityGraphicsVulkan>();
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload() {
  s_UnityInterfaces = nullptr;
  s_Graphics = nullptr;
  s_GfxVulkan = nullptr;
  s_VolkReady = false;
  s_GpuAvailable = false;
}

bool unityGpuPassthroughAvailable() {
  // 惰性初始化: 首次 avoxPlayerCreate→bindSurface 时 UnityPluginLoad 已跑完,
  // 渲染设备已就绪, 此处直接探测 Vulkan 后端 + 加载 volk (幂等)
  if (!s_VolkReady) unityVulkanInit();
  return s_GpuAvailable;
}

bool unityVulkanInit() {
  if (s_VolkReady) return s_GpuAvailable;
  s_VolkReady = true;
  if (!s_GfxVulkan || !s_Graphics) return false;
  // 仅 Unity Vulkan 后端支持直通 (D3D11/OpenGL 走 CPU 回退, 同 godot 需 Vulkan 后端)
  if (s_Graphics->GetRenderer() != kUnityGfxRendererVulkan) return false;
  if (volkInitialize() != VK_SUCCESS) return false;
  const UnityVulkanInstance vi = s_GfxVulkan->Instance();
  if (!vi.instance || !vi.device) return false;
  volkLoadInstance(vi.instance);
  volkLoadDevice(vi.device);
  s_GpuAvailable = true;
  return true;
}

bool unityImportSharedImage(uint64_t memHandle, int32_t w, int32_t h, uint64_t* outImage,
                            uint64_t* outMemory) {
  if (!s_GpuAvailable || !s_GfxVulkan) return false;
  if (!memHandle || !outImage || !outMemory || w <= 0 || h <= 0) return false;
  const UnityVulkanInstance vi = s_GfxVulkan->Instance();
  VkDevice device = vi.device;
  // 与 avox 导出端契约一致: R8G8B8A8_UNORM + TRANSFER_SRC|TRANSFER_DST,
  // usage 加 SAMPLED 供 Unity 直接采样 (usage 是 per-image 属性, 同 godot)
  VkExternalMemoryImageCreateInfo extMemImg = {
      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
  extMemImg.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
  VkImageCreateInfo importInfo = {};
  importInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  importInfo.pNext = &extMemImg;
  importInfo.imageType = VK_IMAGE_TYPE_2D;
  importInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  importInfo.extent = {(uint32_t)w, (uint32_t)h, 1};
  importInfo.mipLevels = 1;
  importInfo.arrayLayers = 1;
  importInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  importInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  importInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                     VK_IMAGE_USAGE_SAMPLED_BIT;
  importInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  importInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImage image = VK_NULL_HANDLE;
  if (vkCreateImage(device, &importInfo, nullptr, &image) != VK_SUCCESS) return false;
  VkMemoryRequirements memReqs = {};
  vkGetImageMemoryRequirements(device, image, &memReqs);
  VkImportMemoryWin32HandleInfoKHR importMemInfo = {
      VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
  importMemInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
  importMemInfo.handle = reinterpret_cast<HANDLE>(memHandle);
  VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.pNext = &importMemInfo;
  allocInfo.allocationSize = memReqs.size;
  // 内存类型: image bits ∩ handle bits (同 godot)
  uint32_t handleBits = 0xFFFFFFFFu;
  VkMemoryWin32HandlePropertiesKHR win32Props = {
      VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
  if (vkGetMemoryWin32HandlePropertiesKHR(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
                                          reinterpret_cast<HANDLE>(memHandle),
                                          &win32Props) == VK_SUCCESS) {
    handleBits = win32Props.memoryTypeBits;
  }
  uint32_t inter = memReqs.memoryTypeBits & handleBits;
  if (inter == 0) inter = memReqs.memoryTypeBits;
  uint32_t memoryTypeIndex = UINT32_MAX;
  for (uint32_t i = 0; i < 32; ++i) {
    if (inter & (1u << i)) {
      memoryTypeIndex = i;
      break;
    }
  }
  VkDeviceMemory memory = VK_NULL_HANDLE;
  allocInfo.memoryTypeIndex = memoryTypeIndex;
  if (memoryTypeIndex == UINT32_MAX ||
      vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
    vkDestroyImage(device, image, nullptr);
    return false;
  }
  if (vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
    vkFreeMemory(device, memory, nullptr);
    vkDestroyImage(device, image, nullptr);
    return false;
  }
  *outImage = (uint64_t)image;
  *outMemory = (uint64_t)memory;
  return true;
}

void unityReleaseImported(uint64_t* image, uint64_t* memory) {
  if (!s_GfxVulkan || !s_VolkReady) return;
  const UnityVulkanInstance vi = s_GfxVulkan->Instance();
  VkDevice device = vi.device;
  if (*image) {
    vkDestroyImage(device, (VkImage)*image, nullptr);
    *image = 0;
  }
  if (*memory) {
    vkFreeMemory(device, (VkDeviceMemory)*memory, nullptr);
    *memory = 0;
  }
}

// ── CPU 路径: IssuePluginCustomTextureUpdateV2 纹理更新回调 (渲染线程调用) ──
// UpdateTextureBegin: 从帧槽拷 BGRA 交给 Unity; UpdateTextureEnd: 释放临时内存

void __stdcall avoxTextureUpdateCallback(int eventID, void* data) {
  UnityRenderingExtTextureUpdateParamsV2* params = (UnityRenderingExtTextureUpdateParamsV2*)data;
  if (eventID == kUnityRenderingExtEventUpdateTextureBeginV2) {
    void* texData = nullptr;
    PlayerBridge* bridge = findBridge((uint32_t)params->userData);
    if (!bridge || !bridge->allocCpuFrame(params->width, params->height, params->bpp, &texData)) {
      // 无帧/异常: 黑帧兜底 (Unity 总是上传 texData)
      texData = calloc((size_t)params->width * params->height * params->bpp, 1);
    }
    params->texData = texData;
  } else if (eventID == kUnityRenderingExtEventUpdateTextureEndV2) {
    free(params->texData);
    params->texData = nullptr;
  }
}
