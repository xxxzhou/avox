#pragma once

#include "VkSharedImage.hpp"

namespace avox {

// 跨 VkDevice 共享兼容性检测
class VkShareChecker {
 public:
  // 检测两个 VkPhysicalDevice 是否支持指定 handle type 的 external memory 共享
  static bool checkCompatible(VkPhysicalDevice phyA, VkPhysicalDevice phyB,
                              VkShareHandleType handleType);

  // 检测单个 VkPhysicalDevice 是否支持导出
  static bool checkExportable(VkPhysicalDevice phyDevice,
                              const VkSharedImageDesc& desc,
                              VkShareHandleType handleType);

  // 检测单个 VkPhysicalDevice 是否支持导入
  static bool checkImportable(VkPhysicalDevice phyDevice,
                              const VkSharedImageDesc& desc,
                              VkShareHandleType handleType);

  // 自动选择最佳 handle type(优先零拷贝,回退 host allocation)
  static VkShareHandleType selectBestHandle(VkPhysicalDevice phyA,
                                             VkPhysicalDevice phyB,
                                             const VkSharedImageDesc& desc);

  // 检测两个 VkPhysicalDevice 是否在同一 GPU(LUID 匹配)
  static bool bSameGpu(VkPhysicalDevice phyA, VkPhysicalDevice phyB);
};

}
