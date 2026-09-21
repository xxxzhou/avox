#include "VkCommand.hpp"

namespace avox {

// fence等待上限: 正常提交毫秒级完成; 无界等待会让调用线程卡死在GPU同步上,
// 拖穿screenShot的1s等待窗, 等待方超时释放buffer后回来download即UAF(0921实证)
static constexpr uint64_t kSubmitFenceTimeoutNs = 500000000ull;

VkCommand::VkCommand(/* args */) {}

VkCommand::~VkCommand() {
  if (vkDevice && fence) {
    // submit超时路径fence可能未信号, GPU未完成时销毁fence/cmd是UB, 兜底有限等待
    vkWaitForFences(vkDevice, 1, &fence, VK_TRUE, 100000000ull);
    vkDestroyFence(vkDevice, fence, nullptr);
    fence = VK_NULL_HANDLE;
  }
  if (vkDevice && cmd) {
    vkFreeCommandBuffers(vkDevice, cmdPool, 1, &cmd);
    cmd = VK_NULL_HANDLE;
  }
}

void VkCommand::onSetVkContext() {
  // command buffer
  VkCommandBufferAllocateInfo cmdBufInfo = {};
  cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufInfo.commandPool = cmdPool;
  cmdBufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufInfo.commandBufferCount = 1;
  vkAllocateCommandBuffers(vkDevice, &cmdBufInfo, &cmd);
  // 创建cpu-gpu通知
  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  // 初始是没有信号的
  fenceInfo.flags = 0;
  vkCreateFence(vkDevice, &fenceInfo, nullptr, &fence);
  // 设置为记录状态
  beginRecord();
}

void VkCommand::beginRecord() {
  VkCommandBufferBeginInfo cmdBeginInfo = {};
  cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  // VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 只会被执行一次,然后被销毁.
  // VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT 可以多次执行或暂停
  // 把flags设置为0是安全的,能多次执行
  cmdBeginInfo.flags = 0;
  vkBeginCommandBuffer(cmd, &cmdBeginInfo);
  bRecord = true;
  bSubmit = false;
}

void VkCommand::endRecord() {
  // 记录状态改为可执行状态
  vkEndCommandBuffer(cmd);
  bRecord = false;
}

void VkCommand::barrier(VkBuffer buffer, VkPipelineStageFlags stageFlag,
                        VkAccessFlags assessFlag,
                        VkPipelineStageFlags oldStageFlag,
                        VkAccessFlags oldAssessFlag) {
  VkBufferMemoryBarrier bufBarrier = {};
  bufBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bufBarrier.buffer = buffer;
  bufBarrier.offset = 0;
  bufBarrier.size = VK_WHOLE_SIZE;
  bufBarrier.srcAccessMask = oldAssessFlag;
  bufBarrier.dstAccessMask = assessFlag;
  vkCmdPipelineBarrier(cmd, oldStageFlag, stageFlag, 0, 0, nullptr, 1,
                       &bufBarrier, 0, nullptr);
}

void VkCommand::fill(VkBuffer src, int32_t size, int32_t offset,
                     uint32_t value) {
  vkCmdFillBuffer(cmd, src, offset, size, value);
}

void VkCommand::record(VkBuffer src, VkBuffer dest, int32_t destOffset,
                       int32_t size) {
  VkBufferCopy region = {};
  region.srcOffset = destOffset;
  region.dstOffset = destOffset;
  region.size = size;
  vkCmdCopyBuffer(cmd, src, dest, 1, &region);
}

bool VkCommand::submit() {
  if (!bSubmit) {
    endRecord();
    bSubmit = true;
  }
  // 提交GPU执行
  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;
  // vkQueueSubmit同一queue须外部同步: 本路径与graph->run()/VkWindow/超分推理
  // 线程并发提交, 其余提交点早已过lockCommand唯独此处漏(0921 panvox双dump实锤)
  lockCommand();
  VkResult result = vkQueueSubmit(vkComputeQueue, 1, &submitInfo, fence);
  unLockCommand();
  if (result == VK_ERROR_DEVICE_LOST) {
    // 与graph->run()同口径: 提交遇设备丢失过闸触发共享上下文恢复
    VkContext::markLost();
    return false;
  }
  if (result != VK_SUCCESS) {
    return false;
  }
  // 等待GPU执行完成给出信号
  if (vkWaitForFences(vkDevice, 1, &fence, VK_TRUE, kSubmitFenceTimeoutNs) !=
      VK_SUCCESS) {
    return false;
  }
  // 重置信号
  vkResetFences(vkDevice, 1, &fence);
  return true;
}

void VkCommand::reset() {
  // 重置CMD状态
  vkResetCommandBuffer(cmd, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
  // 重新开始记录
  beginRecord();
}

}