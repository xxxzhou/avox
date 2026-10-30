// AvoxRuntime 核心侧实现: 不依赖 FFmpeg 的能力查询。
// FFmpeg 相关实现在 avox_ffmpeg/AvoxRuntimeFF.cpp

#include "avox/AvoxRuntime.h"

#if AVOX_ENABLE_VULKAN
#include "avox_vulkan/VkExport.h"
namespace avox {
// 定义在 avox_vulkan/VkContext.cpp; VkExport.h 里那条是全局 C 声明, 别混绑
bool canVulkan();
}  // namespace avox
#endif

namespace avox {

bool isVulkanAvailable() {
#if AVOX_ENABLE_VULKAN
  // canVulkan 首调真建/销 VkInstance+VkDevice, 结果进程内缓存
  return canVulkan();
#else
  return false;
#endif
}

}  // namespace avox
