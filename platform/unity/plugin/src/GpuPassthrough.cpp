#include "GpuPassthrough.h"
#include "PlayerBridge.h"
#include "SourceBridge.h"


#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <volk.h>
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_4.h>

#include "unity/IUnityInterface.h"
#include "unity/IUnityGraphics.h"
#include "unity/IUnityGraphicsVulkan.h"
#include "unity/IUnityGraphicsD3D11.h"
#include "unity/IUnityRenderingExtensions.h"

// ── Unity 插件接口 (UnityPluginLoad 自动调用) ──
static IUnityInterfaces* s_UnityInterfaces = nullptr;
static IUnityGraphics* s_Graphics = nullptr;
static IUnityGraphicsVulkan* s_GfxVulkan = nullptr;
static IUnityGraphicsD3D11* s_GfxD3D11 = nullptr;
static bool s_VolkReady = false;
static bool s_GpuAvailable = false;
// 导入方式: 0 无 / 1 Vulkan (VkImage 导入) / 2 D3D11 (OpenSharedResource1 导入)
static int s_GpuFlavor = 0;

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginLoad(IUnityInterfaces* unityInterfaces) {
  s_UnityInterfaces = unityInterfaces;
  s_Graphics = unityInterfaces->Get<IUnityGraphics>();
  s_GfxVulkan = unityInterfaces->Get<IUnityGraphicsVulkan>();
  s_GfxD3D11 = unityInterfaces->Get<IUnityGraphicsD3D11>();
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload() {
  s_UnityInterfaces = nullptr;
  s_Graphics = nullptr;
  s_GfxVulkan = nullptr;
  s_GfxD3D11 = nullptr;
  s_VolkReady = false;
  s_GpuAvailable = false;
  s_GpuFlavor = 0;
}

bool unityGpuPassthroughAvailable() {
  // 惰性初始化: 首次 avoxPlayerCreate→bindSurface 时 UnityPluginLoad 已跑完,
  // 渲染设备已就绪, 此处探测 Vulkan/D3D11 后端 (幂等)
  if (!s_VolkReady) unityVulkanInit();
  return s_GpuAvailable;
}

int unityGpuImportFlavor() {
  if (!s_VolkReady) unityVulkanInit();
  return s_GpuFlavor;
}

bool unityVulkanInit() {
  if (s_VolkReady) return s_GpuAvailable;
  s_VolkReady = true;
  if (!s_Graphics) return false;
  const auto renderer = s_Graphics->GetRenderer();
  // Unity Vulkan 后端: volk 加载 Unity instance/device, 导入 VkImage (同 godot)
  if (renderer == kUnityGfxRendererVulkan) {
    s_GpuFlavor = 1;
    if (!s_GfxVulkan) return false;
    if (volkInitialize() != VK_SUCCESS) return false;
    const UnityVulkanInstance vi = s_GfxVulkan->Instance();
    if (!vi.instance || !vi.device) return false;
    volkLoadInstance(vi.instance);
    volkLoadDevice(vi.device);
    s_GpuAvailable = true;
    return true;
  }
  // Unity D3D11 后端: 走底层共享纹理 + 渲染线程 CopyResource 的拷贝模式。
  // 注意: 共享纹理由 avox D3D11 设备创建 (D3D11 出生), Unity 设备只做标准
  // D3D11→D3D11 OpenSharedResource1; 严禁用 VK 导出的 OPAQUE_WIN32 内存直开
  // (2026-09-05 实测 AMD 驱动 E_INVALIDARG 重试致驱动级崩溃/系统重启)
  if (renderer == kUnityGfxRendererD3D11) {
    s_GpuFlavor = 2;
    s_GpuAvailable = (s_GfxD3D11 != nullptr);
    return s_GpuAvailable;
  }
  return false;
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

// ── D3D11 拷贝模式: avox 底层共享纹理 → Unity 渲染线程 CopyResource 到 C# 纹理 ──
// 共享纹理在 avox D3D11 设备上创建 (MISC_SHARED_NTHANDLE + 共享 fence),
// VK 管线每帧拷入并 Signal fence; Unity 设备仅标准 D3D11→D3D11 打开复制。
// 以下函数均只在 Unity 渲染线程调用 (经 avoxDx11RenderEvent)

bool unityDx11EnsureOpened(UnityDx11CopyState* st, uint64_t texHandle, uint64_t fenceHandle) {
  if (!st || !texHandle) return false;
  if (st->sharedTex && st->srcHandle == texHandle) return true;
  // 句柄变化 (首开/分辨率重绑): 先释放旧资源再单次打开 (失败不重试)
  unityDx11Close(st);
  ID3D11Device* device = s_GfxD3D11 ? s_GfxD3D11->GetDevice() : nullptr;
  if (!device) return false;
  ID3D11Device1* device1 = nullptr;
  if (FAILED(device->QueryInterface(__uuidof(ID3D11Device1), (void**)&device1))) return false;
  ID3D11Texture2D* tex = nullptr;
  HRESULT hr = device1->OpenSharedResource1((HANDLE)(uintptr_t)texHandle,
                                            __uuidof(ID3D11Texture2D), (void**)&tex);
  device1->Release();
  if (FAILED(hr)) {
    // 失败单次不重试 (安全规矩); 由 bridge 侧轮询报错
    return false;
  }
  D3D11_TEXTURE2D_DESC desc = {};
  tex->GetDesc(&desc);
  st->sharedTex = (uint64_t)(uintptr_t)tex;
  st->srcHandle = texHandle;
  st->width = (int32_t)desc.Width;
  st->height = (int32_t)desc.Height;
  st->lastFenceVal = 0;
  st->openCount++;
  // 目标纹理: 插件在 Unity 设备上自建, 格式/尺寸与共享纹理严格一致
  // (CopyResource 要求格式兼容; Unity 建的 BGRA32 可能是 TYPELESS, 会静默失败)
  D3D11_TEXTURE2D_DESC tdesc = desc;
  tdesc.MiscFlags = 0;
  tdesc.Usage = D3D11_USAGE_DEFAULT;
  tdesc.CPUAccessFlags = 0;
  ID3D11Texture2D* target = nullptr;
  if (SUCCEEDED(device->CreateTexture2D(&tdesc, nullptr, &target))) {
    st->targetTex = (uint64_t)(uintptr_t)target;
  }
  // fence 可选: 打开失败仅退化为每帧拷贝
  if (fenceHandle) {
    ID3D11Device5* device5 = nullptr;
    if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D11Device5), (void**)&device5))) {
      ID3D11Fence* fence = nullptr;
      if (SUCCEEDED(device5->OpenSharedFence((HANDLE)(uintptr_t)fenceHandle,
                                             __uuidof(ID3D11Fence), (void**)&fence))) {
        st->sharedFence = (uint64_t)(uintptr_t)fence;
      }
      device5->Release();
    }
  }
  return st->targetTex != 0;
}

bool unityDx11CopyFrame(UnityDx11CopyState* st) {
  if (!st || !st->sharedTex || !st->targetTex) return false;
  ID3D11Fence* fence = (ID3D11Fence*)(uintptr_t)st->sharedFence;
  if (fence) {
    // fence 去重: avox 未写新帧则跳过拷贝
    UINT64 val = fence->GetCompletedValue();
    if (val == st->lastFenceVal) return true;
    st->lastFenceVal = val;
  }
  ID3D11Device* device = s_GfxD3D11->GetDevice();
  if (!device) return false;
  ID3D11DeviceContext* ctx = nullptr;
  device->GetImmediateContext(&ctx);
  if (!ctx) return false;
  ctx->CopyResource((ID3D11Texture2D*)(uintptr_t)st->targetTex,
                    (ID3D11Texture2D*)(uintptr_t)st->sharedTex);
  ctx->Release();
  st->copyCount++;
  return true;
}

uint64_t unityDx11FenceValue(UnityDx11CopyState* st) {
  if (!st || !st->sharedFence) return 0;
  return ((ID3D11Fence*)(uintptr_t)st->sharedFence)->GetCompletedValue();
}

void unityDx11Close(UnityDx11CopyState* st) {  if (!st) return;
  if (st->sharedFence) {
    ((ID3D11Fence*)(uintptr_t)st->sharedFence)->Release();
    st->sharedFence = 0;
  }
  if (st->targetTex) {
    ((ID3D11Texture2D*)(uintptr_t)st->targetTex)->Release();
    st->targetTex = 0;
  }
  if (st->sharedTex) {
    ((ID3D11Texture2D*)(uintptr_t)st->sharedTex)->Release();
    st->sharedTex = 0;
  }
  st->srcHandle = 0;
  st->width = 0;
  st->height = 0;
  st->lastFenceVal = 0;
}

void __stdcall avoxDx11RenderEvent(int eventId) {
  PlayerBridge* bridge = findBridge((uint32_t)eventId);
  if (bridge) bridge->renderDx11Copy();
}

// ── CPU 路径: IssuePluginCustomTextureUpdateV2 纹理更新回调 (渲染线程调用) ──
// UpdateTextureBegin: 从帧槽拷 BGRA 交给 Unity; UpdateTextureEnd: 释放临时内存

void __stdcall avoxTextureUpdateCallback(int eventID, void* data) {
  UnityRenderingExtTextureUpdateParamsV2* params = (UnityRenderingExtTextureUpdateParamsV2*)data;
  if (eventID == kUnityRenderingExtEventUpdateTextureBeginV2) {
    void* texData = nullptr;
    const uint32_t id = (uint32_t)params->userData;
    PlayerBridge* bridge = findBridge(id);
    bool ok = bridge && bridge->allocCpuFrame(params->width, params->height, params->bpp, &texData);
    if (!ok) {
      // 设备源 (相机等 SourceBridge) 复用同一上传回调
      SourceBridge* source = findSourceBridge(id);
      ok = source && source->allocCpuFrame(params->width, params->height, params->bpp, &texData);
    }
    if (!ok) {
      // 无帧/异常: 黑帧兜底 (Unity 总是上传 texData)
      texData = calloc((size_t)params->width * params->height * params->bpp, 1);
    }
    params->texData = texData;
  } else if (eventID == kUnityRenderingExtEventUpdateTextureEndV2) {
    free(params->texData);
    params->texData = nullptr;
  }
}
