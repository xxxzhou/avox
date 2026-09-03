#pragma once

#include "../VkContext.hpp"

namespace avox {

// 共享句柄类型
enum class VkShareHandleType : int32_t {
  none = 0,
  opaqueWin32,      // VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT
  opaqueFd,         // VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT
  androidHwBuffer,  // VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID
  hostAllocation,   // VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT
};

// 共享句柄(平台无关)
struct VkShareHandle {
  VkShareHandleType type = VkShareHandleType::none;
  // 平台无关的 handle 存储: Win32=HANDLE, Linux/Android=fd, 通用=void*
  void* handle = nullptr;

  // RAII: 析构时按 type 释放 handle
  ~VkShareHandle();
  VkShareHandle() = default;
  VkShareHandle(const VkShareHandle&) = delete;
  VkShareHandle& operator=(const VkShareHandle&) = delete;
  VkShareHandle(VkShareHandle&& other) noexcept;
  VkShareHandle& operator=(VkShareHandle&& other) noexcept;

  // 类型安全的访问器
#ifdef _WIN32
  HANDLE getWin32Handle() const {
    return type == VkShareHandleType::opaqueWin32 ? (HANDLE)handle : nullptr;
  }
#endif
  int getFd() const {
    return type == VkShareHandleType::opaqueFd ? (int)(intptr_t)handle : -1;
  }
};

// 共享图像描述(创建/导入时的参数契约)
struct VkSharedImageDesc {
  int32_t width = 0;
  int32_t height = 0;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  // 导出端填充,导入端校验
  VkShareHandleType handleType = VkShareHandleType::none;
};

// 跨 VkDevice 共享图像
// 导出端: createExportable → exportHandle
// 导入端: importFromHandle
class VkSharedImage : public VkContextRef {
 public:
  VkSharedImage();
  virtual ~VkSharedImage();

 private:
  VkImage vkImage = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkSharedImageDesc desc = {};
  bool bExported = false;
  bool bImported = false;

#if defined(VK_KHR_external_memory_win32)
  PFN_vkGetMemoryWin32HandleKHR vkGetMemoryWin32HandleKHR = nullptr;
#endif
#if defined(__ANDROID__) && defined(VK_ANDROID_external_memory_android_hardware_buffer)
  PFN_vkGetMemoryAndroidHardwareBufferANDROID vkGetMemoryAndroidHardwareBufferANDROID = nullptr;
#endif

 public:
  // ── 导出端 API ──

  // 创建可导出的 VkImage + VkDeviceMemory
  // 调用前需 setVkContext() 绑定导出端 VkDevice
  bool createExportable(const VkSharedImageDesc& desc);

  // 导出共享句柄(给另一个 VkDevice 用)
  VkShareHandle exportHandle();

  // ── 导入端 API ──

  // 从共享句柄导入 VkImage + VkDeviceMemory
  // 调用前需 setVkContext() 绑定导入端 VkDevice
  bool importFromHandle(const VkShareHandle& handle, const VkSharedImageDesc& desc);

  // ── 通用 API ──

  VkImage getImage() const { return vkImage; }
  VkDeviceMemory getMemory() const { return memory; }
  const VkSharedImageDesc& getDesc() const { return desc; }
  bool isValid() const { return vkImage != VK_NULL_HANDLE; }

  // 释放本端资源(不影响对端)
  void release();
};

}
