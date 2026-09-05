#pragma once

#include <stdint.h>

// Unity 插件加载/GPU 直通支持 (对应 godot 插件 gpu_passthrough + GPU 导入部分)

// 全局 GPU 直通可用性: Unity Vulkan/D3D11 后端
// 由 bindSurface 读取决定 GPU 模式还是 CPU 回退
bool unityGpuPassthroughAvailable();

// 导入方式: 0 无 / 1 Vulkan (VkImage 跨设备导入) / 2 D3D11 (底层共享纹理 + 渲染线程拷贝)
int unityGpuImportFlavor();

// 主线程延迟初始化 (Vulkan: volk 加载 Unity 的 instance/device; D3D11: 探测设备接口),
// 首次 updateGpu 调用
bool unityVulkanInit();

// 把 avox 导出的 NT 句柄 (OPAQUE_WIN32) 导入 Unity VkDevice, 返回 VkImage/VkDeviceMemory
// 镜像 godot SurfaceTextureBridge::importSharedImage 流程
bool unityImportSharedImage(uint64_t memHandle, int32_t w, int32_t h, uint64_t* outImage,
                            uint64_t* outMemory);

// 释放导入的 VkImage/VkDeviceMemory (Unity 主线程调用)
void unityReleaseImported(uint64_t* image, uint64_t* memory);

// ── D3D11 拷贝模式 (flavor 2) ──
// avox 底层自建 NT 共享纹理 (D3D11 出生, VK 每帧拷入 + fence 递增);
// 插件在 Unity 渲染线程把共享纹理 CopyResource 到 C# 普通纹理上。
// 全程无 VK→D3D11 反向导入 (该方向在 AMD 驱动上曾致驱动级崩溃/系统重启, 已弃用)

// 每播放器一份的 D3D11 拷贝状态 (仅 Unity 渲染线程触碰, 见 PlayerBridge::renderDx11Copy)
struct UnityDx11CopyState {
  uint64_t srcHandle = 0;    // 已打开的 avox NT 句柄 (变化时重开)
  uint64_t sharedTex = 0;    // 打开的 ID3D11Texture2D* (Unity 设备上)
  uint64_t sharedFence = 0;  // 打开的 ID3D11Fence* (可空)
  uint64_t targetTex = 0;    // 插件在 Unity 设备上自建的目标纹理 (格式与共享纹理一致)
  uint64_t lastFenceVal = 0; // 上次拷贝时的 fence 值 (去重)
  int32_t width = 0;
  int32_t height = 0;
  uint32_t copyCount = 0;    // 实际执行 CopyResource 次数 (诊断用)
  uint32_t openCount = 0;    // 共享纹理打开/重开次数 (诊断用, >1 说明发生过重绑)
};

// 渲染线程: 确保共享纹理/fence/目标纹理就绪 (句柄变化重开, 失败单次不重试)
// 成功后 width/height 填充实际纹理尺寸; 返回 false 表示不可用
bool unityDx11EnsureOpened(UnityDx11CopyState* st, uint64_t texHandle, uint64_t fenceHandle);

// 渲染线程: fence 前进时把共享纹理拷到自建目标纹理
bool unityDx11CopyFrame(UnityDx11CopyState* st);

// 渲染线程: 释放本侧打开的共享纹理/fence 引用
void unityDx11Close(UnityDx11CopyState* st);

// 渲染线程: 当前观察到的共享 fence 值 (未打开返回 0)
uint64_t unityDx11FenceValue(UnityDx11CopyState* st);

// Unity 渲染事件入口 (C# GL.IssuePluginEvent 触发, eventId = PlayerBridge id)
#ifdef _WIN32
void __stdcall avoxDx11RenderEvent(int eventId);
#else
void avoxDx11RenderEvent(int eventId);
#endif

// CPU 路径纹理更新回调 (C# 经 CommandBuffer.IssuePluginCustomTextureUpdateV2 触发,
// userData = PlayerBridge id); Unity 6/官方 NativeRenderingPlugin TextureUpdate 示例同款
// 约定与 unity/IUnityInterface.h 的 UNITY_INTERFACE_API 一致 (__stdcall, x64 下无操作)
#ifdef _WIN32
void __stdcall avoxTextureUpdateCallback(int eventID, void* data);
#else
void avoxTextureUpdateCallback(int eventID, void* data);
#endif
