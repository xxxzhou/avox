#include "GpuPassthrough.h"
#include "PlayerBridge.h"
#include "SourceBridge.h"
#ifdef _WIN32
#include "RtcPlayerBridge.h"
#endif


#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifdef _WIN32
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_4.h>
#include <d3d12.h>
#elif defined(__ANDROID__)
// Vulkan 平台宏由 CMake 提供 (VK_USE_PLATFORM_ANDROID_KHR)
#include <android/log.h>
#include <android/hardware_buffer.h>
#include <cstring>
#include <vector>
// 插件内日志: Unity 无控制台, 走 logcat (真机验证过滤 tag: avox_unity)
#define AVLOG(...) __android_log_print(ANDROID_LOG_INFO, "avox_unity", __VA_ARGS__)
#endif
#include <volk.h>
#include "unity/IUnityInterface.h"
#include "unity/IUnityGraphics.h"
#include "unity/IUnityGraphicsVulkan.h"
#ifdef _WIN32
#include "unity/IUnityGraphicsD3D11.h"
#include "unity/IUnityGraphicsD3D12.h"
#endif
#include "unity/IUnityRenderingExtensions.h"

// ── Unity 插件接口 (UnityPluginLoad 自动调用) ──
static IUnityInterfaces* s_UnityInterfaces = nullptr;
static IUnityGraphics* s_Graphics = nullptr;
static IUnityGraphicsVulkan* s_GfxVulkan = nullptr;
#ifdef _WIN32
static IUnityGraphicsD3D11* s_GfxD3D11 = nullptr;
#endif
static bool s_VolkReady = false;
static bool s_GpuAvailable = false;
// 导入方式: 0 无 / 1 Vulkan (VkImage 导入) / 2 D3D11 / 3 D3D12 (底层共享纹理 + 渲染线程拷贝)
static int s_GpuFlavor = 0;

// D3D12 插件接口: 拷贝路径需要 GetDevice/CommandRecordingState/TextureFromNativeTexture
// (v6 起齐全; 拷贝录 Unity 自己的命令列表, 无需自建 allocator/queue)
#ifdef _WIN32
struct UnityDx12Api {
  ID3D12Device* (UNITY_INTERFACE_API * getDevice)() = nullptr;
  bool (UNITY_INTERFACE_API * commandRecordingState)(UnityGraphicsD3D12RecordingState*) = nullptr;
  ID3D12Resource* (UNITY_INTERFACE_API * textureFromNativeTexture)(UnityTextureID) = nullptr;
  bool ok() const { return getDevice && commandRecordingState && textureFromNativeTexture; }
};
static UnityDx12Api s_Dx12Api;

static void resolveDx12Api(IUnityInterfaces* ui) {
  if (s_Dx12Api.ok()) return;
  if (auto* v8 = ui->Get<IUnityGraphicsD3D12v8>()) {
    s_Dx12Api.getDevice = v8->GetDevice;
    s_Dx12Api.commandRecordingState = v8->CommandRecordingState;
    s_Dx12Api.textureFromNativeTexture = v8->TextureFromNativeTexture;
  } else if (auto* v7 = ui->Get<IUnityGraphicsD3D12v7>()) {
    s_Dx12Api.getDevice = v7->GetDevice;
    s_Dx12Api.commandRecordingState = v7->CommandRecordingState;
    s_Dx12Api.textureFromNativeTexture = v7->TextureFromNativeTexture;
  } else if (auto* v6 = ui->Get<IUnityGraphicsD3D12v6>()) {
    s_Dx12Api.getDevice = v6->GetDevice;
    s_Dx12Api.commandRecordingState = v6->CommandRecordingState;
    s_Dx12Api.textureFromNativeTexture = v6->TextureFromNativeTexture;
  }
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginLoad(IUnityInterfaces* unityInterfaces) {
  s_UnityInterfaces = unityInterfaces;
  s_Graphics = unityInterfaces->Get<IUnityGraphics>();
  s_GfxVulkan = unityInterfaces->Get<IUnityGraphicsVulkan>();
  s_GfxD3D11 = unityInterfaces->Get<IUnityGraphicsD3D11>();
  resolveDx12Api(unityInterfaces);
}
#else
// ── Android: Unity Vulkan 设备 AHB 扩展注入 ──
// Unity 建设备时不启用 VK_ANDROID_external_memory_android_hardware_buffer,
// 经 IUnityGraphicsVulkan::InterceptInitialization 换掉 vkGetInstanceProcAddr,
// 在 vkCreateDevice 时追加该扩展 (Godot 无此注入点, 这是 Unity 的关键优势)。
static PFN_vkGetInstanceProcAddr s_PrevGIPA = nullptr;
static PFN_vkCreateDevice s_RealCreateDevice = nullptr;
static PFN_vkEnumerateDeviceExtensionProperties s_RealEnumDevs = nullptr;
static bool s_AhbInjected = false;
static const char* kAhbExtName = "VK_ANDROID_external_memory_android_hardware_buffer";

static VKAPI_ATTR VkResult VKAPI_CALL
wrappedEnumerateDeviceExtensionProperties(VkPhysicalDevice phys, const char* layer,
                                          uint32_t* pCount, VkExtensionProperties* props) {
  const VkResult r = s_RealEnumDevs(phys, layer, pCount, props);
  if (layer && layer[0]) return r;  // 只往实现层扩展列表追加
  if (!props || !pCount || (r != VK_SUCCESS && r != VK_INCOMPLETE)) return r;
  for (uint32_t i = 0; i < *pCount; ++i) {
    if (!strcmp(props[i].extensionName, kAhbExtName)) return r;
  }
  // 计数查询先 +1 预留, 拉取时补写 (两侧配对, 上层缓冲按 +1 分配)
  if (r == VK_SUCCESS && *pCount < 4096u) {
    snprintf(props[*pCount].extensionName, sizeof(props[*pCount].extensionName), "%s", kAhbExtName);
    props[*pCount].specVersion = 1;
    ++(*pCount);
  }
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL
wrappedCreateDevice(VkPhysicalDevice phys, const VkDeviceCreateInfo* info,
                    const VkAllocationCallbacks* ac, VkDevice* out) {
  VkDeviceCreateInfo mod = *info;
  const char* names[64];
  bool has = false;
  for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
    if (!strcmp(info->ppEnabledExtensionNames[i], kAhbExtName)) {
      has = true;
      break;
    }
  }
  if (!has && info->enabledExtensionCount < 63u) {
    memcpy(names, info->ppEnabledExtensionNames, info->enabledExtensionCount * sizeof(char*));
    names[info->enabledExtensionCount] = kAhbExtName;
    mod.enabledExtensionCount = info->enabledExtensionCount + 1;
    mod.ppEnabledExtensionNames = names;
  }
  const VkResult r = s_RealCreateDevice(phys, &mod, ac, out);
  if (r == VK_SUCCESS) {
    s_AhbInjected = true;
    AVLOG("AHB extension injected into Unity VkDevice");
  }
  return r;
}

static PFN_vkVoidFunction VKAPI_CALL
myGetInstanceProcAddr(VkInstance instance, const char* name) {
  if (!strcmp(name, "vkCreateDevice")) {
    s_RealCreateDevice = (PFN_vkCreateDevice)s_PrevGIPA(instance, name);
    return (PFN_vkVoidFunction)&wrappedCreateDevice;
  }
  if (!strcmp(name, "vkEnumerateDeviceExtensionProperties")) {
    s_RealEnumDevs = (PFN_vkEnumerateDeviceExtensionProperties)s_PrevGIPA(instance, name);
    return (PFN_vkVoidFunction)&wrappedEnumerateDeviceExtensionProperties;
  }
  return s_PrevGIPA(instance, name);
}

static PFN_vkGetInstanceProcAddr VKAPI_CALL
myVulkanInitCallback(PFN_vkGetInstanceProcAddr prev, void* userdata) {
  (void)userdata;
  s_PrevGIPA = prev;
  return (PFN_vkGetInstanceProcAddr)&myGetInstanceProcAddr;
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginLoad(IUnityInterfaces* unityInterfaces) {
  s_UnityInterfaces = unityInterfaces;
  s_Graphics = unityInterfaces->Get<IUnityGraphics>();
  s_GfxVulkan = unityInterfaces->Get<IUnityGraphicsVulkan>();
  // 必须在 Unity 建实例/设备前挂上; 挂晚了 InterceptInitialization 返回 false,
  // 之后 unityVulkanInit 枚举不到 AHB 扩展会自动 CPU 回退
  if (s_GfxVulkan &&
      !s_GfxVulkan->InterceptInitialization(&myVulkanInitCallback, nullptr)) {
    AVLOG("InterceptInitialization failed (vulkan device already created?)");
  }
}
#endif

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload() {
  s_UnityInterfaces = nullptr;
  s_Graphics = nullptr;
  s_GfxVulkan = nullptr;
#ifdef _WIN32
  s_GfxD3D11 = nullptr;
  s_Dx12Api = {};
#endif
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
  // Unity Vulkan 后端: volk 加载 Unity instance/device, 导入 VkImage/AHB (同 godot)
  if (renderer == kUnityGfxRendererVulkan) {
    s_GpuFlavor = 1;
    if (!s_GfxVulkan) return false;
    if (volkInitialize() != VK_SUCCESS) return false;
    const UnityVulkanInstance vi = s_GfxVulkan->Instance();
    if (!vi.instance || !vi.device) return false;
    volkLoadInstance(vi.instance);
    volkLoadDevice(vi.device);
#ifdef _WIN32
    s_GpuAvailable = true;
#else
    // Android: 确认 Unity 设备已启用 AHB 扩展 (注入成功 / Unity 默认启用)
    bool ahbOn = false;
    uint32_t extCount = 0;
    if (vkEnumerateDeviceExtensionProperties(vi.physicalDevice, nullptr, &extCount, nullptr) ==
            VK_SUCCESS &&
        extCount > 0) {
      std::vector<VkExtensionProperties> exts(extCount);
      if (vkEnumerateDeviceExtensionProperties(vi.physicalDevice, nullptr, &extCount,
                                               exts.data()) == VK_SUCCESS) {
        for (const auto& e : exts) {
          if (!strcmp(e.extensionName, kAhbExtName)) {
            ahbOn = true;
            break;
          }
        }
      }
    }
    if (!ahbOn) {
      s_GpuFlavor = 0;
      s_GpuAvailable = false;
      AVLOG("AHB extension NOT enabled on Unity device (injected=%d), CPU fallback",
            (int)s_AhbInjected);
      return false;
    }
    s_GpuAvailable = true;
    AVLOG("vulkan init OK, AHB extension enabled (injected=%d)", (int)s_AhbInjected);
#endif
    return s_GpuAvailable;
  }
#ifdef _WIN32
  // Unity D3D11 后端: 走底层共享纹理 + 渲染线程 CopyResource 的拷贝模式。
  // 注意: 共享纹理由 avox D3D11 设备创建 (D3D11 出生), Unity 设备只做标准
  // D3D11→D3D11 OpenSharedResource1; 严禁用 VK 导出的 OPAQUE_WIN32 内存直开
  // (2026-09-05 实测 AMD 驱动 E_INVALIDARG 重试致驱动级崩溃/系统重启)
  if (renderer == kUnityGfxRendererD3D11) {
    s_GpuFlavor = 2;
    s_GpuAvailable = (s_GfxD3D11 != nullptr);
    return s_GpuAvailable;
  }
  // Unity D3D12 后端: 同一个 avox D3D11 出生共享句柄, D3D12 OpenSharedHandle 打开
  // (同 UE 插件/avox Dx12SharedTex), 经自有命令环 ExecuteCommandList 拷贝
  if (renderer == kUnityGfxRendererD3D12) {
    s_GpuFlavor = 3;
    s_GpuAvailable = s_Dx12Api.ok();
    return s_GpuAvailable;
  }
#endif
  return false;
}

bool unityImportSharedImage(uint64_t memHandle, int32_t w, int32_t h, uint64_t* outImage,
                            uint64_t* outMemory) {
#ifndef _WIN32
  (void)memHandle; (void)w; (void)h; (void)outImage; (void)outMemory;
  return false;  // Android 用 unityImportSharedImageAhb
#else
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
#endif
}

bool unityImportSharedImageAhb(void* ahbPtr, int32_t w, int32_t h, uint64_t* outImage,
                               uint64_t* outMemory) {
#ifndef __ANDROID__
  (void)ahbPtr; (void)w; (void)h; (void)outImage; (void)outMemory;
  return false;
#else
  if (!s_GpuAvailable || !s_GfxVulkan) return false;
  AHardwareBuffer* ahb = (AHardwareBuffer*)ahbPtr;
  if (!ahb || !outImage || !outMemory || w <= 0 || h <= 0) return false;
  const UnityVulkanInstance vi = s_GfxVulkan->Instance();
  VkDevice device = vi.device;
  // AHB 属性一次查询: 真实 format / allocationSize / 内存类型都以此为准
  // (规范导入写法, 同 godot surface.cpp / avox VkAndImage)
  VkAndroidHardwareBufferFormatPropertiesANDROID ahbFmt = {
      VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
  VkAndroidHardwareBufferPropertiesANDROID ahbProps = {
      VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
  ahbProps.pNext = &ahbFmt;
  if (vkGetAndroidHardwareBufferPropertiesANDROID(device, ahb, &ahbProps) != VK_SUCCESS) {
    AVLOG("ahb props query failed");
    return false;
  }
  const VkFormat fmt = ahbFmt.format != VK_FORMAT_UNDEFINED ? ahbFmt.format
                                                            : VK_FORMAT_R8G8B8A8_UNORM;
  // image 契约与 avox 导出端一致: TRANSFER_SRC|TRANSFER_DST|SAMPLED (同 godot)
  VkExternalMemoryImageCreateInfo extMemImg = {
      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
  extMemImg.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
  VkImageCreateInfo importInfo = {};
  importInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  importInfo.pNext = &extMemImg;
  importInfo.imageType = VK_IMAGE_TYPE_2D;
  importInfo.format = fmt;
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
  if (vkCreateImage(device, &importInfo, nullptr, &image) != VK_SUCCESS) {
    AVLOG("vkCreateImage failed");
    return false;
  }
  VkMemoryRequirements memReqs = {};
  vkGetImageMemoryRequirements(device, image, &memReqs);
  // AHB 导入: dedicated + import 同链; 分配大小用 AHB 属性值; 内存类型严格交集
  // (Adreno 对违规写法直接 SIGSEGV, 见 godot 端同款修复)
  VkImportAndroidHardwareBufferInfoANDROID importMemInfo = {
      VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
  importMemInfo.buffer = ahb;
  VkMemoryDedicatedAllocateInfo dedicatedInfo = {
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
  dedicatedInfo.pNext = &importMemInfo;
  dedicatedInfo.image = image;
  VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.pNext = &dedicatedInfo;
  allocInfo.allocationSize = ahbProps.allocationSize;
  const uint32_t inter = memReqs.memoryTypeBits & ahbProps.memoryTypeBits;
  if (inter == 0) {
    AVLOG("no memory type intersection (image=0x%x ahb=0x%x)", memReqs.memoryTypeBits,
          ahbProps.memoryTypeBits);
    vkDestroyImage(device, image, nullptr);
    return false;
  }
  uint32_t memoryTypeIndex = UINT32_MAX;
  for (uint32_t i = 0; i < 32; ++i) {
    if (inter & (1u << i)) {
      memoryTypeIndex = i;
      break;
    }
  }
  if (memoryTypeIndex == UINT32_MAX) {
    vkDestroyImage(device, image, nullptr);
    return false;
  }
  allocInfo.memoryTypeIndex = memoryTypeIndex;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
    AVLOG("vkAllocateMemory failed");
    vkDestroyImage(device, image, nullptr);
    return false;
  }
  VkBindImageMemoryInfo bindInfo = {VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO};
  bindInfo.image = image;
  bindInfo.memory = memory;
  bindInfo.memoryOffset = 0;
  if (vkBindImageMemory2(device, 1, &bindInfo) != VK_SUCCESS) {
    AVLOG("vkBindImageMemory2 failed");
    vkFreeMemory(device, memory, nullptr);
    vkDestroyImage(device, image, nullptr);
    return false;
  }
  *outImage = (uint64_t)image;
  *outMemory = (uint64_t)memory;
  AVLOG("AHB import OK %dx%d fmt=%d", w, h, (int)fmt);
  return true;
#endif
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

#ifdef _WIN32
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

#endif  // _WIN32 (D3D11 拷贝段)

#ifdef _WIN32
void __stdcall avoxDx11RenderEvent(int eventId) {
#else
void avoxDx11RenderEvent(int eventId) {
#endif

  PlayerBridge* bridge = findBridge((uint32_t)eventId);
  if (bridge) bridge->renderDx11Copy();
}

#ifdef _WIN32
// ── 诊断: 独立 D3D11 设备打开共享纹理转储一帧 (与 Unity 消费端无关的地面真值) ──
void unityDx11DumpShared(void* bridge, uint32_t playerId, const char* path) {
  PlayerBridge* pb = findBridge(playerId);
  if (!pb || !path) return;
  const uint64_t handle = pb->dx11HandleSnapshot();
  if (!handle) {
    printf("[avoxdump] no handle\n");
    return;
  }
  ID3D11Device* dev = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                               nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx))) {
    printf("[avoxdump] create device failed\n");
    return;
  }
  ID3D11Device1* dev1 = nullptr;
  ID3D11Texture2D* tex = nullptr;
  HRESULT hr = E_NOINTERFACE;
  if (SUCCEEDED(dev->QueryInterface(__uuidof(ID3D11Device1), (void**)&dev1))) {
    hr = dev1->OpenSharedResource1((HANDLE)(uintptr_t)handle,
                                   __uuidof(ID3D11Texture2D), (void**)&tex);
    dev1->Release();
  }
  if (FAILED(hr) || !tex) {
    printf("[avoxdump] open shared failed hr=0x%08lx\n", (unsigned long)hr);
    ctx->Release();
    dev->Release();
    return;
  }
  D3D11_TEXTURE2D_DESC desc = {};
  tex->GetDesc(&desc);
  D3D11_TEXTURE2D_DESC sdesc = desc;
  sdesc.Usage = D3D11_USAGE_STAGING;
  sdesc.BindFlags = 0;
  sdesc.MiscFlags = 0;
  sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Texture2D* staging = nullptr;
  if (FAILED(dev->CreateTexture2D(&sdesc, nullptr, &staging))) {
    tex->Release();
    ctx->Release();
    dev->Release();
    return;
  }
  ctx->CopyResource(staging, tex);
  D3D11_MAPPED_SUBRESOURCE map = {};
  if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
    FILE* f = fopen(path, "wb");
    if (f) {
      fprintf(f, "P6\n%u %u\n255\n", desc.Width, desc.Height);
      for (UINT y = 0; y < desc.Height; ++y) {
        const uint8_t* row = (const uint8_t*)map.pData + y * map.RowPitch;
        for (UINT x = 0; x < desc.Width; ++x) {
          fwrite(row + x * 4, 1, 3, f);
        }
      }
      fclose(f);
      printf("[avoxdump] dumped: %s\n", path);
    }
    ctx->Unmap(staging, 0);
  }
  staging->Release();
  tex->Release();
  ctx->Release();
  dev->Release();
}
#endif  // _WIN32

#ifdef _WIN32
// ── D3D12 拷贝模式 (flavor 3): 以下函数均只在 Unity 渲染线程调用 ──

bool unityDx12EnsureOpened(UnityDx12CopyState* st, uint64_t texHandle, uint64_t fenceHandle) {
  (void)fenceHandle;
  if (!st || !texHandle || !s_Dx12Api.ok()) return false;
  if (st->sharedTex && st->srcHandle == texHandle) return true;
  // 句柄变化 (首开/管线重建重绑): 释放旧资源再单次打开 (失败不重试)
  unityDx12Close(st);
  ID3D12Device* device = s_Dx12Api.getDevice();
  if (!device) return false;
  // D3D11 出生 NT 句柄在 D3D12 标准互操作打开 (同 UE 插件 openDx12/avox Dx12SharedTex)
  ID3D12Resource* tex = nullptr;
  if (FAILED(device->OpenSharedHandle((HANDLE)(uintptr_t)texHandle,
                                      __uuidof(ID3D12Resource), (void**)&tex)) ||
      !tex) {
    return false;
  }
  D3D12_RESOURCE_DESC desc = tex->GetDesc();
  st->sharedTex = (uint64_t)(uintptr_t)tex;
  st->srcHandle = texHandle;
  st->width = (int32_t)desc.Width;
  st->height = (int32_t)desc.Height;
  st->openCount++;
  st->lastFenceVal = 0;
  // fence 可选: 打开失败仅退化为每帧拷贝
  if (fenceHandle) {
    ID3D12Fence* fence = nullptr;
    if (SUCCEEDED(device->OpenSharedHandle((HANDLE)(uintptr_t)fenceHandle,
                                           __uuidof(ID3D12Fence), (void**)&fence))) {
      st->sharedFence = (uint64_t)(uintptr_t)fence;
    }
  }
  return true;
}

void unityDx12SetTarget(UnityDx12CopyState* st, void* nativeTex) {
  if (!st) return;
  if (!nativeTex || (uint64_t)(uintptr_t)nativeTex == st->targetNative) return;
  // C# GetNativeTexturePtr 在 D3D12 后端即 ID3D12Resource* (官方 NativeRenderingPlugin
  // 同款); TextureFromNativeTexture 对外部纹理实测返回 null, 直接强转
  st->targetTex = (uint64_t)(uintptr_t)nativeTex;
  st->targetNative = (uint64_t)(uintptr_t)nativeTex;
}

int unityDx12CopyFrame(UnityDx12CopyState* st) {
  if (!st || !st->sharedTex || !st->targetTex) {
    if (st) st->noTarget++;
    return 1;
  }
  if (st->sharedFence) {
    // fence 去重: avox 未写新帧则跳过拷贝
    UINT64 val = ((ID3D12Fence*)(uintptr_t)st->sharedFence)->GetCompletedValue();
    if (val == st->lastFenceVal) return 0;
    st->lastFenceVal = val;
  }
  // 录入 Unity 当前命令列表 (渲染事件期间可取), 无需自建 allocator/queue
  UnityGraphicsD3D12RecordingState rs = {};
  if (!s_Dx12Api.commandRecordingState(&rs) || !rs.commandList) {
    st->noCl++;
    return 2;
  }
  ID3D12GraphicsCommandList* cl = rs.commandList;
  // 源: 外来共享资源按 COMMON 语义自行插屏障; 目的: C# 纹理采样态 ↔ COPY_DEST
  D3D12_RESOURCE_BARRIER b[2];
  for (auto& x : b) {
    x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  }
  b[0].Transition.pResource = (ID3D12Resource*)(uintptr_t)st->sharedTex;
  b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  cl->ResourceBarrier(1, b);
  b[0].Transition.pResource = (ID3D12Resource*)(uintptr_t)st->targetTex;
  b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  cl->ResourceBarrier(1, b);
  cl->CopyResource((ID3D12Resource*)(uintptr_t)st->targetTex,
                   (ID3D12Resource*)(uintptr_t)st->sharedTex);
  b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  cl->ResourceBarrier(1, b);
  b[0].Transition.pResource = (ID3D12Resource*)(uintptr_t)st->sharedTex;
  b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
  cl->ResourceBarrier(1, b);
  st->copyCount++;
  return 0;
}

uint64_t unityDx12FenceValue(UnityDx12CopyState* st) {
  if (!st || !st->sharedFence) return 0;
  return ((ID3D12Fence*)(uintptr_t)st->sharedFence)->GetCompletedValue();
}

void unityDx12Close(UnityDx12CopyState* st) {
  if (!st) return;
  if (st->sharedFence) {
    ((ID3D12Fence*)(uintptr_t)st->sharedFence)->Release();
    st->sharedFence = 0;
  }
  if (st->sharedTex) {
    ((ID3D12Resource*)(uintptr_t)st->sharedTex)->Release();
    st->sharedTex = 0;
  }
  st->targetTex = 0;   // C# 纹理非本插件所有, 不 Release
  st->targetNative = 0;
  st->srcHandle = 0;
  st->width = 0;
  st->height = 0;
  st->lastFenceVal = 0;
}
#endif  // _WIN32

// ── CPU 路径: IssuePluginCustomTextureUpdateV2 纹理更新回调 (渲染线程调用) ──
// UpdateTextureBegin: 从帧槽拷 BGRA 交给 Unity; UpdateTextureEnd: 释放临时内存

#ifdef _WIN32
void __stdcall avoxTextureUpdateCallback(int eventID, void* data) {
#else
void avoxTextureUpdateCallback(int eventID, void* data) {
#endif

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
#ifdef _WIN32
    if (!ok) {
      // WebRTC 远端画面 (RtcPlayerBridge) 复用同一上传回调
      RtcPlayerBridge* rtc = findRtcBridge(id);
      ok = rtc && rtc->allocCpuFrame(params->width, params->height, params->bpp, &texData);
    }
#endif
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
