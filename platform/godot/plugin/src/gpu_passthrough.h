#pragma once

#include <volk.h>

/// GPU 直通可用标志
/// 由 avox_gpu_passthrough_init() (主线程延迟初始化) 设置:
///   - RenderingDevice 非 null (Vulkan 后端)
///   - volk 加载成功
///   - 外部内存扩展可用 (Windows: VK_KHR_external_memory_win32;
///     Android: VK_ANDROID_external_memory_android_hardware_buffer)
/// avox_surface.cpp 读取此标志决定走 GPU 模式还是 CPU 回退
extern bool gGpuPassthroughAvailable;

#ifdef _WIN32
/// volk 未加载的 Win32 扩展函数 (avox 的 volk 编译时未定义 VK_USE_PLATFORM_WIN32_KHR)
/// 由 avox_gpu_passthrough_init() 用 vkGetDeviceProcAddr 手动加载
extern PFN_vkGetMemoryWin32HandlePropertiesKHR g_vkGetMemoryWin32HandlePropertiesKHR;
#endif

/// 主线程延迟初始化 GPU 直通 (volk + RenderingDevice + 平台扩展函数)。
/// 必须在主线程、Godot 完全初始化后调用 (GDExtension SCENE 级 RenderingServer
/// 单例尚未注册, 不能在那里调用)。由 MediaPlayer::createPlayer 调用。
void avox_gpu_passthrough_init();
