#pragma once

#include "../VkContext.hpp"

namespace avox {

// VulkanContext与VkPipeGraph构建了一个复杂的计算管线
// 但是有些时间只需要完成一些简单细碎的GPU操作
// 注意,所有API调用请保持在一个线程中
class VkCommand : public VkContextRef {
 private:
  /* data */
  // VkQueue queue = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  // VkQueue computeQueue = VK_NULL_HANDLE;

  // submit后,除非调用过reset,否则后面record不会起作用
  bool bRecord = false;
  bool bSubmit = false;

 public:
  VkCommand(/* args */);
  // VkCommand(VkDevice vdevice);
  virtual ~VkCommand();

 protected:
  virtual void onSetVkContext() override;

 private:
  void beginRecord();
  void endRecord();

 public:
  void barrier(VkBuffer buffer, VkPipelineStageFlags stageFlag,
               VkAccessFlags assessFlag, VkPipelineStageFlags oldStageFlag,
               VkAccessFlags oldAssessFlag);
  void fill(VkBuffer src, int32_t size, int32_t offset = 0, uint32_t value = 0);
  void record(VkBuffer src, VkBuffer dest, int32_t destOffset, int32_t size);

  void submit();
  void reset();

  inline VkCommandBuffer getCommandBuffer() { return cmd; }
};

}

// vkCmdFillBuffer