// Android: 预挂 Android 平台头(vulkan_android.h 定义 VK_KHR_android_surface /
// AHB 扩展宏), 再统一进 volk 加载 —— 与 volk_win32.c 同构。
// 注意 VK_USE_PLATFORM_ANDROID_KHR 由目标编译定义提供(见插件 CMakeLists)。
#include <vulkan/vulkan.h>

#ifndef VK_KHR_android_surface
#error "VK_KHR_android_surface not defined after vulkan.h include (VK_USE_PLATFORM_ANDROID_KHR set?)"
#endif

#include "volk.c"
