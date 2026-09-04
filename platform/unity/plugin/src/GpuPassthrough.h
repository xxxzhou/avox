#pragma once

#include <stdint.h>

// Unity 插件加载/GPU 直通支持 (对应 godot 插件 gpu_passthrough + GPU 导入部分)

// 全局 GPU 直通可用性: Unity Vulkan 后端 + volk 就绪 + 平台外部内存可用
// 由 bindSurface 读取决定 GPU 模式还是 CPU 回退
bool unityGpuPassthroughAvailable();

// 主线程延迟初始化 (volk 加载 Unity 的 instance/device), 首次 updateGpu 调用
bool unityVulkanInit();

// 把 avox 导出的 NT 句柄 (OPAQUE_WIN32) 导入 Unity VkDevice, 返回 VkImage/VkDeviceMemory
// 镜像 godot SurfaceTextureBridge::importSharedImage 流程
bool unityImportSharedImage(uint64_t memHandle, int32_t w, int32_t h, uint64_t* outImage,
                            uint64_t* outMemory);

// 释放导入的 VkImage/VkDeviceMemory (Unity 主线程调用)
void unityReleaseImported(uint64_t* image, uint64_t* memory);

// CPU 路径纹理更新回调 (C# 经 CommandBuffer.IssuePluginCustomTextureUpdateV2 触发,
// userData = PlayerBridge id); Unity 6/官方 NativeRenderingPlugin TextureUpdate 示例同款
// 约定与 unity/IUnityInterface.h 的 UNITY_INTERFACE_API 一致 (__stdcall, x64 下无操作)
#ifdef _WIN32
void __stdcall avoxTextureUpdateCallback(int eventID, void* data);
#else
void avoxTextureUpdateCallback(int eventID, void* data);
#endif
