#pragma once

#include <string>
#include <vector>

#include "avox/module/LogHelper.hpp"

// 动态加载<vulkan/vulkan.h>里的函数
#define VK_NO_PROTOTYPES 1
#include <volk.h>

#ifdef AVOX_ENABLE_VULKAN_DECODE
#include <vk_video/vulkan_video_codec_h264std_decode.h>
#include <vk_video/vulkan_video_codec_h265std_decode.h>
#endif

#ifdef __ANDROID__
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/asset_manager.h>
#include <android/hardware_buffer.h>
#endif

#ifdef __APPLE__
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan_metal.h> 
#endif

namespace avox {

enum class QueueType : uint32_t {
  none = 0,
  graphics = VK_QUEUE_GRAPHICS_BIT,
  compute = VK_QUEUE_COMPUTE_BIT,
  graphics_compute = graphics | compute,
#ifdef AVOX_ENABLE_VULKAN_DECODE
  decode = VK_QUEUE_VIDEO_DECODE_BIT_KHR,
  graphics_compute_decode = graphics | compute | decode,
#endif
};

struct FormatInfo {
  uint32_t size;
  uint32_t channelCount;
};

// 当前vulkan驱动支持的特性
struct AVOX_EXPORT VKLayerProps {
 public:
  VKLayerProps();
  ~VKLayerProps();

 public:
  std::vector<VkLayerProperties> layerProps;
  std::vector<VkExtensionProperties> exLayerProps;

 public:
  void init();
  bool findLayer(const char* layerName);
  bool findExtension(const char* name);
  void dump();
};

// 创建VkInstance实例的参数
struct VkInstanceArgs {
 public:
  bool bDebugMsg = false;
  std::string appName;
  // 需要的特性
  std::vector<const char*> layers;
  // 需要的扩展特性
  std::vector<const char*> extensions;

 private:
  // 用于检查vulkan驱动是否满足layers/extensions
  VKLayerProps vLayerProps = {};

 public:
  static VkInstanceArgs defArgs(bool bDebug = false);

 public:
  VkInstance crateInstace();
};

// 物理显卡封装,用来记录VkPhysicalDevice的常用属性
struct VKPhysDevWrapper {
 public:
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  // 物理显示的属性
  VkPhysicalDeviceProperties properties;
  // 一般来说,主机会有1-3个左右props,手机一般只一个.
  // 而第0个一般要满足graph/compute/present
  // queueFlags包含每个通道VK_QUEUE_GRAPHICS_BIT/VK_QUEUE_COMPUTE_BIT能力表示
  std::vector<VkQueueFamilyProperties2> props;
  // VkQueueFamilyProperties

#ifdef AVOX_ENABLE_VULKAN_DECODE
  std::vector<VkQueueFamilyVideoPropertiesKHR> videoQueues;
  std::vector<VkQueueFamilyQueryResultStatusPropertiesKHR> queryResultStatus;
#endif
  // 物理显卡支持的扩展特性
  std::vector<VkExtensionProperties> exProps;
  // 物理显卡的内存显示
  VkPhysicalDeviceMemoryProperties mempryProperties;
  // 渲染，计算，传输，解码，编码队列索引
  std::vector<int32_t> graphicsIndexs;
  std::vector<int32_t> computeIndexs;
  std::vector<int32_t> decodeIndexs;
  std::vector<int32_t> encodeIndexs;
  std::vector<int32_t> transferIndexs;
  uint64_t luid = 0;

 public:
  // 根据VkPhysicalDevice填充PhysicalDevice
  void form(VkPhysicalDevice phyDevice);
  // 清空
  void clear();
  // 查找支持QueueType的队列
  bool findIndex(QueueType type, int32_t& index);
  // 找到支持渲染到窗口的渲染Index
  bool findSurfaceQueue(VkSurfaceKHR surface, int32_t graphicsIndex,
                        int32_t& presentIndex);
  // 查找是否支持扩展特性
  bool findExtension(const char* name) const;
  // typeBits当前的显存空间类型,每一位的索引对应mempryProperties是否可用
  // quirementsMaks需要满足的mask,是否主机可见,是否支持缓存等
  // index返回VkPhysicalDevice支持quirementsMaks的显存分配索引
  bool getMemoryTypeIndex(uint32_t typeBits, VkFlags quirementsMaks,
                          uint32_t& index);
  // WIN平台是否支持与DX11交互
  bool bInterpDx11();
  // 与android的HardwareImage支持(opengl es)
  bool bInterpAndroid();
  bool bInterpMetal();
#ifdef AVOX_ENABLE_VULKAN_DECODE
  uint32_t getYcbcrConversionCount(VkFormat format);
#endif
};

struct VkDeviceArgs {
  // 图像渲染(主要是用来图像处理,可能并不需要)
  int32_t graphicsIndex = -1;
  // 计算
  int32_t computeIndex = 0;
  // 传输
  int32_t transferIndex = 0;
  // 解码
  int32_t decodeIndex = 0;
  // 编码
  int32_t encodeIndex = -1;
  // 需要的扩展特性
  std::vector<const char*> extensions;

 private:
#ifdef AVOX_ENABLE_VULKAN_DECODE
  VkVideoCodecOperationFlagsKHR decodeCodecs =
      VK_VIDEO_CODEC_OPERATION_NONE_KHR;
#endif

 public:
  // 根据硬件特性生成一个默认的VkDeviceArgs参数
  static VkDeviceArgs defArgs(const VKPhysDevWrapper& phyDevice);

 public:
  VkDevice crateDevice(const VKPhysDevWrapper& phyDevice);
#ifdef AVOX_ENABLE_VULKAN_DECODE
  // 创建设备后,获取支持的解码格式
  VkVideoCodecOperationFlagsKHR getDecodeCodecs() { return decodeCodecs; }
#endif
};

struct VKDeviceWrapper {
 public:
  VkDevice device = VK_NULL_HANDLE;

 public:
  // 根据VkPhysicalDevice填充PhysicalDevice
  void form(VkDevice device);
};

}