// volk 唯一编译单元: 动态加载 Vulkan (VK_NO_PROTOTYPES), 不链 vulkan-1.lib
// VK_USE_PLATFORM_WIN32_KHR 由 CMake target_compile_definitions 提供
#define VOLK_IMPLEMENTATION
#include <volk.h>
