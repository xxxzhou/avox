#pragma once

#include "../VkContext.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/video/VideoDecoder.hpp"
namespace avox {

class VkDecoder : public VideoDecoder, public VkContextRef {
 public:
  VkDecoder(/* args */);
  virtual ~VkDecoder();

 protected:
  VkVideoSessionKHR videoSession = VK_NULL_HANDLE;
  bool bVulkanInit = false;
  VkVideoCodecOperationFlagBitsKHR vkCodecId =
      VK_VIDEO_CODEC_OPERATION_NONE_KHR;
  // SPS解析出来的宽高
  VkExtent3D imageExtent = {};
  // 子类根据PPS填充YUV格式
  VkVideoProfileInfoKHR profileInfo = {};
  // 设备支持的图像格式，比如大小
  VkVideoCapabilitiesKHR videoCapabilities = {};
  // 解码缓冲区和输出缓冲区是否独立
  bool bDistinct = false;
  // 对应YUV解码输出格式
  VkFormat selectFormat = {};
  uint32_t memoryRequirements = 0;
  std::vector<VkDeviceMemory> memoryBounds = {};

 protected:
  // 得到当前解码能力相关属性
  bool getVideoProfile();
  // 得到所有支持的图像格式
  bool getVideoFormat();
  // 创建video session
  bool createVideoSession();
  // 绑定内存
  bool memoryVideoSession();
  bool createFrameQueue();
  bool initVkDecoder();

 protected:
  // 子类解析H264的包
  virtual bool onParsePacket(PacketBufPtr pkt) = 0;

  // VideoDecoder
 public:
  // 初始化
  virtual bool onVaild() override;
  // 解码
  virtual bool decode(const AvoxPacket & packet) override;
  // 解码完成
  virtual void flush() override;
};

}