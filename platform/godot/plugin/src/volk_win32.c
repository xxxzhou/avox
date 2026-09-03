// Force-include vulkan.h before volk.h, so that vulkan_win32.h gets included
// (via VK_USE_PLATFORM_WIN32_KHR), which defines VK_KHR_external_memory_win32 etc.
// Without this, volk.h only includes vulkan_core.h and skips Win32 extension function loading.
#include <vulkan/vulkan.h>

#ifndef VK_KHR_external_memory_win32
#error "VK_KHR_external_memory_win32 not defined after vulkan.h include"
#endif

#include "volk.c"
